/*
 * Copyright 2021 Dmitry Ivanov
 *
 * This file is part of libquentier
 *
 * libquentier is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * libquentier is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libquentier. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PutToDatabaseUtils.h"
#include "NotebookUtils.h"
#include "NoteUtils.h"
#include "RemoveFromDatabaseUtils.h"
#include "ResourceDataFilesUtils.h"
#include "ResourceUtils.h"
#include "TagUtils.h"

#include "../ErrorHandling.h"
#include "../Transaction.h"
#include "../TypeChecks.h"

#include <quentier/types/ErrorString.h>
#include <quentier/types/NoteUtils.h>
#include <quentier/utility/StringUtils.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/User.h>
#include <qevercloud/utility/ToRange.h>

#include <QGlobalStatic>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>

namespace quentier::local_storage::sql::utils {

namespace {

Q_GLOBAL_STATIC(QVariant, gNullValue);

void setNoteIdsToNoteResources(qevercloud::Note & note)
{
    if (!note.resources()) {
        return;
    }

    auto resources = *note.resources();
    for (auto & resource: resources) {
        resource.setNoteLocalId(note.localId());
        if (note.guid()) {
            resource.setNoteGuid(*note.guid());
        }
    }
    note.setResources(resources);
}

[[nodiscard]] bool clearNoteGuid(
    const PutNoteOptions putNoteOptions, qevercloud::Note & note,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (note.resources() &&
        putNoteOptions.testFlag(PutNoteOption::PutResourceMetadata))
    {
        const auto & resources = *note.resources();
        for (const auto & resource: qAsConst(resources))
        {
            if (Q_UNLIKELY(resource.noteGuid())) {
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage::sql::utils",
                    "note's guid is being cleared but one of "
                    "note's resources has non-empty note guid"));
                if (resource.attributes() &&
                    resource.attributes()->fileName()) {
                    errorDescription.details() =
                        *resource.attributes()->fileName();
                }

                QNWARNING("local_storage::sql::utils", errorDescription);
                return false;
            }

            if (Q_UNLIKELY(resource.guid())) {
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage::sql::utils",
                    "note's guid is being cleared but one of "
                    "note's resources has non-empty guid"));
                if (resource.attributes() &&
                    resource.attributes()->fileName()) {
                    errorDescription.details() =
                        *resource.attributes()->fileName();
                }

                QNWARNING("local_storage::sql::utils", errorDescription);
                return false;
            }
        }
    }

    static const QString queryString = QStringLiteral(
        "UPDATE Notes SET guid = NULL WHERE localUid = :localUid");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot clear guid from note: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":localUid"), note.localId());

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot clear guid from note"),
        false);

    return true;
}

void bindNoteApplicationData(
    const qevercloud::LazyMap & applicationData, QSqlQuery & query)
{
    if (applicationData.keysOnly()) {
        const QSet<QString> & keysOnly = *applicationData.keysOnly();
        QString keysOnlyString;
        QTextStream strm{&keysOnlyString};

        for (const auto & key: keysOnly) {
            strm << "'" << key << "'";
        }

        query.bindValue(
            QStringLiteral(":applicationDataKeysOnly"),
            keysOnlyString);
    }
    else {
        query.bindValue(
            QStringLiteral(":applicationDataKeysOnly"), *gNullValue);
    }

    if (applicationData.fullMap()) {
        const QMap<QString, QString> & fullMap = *applicationData.fullMap();
        QString fullMapKeysString;
        QTextStream fullMapKeysStrm{&fullMapKeysString};

        QString fullMapValuesString;
        QTextStream fullMapValuesStrm{&fullMapValuesString};

        for (const auto it: qevercloud::toRange(fullMap)) {
            fullMapKeysStrm << "'" << it.key() << "'";
            fullMapValuesStrm << "'" << it.value() << "'";
        }

        query.bindValue(
            QStringLiteral(":applicationDataKeysMap"),
            fullMapKeysString);

        query.bindValue(
            QStringLiteral(":applicationDataValues"),
            fullMapValuesString);
    }
    else {
        query.bindValue(
            QStringLiteral(":applicationDataKeysMap"), *gNullValue);

        query.bindValue(
            QStringLiteral(":applicationDataValues"), *gNullValue);
    }
}

void bindNullNoteApplicationData(QSqlQuery & query)
{
    query.bindValue(QStringLiteral(":applicationDataKeysOnly"), *gNullValue);
    query.bindValue(QStringLiteral(":applicationDataKeysMap"), *gNullValue);
    query.bindValue(QStringLiteral(":applicationDataValues"), *gNullValue);
}

void bindNoteClassifications(
    const QMap<QString, QString> & classifications, QSqlQuery & query)
{
    QString classificationKeys;
    QTextStream keysStrm{&classificationKeys};

    QString classificationValues;
    QTextStream valuesStrm{&classificationValues};

    for (const auto it: qevercloud::toRange(classifications)) {
        keysStrm << "'" << it.key() << "'";
        valuesStrm << "'" << it.value() << "'";
    }

    query.bindValue(
        QStringLiteral(":classificationKeys"), classificationKeys);

    query.bindValue(
        QStringLiteral(":classificationValues"),
        classificationValues);
}

void bindNullNoteClassifications(QSqlQuery & query)
{
    query.bindValue(QStringLiteral(":classificationKeys"), *gNullValue);
    query.bindValue(QStringLiteral(":classificationValues"), *gNullValue);
}

void bindNoteAttributes(
    const qevercloud::NoteAttributes & attributes, QSqlQuery & query)
{
    const auto bindAttribute = [&](const QString & name, auto getter)
    {
        const auto value = getter();
        query.bindValue(name, value ? *value : *gNullValue);
    };

    bindAttribute(QStringLiteral(":subjectDate"), [&] {
        return attributes.subjectDate();
    });

    bindAttribute(QStringLiteral(":latitude"), [&] {
        return attributes.latitude();
    });

    bindAttribute(QStringLiteral(":longitude"), [&] {
        return attributes.longitude();
    });

    bindAttribute(QStringLiteral(":altitude"), [&] {
        return attributes.altitude();
    });

    bindAttribute(QStringLiteral(":author"), [&] {
        return attributes.author();
    });

    bindAttribute(QStringLiteral(":source"), [&] {
        return attributes.source();
    });

    bindAttribute(QStringLiteral(":sourceURL"), [&] {
        return attributes.sourceURL();
    });

    bindAttribute(QStringLiteral(":sourceApplication"), [&] {
        return attributes.sourceApplication();
    });

    bindAttribute(QStringLiteral(":shareDate"), [&] {
        return attributes.shareDate();
    });

    bindAttribute(QStringLiteral(":reminderOrder"), [&] {
        return attributes.reminderOrder();
    });

    bindAttribute(QStringLiteral(":reminderDoneTime"), [&] {
        return attributes.reminderDoneTime();
    });

    bindAttribute(QStringLiteral(":reminderTime"), [&] {
        return attributes.reminderTime();
    });

    bindAttribute(QStringLiteral(":placeName"), [&] {
        return attributes.placeName();
    });

    bindAttribute(QStringLiteral(":contentClass"), [&] {
        return attributes.contentClass();
    });

    bindAttribute(QStringLiteral(":lastEditedBy"), [&] {
        return attributes.lastEditedBy();
    });

    bindAttribute(QStringLiteral(":creatorId"), [&] {
        return attributes.creatorId();
    });

    bindAttribute(QStringLiteral(":lastEditorId"), [&] {
        return attributes.lastEditorId();
    });

    bindAttribute(QStringLiteral(":sharedWithBusiness"), [&] {
        return attributes.sharedWithBusiness();
    });

    bindAttribute(QStringLiteral(":conflictSourceNoteGuid"), [&] {
        return attributes.conflictSourceNoteGuid();
    });

    bindAttribute(QStringLiteral(":noteTitleQuality"), [&] {
        return attributes.noteTitleQuality();
    });

    if (attributes.applicationData()) {
        bindNoteApplicationData(*attributes.applicationData(), query);
    }
    else {
        bindNullNoteApplicationData(query);
    }

    if (attributes.classifications()) {
        bindNoteClassifications(*attributes.classifications(), query);
    }
    else {
        bindNullNoteClassifications(query);
    }
}

void bindNullNoteAttributes(QSqlQuery & query)
{
    const auto bindNullAttribute = [&](const QString & name)
    {
        query.bindValue(name, *gNullValue);
    };

    bindNullAttribute(QStringLiteral(":subjectDate"));
    bindNullAttribute(QStringLiteral(":latitude"));
    bindNullAttribute(QStringLiteral(":longitude"));
    bindNullAttribute(QStringLiteral(":altitude"));
    bindNullAttribute(QStringLiteral(":author"));
    bindNullAttribute(QStringLiteral(":source"));
    bindNullAttribute(QStringLiteral(":sourceURL"));
    bindNullAttribute(QStringLiteral(":sourceApplication"));
    bindNullAttribute(QStringLiteral(":shareDate"));
    bindNullAttribute(QStringLiteral(":reminderOrder"));
    bindNullAttribute(QStringLiteral(":reminderDoneTime"));
    bindNullAttribute(QStringLiteral(":reminderTime"));
    bindNullAttribute(QStringLiteral(":placeName"));
    bindNullAttribute(QStringLiteral(":contentClass"));
    bindNullAttribute(QStringLiteral(":lastEditedBy"));
    bindNullAttribute(QStringLiteral(":creatorId"));
    bindNullAttribute(QStringLiteral(":lastEditorId"));
    bindNullAttribute(QStringLiteral(":sharedWithBusiness"));
    bindNullAttribute(QStringLiteral(":conflictSourceNoteGuid"));
    bindNullAttribute(QStringLiteral(":noteTitleQuality"));

    bindNullNoteApplicationData(query);
    bindNullNoteClassifications(query);
}

} // namespace

bool putUser(
    const qevercloud::User & user, QSqlDatabase & database,
    ErrorString & errorDescription, const TransactionOption transactionOption)
{
    QNDEBUG("local_storage::sql::utils", "putUser: " << user);

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't put user into the local storage database")};

    ErrorString error;
    if (!checkUser(user, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage:sql:utils", error << "\nUser: " << user);
        return false;
    }

    std::optional<Transaction> transaction;
    if (transactionOption == TransactionOption::UseSeparateTransaction) {
        transaction.emplace(database, Transaction::Type::Exclusive);
    }

    const QString userId = QString::number(*user.id());

    if (!putCommonUserData(user, userId, database, errorDescription)) {
        return false;
    }

    if (user.attributes()) {
        if (!putUserAttributes(
                *user.attributes(), userId, database, errorDescription)) {
            return false;
        }
    }
    else if (!removeUserAttributes(userId, database, errorDescription)) {
        return false;
    }

    if (user.accounting()) {
        if (!putAccounting(
                *user.accounting(), userId, database, errorDescription)) {
            return false;
        }
    }
    else if (!removeAccounting(userId, database, errorDescription)) {
        return false;
    }

    if (user.accountLimits()) {
        if (!putAccountLimits(
                *user.accountLimits(), userId, database, errorDescription))
        {
            return false;
        }
    }
    else if (!removeAccountLimits(userId, database, errorDescription)) {
        return false;
    }

    if (user.businessUserInfo()) {
        if (!putBusinessUserInfo(
                *user.businessUserInfo(), userId, database, errorDescription))
        {
            return false;
        }
    }
    else if (!removeBusinessUserInfo(userId, database, errorDescription)) {
        return false;
    }

    if (transactionOption == TransactionOption::UseSeparateTransaction) {
        const bool res = transaction->commit();
        ENSURE_DB_REQUEST_RETURN(
            res, database, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot put user into the local storage database, failed to "
                "commit"),
            false);
    }

    return true;
}

bool putCommonUserData(
    const qevercloud::User & user, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Users"
        "(id, username, email, name, timezone, privilege, "
        "serviceLevel, userCreationTimestamp, "
        "userModificationTimestamp, userIsDirty, "
        "userIsLocal, userDeletionTimestamp, userIsActive, "
        "userShardId, userPhotoUrl, userPhotoLastUpdateTimestamp) "
        "VALUES(:id, :username, :email, :name, :timezone, "
        ":privilege, :serviceLevel, :userCreationTimestamp, "
        ":userModificationTimestamp, :userIsDirty, :userIsLocal, "
        ":userDeletionTimestamp, :userIsActive, :userShardId, "
        ":userPhotoUrl, :userPhotoLastUpdateTimestamp)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put common user data into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        QStringLiteral(":username"),
        (user.username() ? *user.username() : *gNullValue));

    query.bindValue(
        QStringLiteral(":email"), (user.email() ? *user.email() : *gNullValue));

    query.bindValue(
        QStringLiteral(":name"), (user.name() ? *user.name() : *gNullValue));

    query.bindValue(
        QStringLiteral(":timezone"),
        (user.timezone() ? *user.timezone() : *gNullValue));

    query.bindValue(
        QStringLiteral(":privilege"),
        (user.privilege() ? static_cast<int>(*user.privilege()) : *gNullValue));

    query.bindValue(
        QStringLiteral(":serviceLevel"),
        (user.serviceLevel() ? static_cast<int>(*user.serviceLevel())
                             : *gNullValue));

    query.bindValue(
        QStringLiteral(":userCreationTimestamp"),
        (user.created() ? *user.created() : *gNullValue));

    query.bindValue(
        QStringLiteral(":userModificationTimestamp"),
        (user.updated() ? *user.updated() : *gNullValue));

    query.bindValue(
        QStringLiteral(":userIsDirty"), (user.isLocallyModified() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":userIsLocal"), (user.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":userDeletionTimestamp"),
        (user.deleted() ? *user.deleted() : *gNullValue));

    query.bindValue(
        QStringLiteral(":userIsActive"),
        (user.active() ? (*user.active() ? 1 : 0) : *gNullValue));

    query.bindValue(
        QStringLiteral(":userShardId"),
        (user.shardId() ? *user.shardId() : *gNullValue));

    query.bindValue(
        QStringLiteral(":userPhotoUrl"),
        (user.photoUrl() ? *user.photoUrl() : *gNullValue));

    query.bindValue(
        QStringLiteral(":userPhotoLastUpdateTimestamp"),
        (user.photoLastUpdated() ? *user.photoLastUpdated() : *gNullValue));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put common user data into the local storage database"),
        false);

    return true;
}

bool putUserAttributes(
    const qevercloud::UserAttributes & userAttributes, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (!putUserAttributesViewedPromotions(
            userId, userAttributes.viewedPromotions(), database,
            errorDescription))
    {
        return false;
    }

    if (!putUserAttributesRecentMailedAddresses(
            userId, userAttributes.recentMailedAddresses(), database,
            errorDescription))
    {
        return false;
    }

    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO UserAttributes"
        "(id, defaultLocationName, defaultLatitude, "
        "defaultLongitude, preactivation, "
        "incomingEmailAddress, comments, "
        "dateAgreedToTermsOfService, maxReferrals, "
        "referralCount, refererCode, sentEmailDate, "
        "sentEmailCount, dailyEmailLimit, "
        "emailOptOutDate, partnerEmailOptInDate, "
        "preferredLanguage, preferredCountry, "
        "clipFullPage, twitterUserName, twitterId, "
        "groupName, recognitionLanguage, "
        "referralProof, educationalDiscount, "
        "businessAddress, hideSponsorBilling, "
        "useEmailAutoFiling, reminderEmailConfig, "
        "emailAddressLastConfirmed, passwordUpdated, "
        "salesforcePushEnabled, shouldLogClientEvent) "
        "VALUES(:id, :defaultLocationName, :defaultLatitude, "
        ":defaultLongitude, :preactivation, "
        ":incomingEmailAddress, :comments, "
        ":dateAgreedToTermsOfService, :maxReferrals, "
        ":referralCount, :refererCode, :sentEmailDate, "
        ":sentEmailCount, :dailyEmailLimit, "
        ":emailOptOutDate, :partnerEmailOptInDate, "
        ":preferredLanguage, :preferredCountry, "
        ":clipFullPage, :twitterUserName, :twitterId, "
        ":groupName, :recognitionLanguage, "
        ":referralProof, :educationalDiscount, "
        ":businessAddress, :hideSponsorBilling, "
        ":useEmailAutoFiling, :reminderEmailConfig, "
        ":emailAddressLastConfirmed, :passwordUpdated, "
        ":salesforcePushEnabled, :shouldLogClientEvent)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user attributes into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        QStringLiteral(":defaultLocationName"),
        (userAttributes.defaultLocationName()
             ? *userAttributes.defaultLocationName()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":defaultLatitude"),
        (userAttributes.defaultLatitude() ? *userAttributes.defaultLatitude()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":defaultLongitude"),
        (userAttributes.defaultLongitude() ? *userAttributes.defaultLongitude()
                                           : *gNullValue));

    query.bindValue(
        QStringLiteral(":incomingEmailAddress"),
        (userAttributes.incomingEmailAddress()
             ? *userAttributes.incomingEmailAddress()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":comments"),
        (userAttributes.comments() ? *userAttributes.comments() : *gNullValue));

    query.bindValue(
        QStringLiteral(":dateAgreedToTermsOfService"),
        (userAttributes.dateAgreedToTermsOfService()
             ? *userAttributes.dateAgreedToTermsOfService()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":maxReferrals"),
        (userAttributes.maxReferrals() ? *userAttributes.maxReferrals()
                                       : *gNullValue));

    query.bindValue(
        QStringLiteral(":referralCount"),
        (userAttributes.referralCount() ? *userAttributes.referralCount()
                                        : *gNullValue));

    query.bindValue(
        QStringLiteral(":refererCode"),
        (userAttributes.refererCode() ? *userAttributes.refererCode()
                                      : *gNullValue));

    query.bindValue(
        QStringLiteral(":sentEmailDate"),
        (userAttributes.sentEmailDate() ? *userAttributes.sentEmailDate()
                                        : *gNullValue));

    query.bindValue(
        QStringLiteral(":sentEmailCount"),
        (userAttributes.sentEmailCount() ? *userAttributes.sentEmailCount()
                                         : *gNullValue));

    query.bindValue(
        QStringLiteral(":dailyEmailLimit"),
        (userAttributes.dailyEmailLimit() ? *userAttributes.dailyEmailLimit()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":emailOptOutDate"),
        (userAttributes.emailOptOutDate() ? *userAttributes.emailOptOutDate()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":partnerEmailOptInDate"),
        (userAttributes.partnerEmailOptInDate()
             ? *userAttributes.partnerEmailOptInDate()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":preferredLanguage"),
        (userAttributes.preferredLanguage()
             ? *userAttributes.preferredLanguage()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":preferredCountry"),
        (userAttributes.preferredCountry() ? *userAttributes.preferredCountry()
                                           : *gNullValue));

    query.bindValue(
        QStringLiteral(":twitterUserName"),
        (userAttributes.twitterUserName() ? *userAttributes.twitterUserName()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":twitterId"),
        (userAttributes.twitterId() ? *userAttributes.twitterId()
                                    : *gNullValue));

    query.bindValue(
        QStringLiteral(":groupName"),
        (userAttributes.groupName() ? *userAttributes.groupName()
                                    : *gNullValue));

    query.bindValue(
        QStringLiteral(":recognitionLanguage"),
        (userAttributes.recognitionLanguage()
             ? *userAttributes.recognitionLanguage()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":referralProof"),
        (userAttributes.referralProof() ? *userAttributes.referralProof()
                                        : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessAddress"),
        (userAttributes.businessAddress() ? *userAttributes.businessAddress()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":reminderEmailConfig"),
        (userAttributes.reminderEmailConfig()
             ? static_cast<int>(*userAttributes.reminderEmailConfig())
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":emailAddressLastConfirmed"),
        (userAttributes.emailAddressLastConfirmed()
             ? *userAttributes.emailAddressLastConfirmed()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":passwordUpdated"),
        (userAttributes.passwordUpdated() ? *userAttributes.passwordUpdated()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":preactivation"),
        (userAttributes.preactivation()
             ? (*userAttributes.preactivation() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":clipFullPage"),
        (userAttributes.clipFullPage()
             ? (*userAttributes.clipFullPage() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":educationalDiscount"),
        (userAttributes.educationalDiscount()
             ? (*userAttributes.educationalDiscount() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":hideSponsorBilling"),
        (userAttributes.hideSponsorBilling()
             ? (*userAttributes.hideSponsorBilling() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":useEmailAutoFiling"),
        (userAttributes.useEmailAutoFiling()
             ? (*userAttributes.useEmailAutoFiling() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":salesforcePushEnabled"),
        (userAttributes.salesforcePushEnabled()
             ? (*userAttributes.salesforcePushEnabled() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":shouldLogClientEvent"),
        (userAttributes.shouldLogClientEvent()
             ? (*userAttributes.shouldLogClientEvent() ? 1 : 0)
             : *gNullValue));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user attributes into the local storage database"),
        false);

    return true;
}

bool putUserAttributesViewedPromotions(
    const QString & userId, const std::optional<QStringList> & viewedPromotions,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (!removeUserAttributesViewedPromotions(
            userId, database, errorDescription)) {
        return false;
    }

    if (!viewedPromotions || viewedPromotions->isEmpty()) {
        return true;
    }

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO UserAttributesViewedPromotions"
        "(id, promotion) VALUES(:id, :promotion)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user attributes' viewer promotions into the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    for (const auto & viewedPromotion: *viewedPromotions) {
        query.bindValue(QStringLiteral(":promotion"), viewedPromotion);
        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot put user attributes' viewer promotions into the local "
                "storage database"),
            false);
    }

    return true;
}

bool putUserAttributesRecentMailedAddresses(
    const QString & userId,
    const std::optional<QStringList> & recentMailedAddresses,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (!removeUserAttributesRecentMailedAddresses(
            userId, database, errorDescription))
    {
        return false;
    }

    if (!recentMailedAddresses || recentMailedAddresses->isEmpty()) {
        return true;
    }

    const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO UserAttributesRecentMailedAddresses"
        "(id, address) VALUES(:id, :address)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user attributes' recent mailed addresses into "
            "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    for (const auto & recentMailedAddress: *recentMailedAddresses) {
        query.bindValue(QStringLiteral(":address"), recentMailedAddress);
        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot put user attributes' recent mailed addresses into "
                "the local storage database"),
            false);
    }

    return true;
}

bool putAccounting(
    const qevercloud::Accounting & accounting, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Accounting"
        "(id, uploadLimitEnd, uploadLimitNextMonth, "
        "premiumServiceStatus, premiumOrderNumber, "
        "premiumCommerceService, premiumServiceStart, "
        "premiumServiceSKU, lastSuccessfulCharge, "
        "lastFailedCharge, lastFailedChargeReason, nextPaymentDue, "
        "premiumLockUntil, updated, premiumSubscriptionNumber, "
        "lastRequestedCharge, currency, unitPrice, unitDiscount, "
        "nextChargeDate, availablePoints) "
        "VALUES(:id, :uploadLimitEnd, :uploadLimitNextMonth, "
        ":premiumServiceStatus, :premiumOrderNumber, "
        ":premiumCommerceService, :premiumServiceStart, "
        ":premiumServiceSKU, :lastSuccessfulCharge, "
        ":lastFailedCharge, :lastFailedChargeReason, "
        ":nextPaymentDue, :premiumLockUntil, :updated, "
        ":premiumSubscriptionNumber, :lastRequestedCharge, "
        ":currency, :unitPrice, :unitDiscount, :nextChargeDate, "
        ":availablePoints)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user's accounting data into the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        ":uploadLimitEnd",
        accounting.uploadLimitEnd() ? *accounting.uploadLimitEnd()
                                    : *gNullValue);

    query.bindValue(
        ":uploadLimitNextMonth",
        accounting.uploadLimitNextMonth() ? *accounting.uploadLimitNextMonth()
                                          : *gNullValue);

    query.bindValue(
        ":premiumServiceStatus",
        accounting.premiumServiceStatus()
            ? static_cast<int>(*accounting.premiumServiceStatus())
            : *gNullValue);

    query.bindValue(
        ":premiumOrderNumber",
        accounting.premiumOrderNumber() ? *accounting.premiumOrderNumber()
                                        : *gNullValue);

    query.bindValue(
        ":premiumCommerceService",
        accounting.premiumCommerceService()
            ? *accounting.premiumCommerceService()
            : *gNullValue);

    query.bindValue(
        ":premiumServiceStart",
        accounting.premiumServiceStart() ? *accounting.premiumServiceStart()
                                         : *gNullValue);

    query.bindValue(
        ":premiumServiceSKU",
        accounting.premiumServiceSKU() ? *accounting.premiumServiceSKU()
                                       : *gNullValue);

    query.bindValue(
        ":lastSuccessfulCharge",
        accounting.lastSuccessfulCharge() ? *accounting.lastSuccessfulCharge()
                                          : *gNullValue);

    query.bindValue(
        ":lastFailedCharge",
        accounting.lastFailedCharge() ? *accounting.lastFailedCharge()
                                      : *gNullValue);

    query.bindValue(
        ":lastFailedChargeReason",
        accounting.lastFailedChargeReason()
            ? *accounting.lastFailedChargeReason()
            : *gNullValue);

    query.bindValue(
        ":nextPaymentDue",
        accounting.nextPaymentDue() ? *accounting.nextPaymentDue()
                                    : *gNullValue);

    query.bindValue(
        ":premiumLockUntil",
        accounting.premiumLockUntil() ? *accounting.premiumLockUntil()
                                      : *gNullValue);

    query.bindValue(
        ":updated", accounting.updated() ? *accounting.updated() : *gNullValue);

    query.bindValue(
        ":premiumSubscriptionNumber",
        accounting.premiumSubscriptionNumber()
            ? *accounting.premiumSubscriptionNumber()
            : *gNullValue);

    query.bindValue(
        ":lastRequestedCharge",
        accounting.lastRequestedCharge() ? *accounting.lastRequestedCharge()
                                         : *gNullValue);

    query.bindValue(
        ":currency",
        accounting.currency() ? *accounting.currency() : *gNullValue);

    query.bindValue(
        ":unitPrice",
        accounting.unitPrice() ? *accounting.unitPrice() : *gNullValue);

    query.bindValue(
        ":unitDiscount",
        accounting.unitDiscount() ? *accounting.unitDiscount() : *gNullValue);

    query.bindValue(
        ":nextChargeDate",
        accounting.nextChargeDate() ? *accounting.nextChargeDate()
                                    : *gNullValue);

    query.bindValue(
        ":availablePoints",
        accounting.availablePoints() ? *accounting.availablePoints()
                                     : *gNullValue);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user's accounting data into the local storage "
            "database"),
        false);

    return true;
}

bool putAccountLimits(
    const qevercloud::AccountLimits & accountLimits, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO AccountLimits"
        "(id, userMailLimitDaily, noteSizeMax, resourceSizeMax, "
        "userLinkedNotebookMax, uploadLimit, userNoteCountMax, "
        "userNotebookCountMax, userTagCountMax, noteTagCountMax, "
        "userSavedSearchesMax, noteResourceCountMax) "
        "VALUES(:id, :userMailLimitDaily, :noteSizeMax, "
        ":resourceSizeMax, :userLinkedNotebookMax, :uploadLimit, "
        ":userNoteCountMax, :userNotebookCountMax, "
        ":userTagCountMax, :noteTagCountMax, "
        ":userSavedSearchesMax, :noteResourceCountMax)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user's account limits into the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        QStringLiteral(":userMailLimitDaily"),
        accountLimits.userMailLimitDaily() ? *accountLimits.userMailLimitDaily()
                                           : *gNullValue);

    query.bindValue(
        QStringLiteral(":noteSizeMax"),
        accountLimits.noteSizeMax() ? *accountLimits.noteSizeMax()
                                    : *gNullValue);

    query.bindValue(
        QStringLiteral(":resourceSizeMax"),
        accountLimits.resourceSizeMax() ? *accountLimits.resourceSizeMax()
                                        : *gNullValue);

    query.bindValue(
        QStringLiteral(":userLinkedNotebookMax"),
        accountLimits.userLinkedNotebookMax()
            ? *accountLimits.userLinkedNotebookMax()
            : *gNullValue);

    query.bindValue(
        QStringLiteral(":uploadLimit"),
        accountLimits.uploadLimit() ? *accountLimits.uploadLimit()
                                    : *gNullValue);

    query.bindValue(
        QStringLiteral(":userNoteCountMax"),
        accountLimits.userNoteCountMax() ? *accountLimits.userNoteCountMax()
                                         : *gNullValue);

    query.bindValue(
        QStringLiteral(":userNotebookCountMax"),
        accountLimits.userNotebookCountMax()
            ? *accountLimits.userNotebookCountMax()
            : *gNullValue);

    query.bindValue(
        QStringLiteral(":userTagCountMax"),
        accountLimits.userTagCountMax() ? *accountLimits.userTagCountMax()
                                        : *gNullValue);

    query.bindValue(
        QStringLiteral(":noteTagCountMax"),
        accountLimits.noteTagCountMax() ? *accountLimits.noteTagCountMax()
                                        : *gNullValue);

    query.bindValue(
        QStringLiteral(":userSavedSearchesMax"),
        accountLimits.userSavedSearchesMax()
            ? *accountLimits.userSavedSearchesMax()
            : *gNullValue);

    query.bindValue(
        QStringLiteral(":noteResourceCountMax"),
        accountLimits.noteResourceCountMax()
            ? *accountLimits.noteResourceCountMax()
            : *gNullValue);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put user's account limits into the local storage database"),
        false);

    return true;
}

bool putBusinessUserInfo(
    const qevercloud::BusinessUserInfo & info, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO BusinessUserInfo"
        "(id, businessId, businessName, role, businessInfoEmail) "
        "VALUES(:id, :businessId, :businessName, :role, :businessInfoEmail)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put business user info into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        QStringLiteral(":businessId"),
        (info.businessId() ? *info.businessId() : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessName"),
        (info.businessName() ? *info.businessName() : *gNullValue));

    query.bindValue(
        QStringLiteral(":role"),
        (info.role() ? static_cast<int>(*info.role()) : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessInfoEmail"),
        (info.email() ? *info.email() : *gNullValue));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put business user info into the local storage database"),
        false);

    return true;
}

bool putNotebook(
    qevercloud::Notebook notebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG("local_storage::sql::utils", "putNotebook: " << notebook);

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't put notebook into the local storage database")};

    ErrorString error;
    if (!checkNotebook(notebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils", error << "\nNotebook: " << notebook);
        return false;
    }

    Transaction transaction{database, Transaction::Type::Exclusive};

    error.clear();
    const auto localId = notebookLocalId(notebook, database, error);
    if (localId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils",
            errorDescription << "\nNotebook: " << notebook);
        return false;
    }

    if (notebook.localId() != localId) {
        notebook.setLocalId(localId);
    }

    if (!putCommonNotebookData(notebook, database, errorDescription)) {
        return false;
    }

    if (notebook.restrictions()) {
        if (!putNotebookRestrictions(
                localId, *notebook.restrictions(), database, errorDescription))
        {
            return false;
        }
    }
    else if (!removeNotebookRestrictions(localId, database, errorDescription)) {
        return false;
    }

    if (notebook.guid()) {
        if (!removeSharedNotebooks(
                *notebook.guid(), database, errorDescription)) {
            return false;
        }

        if (notebook.sharedNotebooks() &&
            !notebook.sharedNotebooks()->isEmpty()) {
            int indexInNotebook = 0;
            for (const auto & sharedNotebook:
                 qAsConst(*notebook.sharedNotebooks())) {
                if (!sharedNotebook.id()) {
                    QNWARNING(
                        "local_storage::sql::utils",
                        "Found shared notebook without primary identifier "
                        "of the share set, skipping it: "
                            << sharedNotebook);
                    continue;
                }

                if (!putSharedNotebook(
                        sharedNotebook, indexInNotebook, database,
                        errorDescription))
                {
                    return false;
                }

                ++indexInNotebook;
            }
        }
    }

    if (notebook.contact() &&
        !putUser(
            *notebook.contact(), database, errorDescription,
            TransactionOption::DontUseSeparateTransaction))
    {
        return false;
    }

    const bool res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put notebook into the local storage database, failed to "
            "commit"),
        false);

    return true;
}

bool putCommonNotebookData(
    const qevercloud::Notebook & notebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Notebooks"
        "(localUid, guid, linkedNotebookGuid, "
        "updateSequenceNumber, notebookName, notebookNameUpper, "
        "creationTimestamp, modificationTimestamp, isDirty, "
        "isLocal, isDefault, isLastUsed, isFavorited, "
        "publishingUri, publishingNoteSortOrder, "
        "publishingAscendingSort, publicDescription, isPublished, "
        "stack, businessNotebookDescription, "
        "businessNotebookPrivilegeLevel, "
        "businessNotebookIsRecommended, contactId, "
        "recipientReminderNotifyEmail, recipientReminderNotifyInApp, "
        "recipientInMyList, recipientStack) "
        "VALUES(:localUid, :guid, :linkedNotebookGuid, "
        ":updateSequenceNumber, :notebookName, :notebookNameUpper, "
        ":creationTimestamp, :modificationTimestamp, :isDirty, "
        ":isLocal, :isDefault, :isLastUsed, :isFavorited, "
        ":publishingUri, :publishingNoteSortOrder, "
        ":publishingAscendingSort, :publicDescription, "
        ":isPublished, :stack, :businessNotebookDescription, "
        ":businessNotebookPrivilegeLevel, "
        ":businessNotebookIsRecommended, :contactId, "
        ":recipientReminderNotifyEmail, "
        ":recipientReminderNotifyInApp, :recipientInMyList, "
        ":recipientStack)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put common notebook data into the local storage database: "
            "failed to prepare query"),
        false);

    const auto & localId = notebook.localId();
    query.bindValue(
        QStringLiteral(":localUid"),
        (localId.isEmpty() ? *gNullValue : localId));

    query.bindValue(
        QStringLiteral(":guid"),
        (notebook.guid() ? *notebook.guid() : *gNullValue));

    const QString linkedNotebookGuid =
        notebook.linkedNotebookGuid().value_or(QString{});

    query.bindValue(
        QStringLiteral(":linkedNotebookGuid"),
        (!linkedNotebookGuid.isEmpty() ? linkedNotebookGuid : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (notebook.updateSequenceNum() ? *notebook.updateSequenceNum()
                                      : *gNullValue));

    query.bindValue(
        QStringLiteral(":notebookName"),
        (notebook.name() ? *notebook.name() : *gNullValue));

    query.bindValue(
        QStringLiteral(":notebookNameUpper"),
        (notebook.name() ? notebook.name()->toUpper() : *gNullValue));

    query.bindValue(
        QStringLiteral(":creationTimestamp"),
        (notebook.serviceCreated() ? *notebook.serviceCreated() : *gNullValue));

    query.bindValue(
        QStringLiteral(":modificationTimestamp"),
        (notebook.serviceUpdated() ? *notebook.serviceUpdated() : *gNullValue));

    query.bindValue(
        QStringLiteral(":isDirty"), (notebook.isLocallyModified() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isLocal"), (notebook.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isDefault"),
        (notebook.defaultNotebook() && *notebook.defaultNotebook()
             ? 1
             : *gNullValue));

    bool isLastUsed = false;
    if (const auto it =
            notebook.localData().constFind(QStringLiteral("lastUsed"));
        it != notebook.localData().constEnd())
    {
        isLastUsed = it.value().toBool();
    }

    query.bindValue(
        QStringLiteral(":isLastUsed"), (isLastUsed ? 1 : *gNullValue));

    query.bindValue(
        QStringLiteral(":isFavorited"),
        (notebook.isLocallyFavorited() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":publishingUri"),
        (notebook.publishing()
             ? (notebook.publishing()->uri() ? *notebook.publishing()->uri()
                                             : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":publishingNoteSortOrder"),
        (notebook.publishing()
             ? (notebook.publishing()->order()
                    ? static_cast<int>(*notebook.publishing()->order())
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":publishingAscendingSort"),
        (notebook.publishing()
             ? (notebook.publishing()->ascending()
                    ? static_cast<int>(*notebook.publishing()->ascending())
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":publicDescription"),
        (notebook.publishing()
             ? (notebook.publishing()->publicDescription()
                    ? *notebook.publishing()->publicDescription()
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":isPublished"),
        (notebook.published() ? static_cast<int>(*notebook.published())
                              : *gNullValue));

    query.bindValue(
        QStringLiteral(":stack"),
        (notebook.stack() ? *notebook.stack() : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessNotebookDescription"),
        (notebook.businessNotebook()
             ? (notebook.businessNotebook()->notebookDescription()
                    ? *notebook.businessNotebook()->notebookDescription()
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessNotebookPrivilegeLevel"),
        (notebook.businessNotebook()
             ? (notebook.businessNotebook()->privilege()
                    ? static_cast<int>(
                          *notebook.businessNotebook()->privilege())
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessNotebookIsRecommended"),
        (notebook.businessNotebook()
             ? (notebook.businessNotebook()->recommended()
                    ? static_cast<int>(
                          *notebook.businessNotebook()->recommended())
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":contactId"),
        ((notebook.contact() && notebook.contact()->id())
             ? *notebook.contact()->id()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientReminderNotifyEmail"),
        (notebook.recipientSettings()
             ? (notebook.recipientSettings()->reminderNotifyEmail()
                    ? static_cast<int>(
                          *notebook.recipientSettings()->reminderNotifyEmail())
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientReminderNotifyInApp"),
        (notebook.recipientSettings()
             ? (notebook.recipientSettings()->reminderNotifyInApp()
                    ? static_cast<int>(
                          *notebook.recipientSettings()->reminderNotifyInApp())
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientInMyList"),
        (notebook.recipientSettings()
             ? (notebook.recipientSettings()->inMyList()
                    ? static_cast<int>(
                          *notebook.recipientSettings()->inMyList())
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":recipientStack"),
        (notebook.recipientSettings()
             ? (notebook.recipientSettings()->stack()
                    ? *notebook.recipientSettings()->stack()
                    : *gNullValue)
             : *gNullValue));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put common notebook data into the local storage database"),
        false);

    return true;
}

bool putNotebookRestrictions(
    const QString & localId,
    const qevercloud::NotebookRestrictions & notebookRestrictions,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NotebookRestrictions"
        "(localUid, noReadNotes, noCreateNotes, noUpdateNotes, "
        "noExpungeNotes, noShareNotes, noEmailNotes, "
        "noSendMessageToRecipients, noUpdateNotebook, "
        "noExpungeNotebook, noSetDefaultNotebook, "
        "noSetNotebookStack, noPublishToPublic, "
        "noPublishToBusinessLibrary, noCreateTags, noUpdateTags, "
        "noExpungeTags, noSetParentTag, noCreateSharedNotebooks, "
        "noShareNotesWithBusiness, noRenameNotebook, "
        "updateWhichSharedNotebookRestrictions, "
        "expungeWhichSharedNotebookRestrictions) "
        "VALUES(:localUid, :noReadNotes, :noCreateNotes, "
        ":noUpdateNotes, :noExpungeNotes, :noShareNotes, "
        ":noEmailNotes, :noSendMessageToRecipients, "
        ":noUpdateNotebook, :noExpungeNotebook, "
        ":noSetDefaultNotebook, :noSetNotebookStack, "
        ":noPublishToPublic, :noPublishToBusinessLibrary, "
        ":noCreateTags, :noUpdateTags, :noExpungeTags, "
        ":noSetParentTag, :noCreateSharedNotebooks, "
        ":noShareNotesWithBusiness, :noRenameNotebook, "
        ":updateWhichSharedNotebookRestrictions, "
        ":expungeWhichSharedNotebookRestrictions)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put notebook restrictions into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(
        QStringLiteral(":localUid"),
        (localId.isEmpty() ? *gNullValue : localId));

    query.bindValue(
        QStringLiteral(":noReadNotes"),
        (notebookRestrictions.noReadNotes()
             ? (*notebookRestrictions.noReadNotes() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noCreateNotes"),
        (notebookRestrictions.noCreateNotes()
             ? (*notebookRestrictions.noCreateNotes() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noUpdateNotes"),
        (notebookRestrictions.noUpdateNotes()
             ? (*notebookRestrictions.noUpdateNotes() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noExpungeNotes"),
        (notebookRestrictions.noExpungeNotes()
             ? (*notebookRestrictions.noExpungeNotes() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noShareNotes"),
        (notebookRestrictions.noShareNotes()
             ? (*notebookRestrictions.noShareNotes() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noEmailNotes"),
        (notebookRestrictions.noEmailNotes()
             ? (*notebookRestrictions.noEmailNotes() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSendMessageToRecipients"),
        (notebookRestrictions.noSendMessageToRecipients()
             ? (*notebookRestrictions.noSendMessageToRecipients() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noUpdateNotebook"),
        (notebookRestrictions.noUpdateNotebook()
             ? (*notebookRestrictions.noUpdateNotebook() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noExpungeNotebook"),
        (notebookRestrictions.noExpungeNotebook()
             ? (*notebookRestrictions.noExpungeNotebook() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSetDefaultNotebook"),
        (notebookRestrictions.noSetDefaultNotebook()
             ? (*notebookRestrictions.noSetDefaultNotebook() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSetNotebookStack"),
        (notebookRestrictions.noSetNotebookStack()
             ? (*notebookRestrictions.noSetNotebookStack() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noPublishToPublic"),
        (notebookRestrictions.noPublishToPublic()
             ? (*notebookRestrictions.noPublishToPublic() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noPublishToBusinessLibrary"),
        (notebookRestrictions.noPublishToBusinessLibrary()
             ? (*notebookRestrictions.noPublishToBusinessLibrary() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noCreateTags"),
        (notebookRestrictions.noCreateTags()
             ? (*notebookRestrictions.noCreateTags() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noUpdateTags"),
        (notebookRestrictions.noUpdateTags()
             ? (*notebookRestrictions.noUpdateTags() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noExpungeTags"),
        (notebookRestrictions.noExpungeTags()
             ? (*notebookRestrictions.noExpungeTags() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noSetParentTag"),
        (notebookRestrictions.noSetParentTag()
             ? (*notebookRestrictions.noSetParentTag() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noCreateSharedNotebooks"),
        (notebookRestrictions.noCreateSharedNotebooks()
             ? (*notebookRestrictions.noCreateSharedNotebooks() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noShareNotesWithBusiness"),
        (notebookRestrictions.noShareNotesWithBusiness()
             ? (*notebookRestrictions.noShareNotesWithBusiness() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":noRenameNotebook"),
        (notebookRestrictions.noRenameNotebook()
             ? (*notebookRestrictions.noRenameNotebook() ? 1 : 0)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateWhichSharedNotebookRestrictions"),
        notebookRestrictions.updateWhichSharedNotebookRestrictions()
            ? static_cast<int>(
                  *notebookRestrictions.updateWhichSharedNotebookRestrictions())
            : *gNullValue);

    query.bindValue(
        QStringLiteral(":expungeWhichSharedNotebookRestrictions"),
        notebookRestrictions.expungeWhichSharedNotebookRestrictions()
            ? static_cast<int>(*notebookRestrictions
                                    .expungeWhichSharedNotebookRestrictions())
            : *gNullValue);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put notebook restrictions into the local storage database"),
        false);

    return true;
}

bool putSharedNotebook(
    const qevercloud::SharedNotebook & sharedNotebook,
    const int indexInNotebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO SharedNotebooks"
        "(sharedNotebookShareId, sharedNotebookUserId, "
        "sharedNotebookNotebookGuid, sharedNotebookEmail, "
        "sharedNotebookCreationTimestamp, "
        "sharedNotebookModificationTimestamp, "
        "sharedNotebookGlobalId, sharedNotebookUsername, "
        "sharedNotebookPrivilegeLevel, "
        "sharedNotebookRecipientReminderNotifyEmail, "
        "sharedNotebookRecipientReminderNotifyInApp, "
        "sharedNotebookSharerUserId, "
        "sharedNotebookRecipientUsername, "
        "sharedNotebookRecipientUserId, "
        "sharedNotebookRecipientIdentityId, "
        "sharedNotebookAssignmentTimestamp, indexInNotebook) "
        "VALUES(:sharedNotebookShareId, :sharedNotebookUserId, "
        ":sharedNotebookNotebookGuid, :sharedNotebookEmail, "
        ":sharedNotebookCreationTimestamp, "
        ":sharedNotebookModificationTimestamp, "
        ":sharedNotebookGlobalId, :sharedNotebookUsername, "
        ":sharedNotebookPrivilegeLevel, "
        ":sharedNotebookRecipientReminderNotifyEmail, "
        ":sharedNotebookRecipientReminderNotifyInApp, "
        ":sharedNotebookSharerUserId, "
        ":sharedNotebookRecipientUsername, "
        ":sharedNotebookRecipientUserId, "
        ":sharedNotebookRecipientIdentityId, "
        ":sharedNotebookAssignmentTimestamp, :indexInNotebook) ");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put shared notebook into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(
        QStringLiteral(":sharedNotebookShareId"), sharedNotebook.id().value());

    query.bindValue(
        QStringLiteral(":sharedNotebookUserId"),
        (sharedNotebook.userId() ? *sharedNotebook.userId() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookNotebookGuid"),
        (sharedNotebook.notebookGuid() ? *sharedNotebook.notebookGuid()
                                       : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookEmail"),
        (sharedNotebook.email() ? *sharedNotebook.email() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookCreationTimestamp"),
        (sharedNotebook.serviceCreated() ? *sharedNotebook.serviceCreated()
                                         : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookModificationTimestamp"),
        (sharedNotebook.serviceUpdated() ? *sharedNotebook.serviceUpdated()
                                         : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookGlobalId"),
        (sharedNotebook.globalId() ? *sharedNotebook.globalId() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookUsername"),
        (sharedNotebook.username() ? *sharedNotebook.username() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookPrivilegeLevel"),
        (sharedNotebook.privilege()
             ? static_cast<int>(*sharedNotebook.privilege())
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientReminderNotifyEmail"),
        (sharedNotebook.recipientSettings()
             ? (sharedNotebook.recipientSettings()->reminderNotifyEmail()
                    ? (*sharedNotebook.recipientSettings()
                               ->reminderNotifyEmail()
                           ? 1
                           : 0)
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientReminderNotifyInApp"),
        (sharedNotebook.recipientSettings()
             ? (sharedNotebook.recipientSettings()->reminderNotifyInApp()
                    ? (*sharedNotebook.recipientSettings()
                               ->reminderNotifyInApp()
                           ? 1
                           : 0)
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookSharerUserId"),
        (sharedNotebook.sharerUserId() ? *sharedNotebook.sharerUserId()
                                       : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientUsername"),
        (sharedNotebook.recipientUsername()
             ? *sharedNotebook.recipientUsername()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientUserId"),
        (sharedNotebook.recipientUserId() ? *sharedNotebook.recipientUserId()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookRecipientIdentityId"),
        (sharedNotebook.recipientIdentityId()
             ? *sharedNotebook.recipientIdentityId()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookAssignmentTimestamp"),
        (sharedNotebook.serviceAssigned() ? *sharedNotebook.serviceAssigned()
                                          : *gNullValue));

    query.bindValue(QStringLiteral(":indexInNotebook"), indexInNotebook);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put shared notebook into the local storage database"),
        false);

    return true;
}

bool putTag(
    qevercloud::Tag tag, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG("local_storage::sql::utils", "putTag: " << tag);

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't put tag into the local storage database")};

    ErrorString error;
    if (!checkTag(tag, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils", errorDescription << "\nTag: " << tag);
        return false;
    }

    Transaction transaction{database};

    error.clear();
    const auto localId = tagLocalId(tag, database, error);
    if (localId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils", errorDescription << "\nTag: " << tag);
        return false;
    }

    if (tag.localId() != localId) {
        tag.setLocalId(localId);
    }

    if (!complementTagParentInfo(tag, database, errorDescription)) {
        return false;
    }

    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Tags "
        "(localUid, guid, linkedNotebookGuid, updateSequenceNumber, "
        "name, nameLower, parentGuid, parentLocalUid, isDirty, "
        "isLocal, isFavorited) "
        "VALUES(:localUid, :guid, :linkedNotebookGuid, "
        ":updateSequenceNumber, :name, :nameLower, "
        ":parentGuid, :parentLocalUid, :isDirty, :isLocal, :isFavorited)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put tag into the local storage database: "
            "failed to prepare query"),
        false);

    QString tagNameNormalized;
    if (tag.name()) {
        tagNameNormalized = tag.name()->toLower();
        StringUtils stringUtils;
        stringUtils.removeDiacritics(tagNameNormalized);
    }

    query.bindValue(
        QStringLiteral(":localUid"),
        (localId.isEmpty() ? *gNullValue : localId));

    query.bindValue(
        QStringLiteral(":guid"), (tag.guid() ? *tag.guid() : *gNullValue));

    const QString linkedNotebookGuid =
        tag.linkedNotebookGuid().value_or(QString{});

    query.bindValue(
        QStringLiteral(":linkedNotebookGuid"),
        (!linkedNotebookGuid.isEmpty() ? linkedNotebookGuid : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (tag.updateSequenceNum() ? *tag.updateSequenceNum() : *gNullValue));

    query.bindValue(
        QStringLiteral(":name"), (tag.name() ? *tag.name() : *gNullValue));

    query.bindValue(
        QStringLiteral(":nameLower"),
        (tag.name() ? tagNameNormalized : *gNullValue));

    query.bindValue(
        QStringLiteral(":parentGuid"),
        (tag.parentGuid() ? *tag.parentGuid() : *gNullValue));

    query.bindValue(
        QStringLiteral(":parentLocalUid"),
        (!tag.parentTagLocalId().isEmpty() ? tag.parentTagLocalId()
                                           : *gNullValue));

    query.bindValue(
        QStringLiteral(":isDirty"), (tag.isLocallyModified() ? 1 : 0));

    query.bindValue(QStringLiteral(":isLocal"), (tag.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isFavorited"), (tag.isLocallyFavorited() ? 1 : 0));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put tag into the local storage database"),
        false);

    res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put tag into the local storage database, failed to commit"),
        false);

    return true;
}

bool putLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils", "putLinkedNotebook: " << linkedNotebook);

    const ErrorString errorPrefix(QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't put linked notebook into the local storage database"));

    ErrorString error;
    if (!checkLinkedNotebook(linkedNotebook, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils",
            error << "\nLinked notebook: " << linkedNotebook);
        return false;
    }

    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO LinkedNotebooks "
        "(guid, updateSequenceNumber, shareName, "
        "username, shardId, sharedNotebookGlobalId, "
        "uri, noteStoreUrl, webApiUrlPrefix, stack, "
        "businessId, isDirty) VALUES(:guid, "
        ":updateSequenceNumber, :shareName, :username, "
        ":shardId, :sharedNotebookGlobalId, :uri, "
        ":noteStoreUrl, :webApiUrlPrefix, :stack, "
        ":businessId, :isDirty)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put linked notebook into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(
        QStringLiteral(":guid"),
        (linkedNotebook.guid() ? *linkedNotebook.guid() : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (linkedNotebook.updateSequenceNum()
             ? *linkedNotebook.updateSequenceNum()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":shareName"),
        (linkedNotebook.shareName() ? *linkedNotebook.shareName()
                                    : *gNullValue));

    query.bindValue(
        QStringLiteral(":username"),
        (linkedNotebook.username() ? *linkedNotebook.username() : *gNullValue));

    query.bindValue(
        QStringLiteral(":shardId"),
        (linkedNotebook.shardId() ? *linkedNotebook.shardId() : *gNullValue));

    query.bindValue(
        QStringLiteral(":sharedNotebookGlobalId"),
        (linkedNotebook.sharedNotebookGlobalId()
             ? *linkedNotebook.sharedNotebookGlobalId()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":uri"),
        (linkedNotebook.uri() ? *linkedNotebook.uri() : *gNullValue));

    query.bindValue(
        QStringLiteral(":noteStoreUrl"),
        (linkedNotebook.noteStoreUrl() ? *linkedNotebook.noteStoreUrl()
                                       : *gNullValue));

    query.bindValue(
        QStringLiteral(":webApiUrlPrefix"),
        (linkedNotebook.webApiUrlPrefix() ? *linkedNotebook.webApiUrlPrefix()
                                          : *gNullValue));

    query.bindValue(
        QStringLiteral(":stack"),
        (linkedNotebook.stack() ? *linkedNotebook.stack() : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessId"),
        (linkedNotebook.businessId() ? *linkedNotebook.businessId()
                                     : *gNullValue));

    query.bindValue(
        QStringLiteral(":isDirty"),
        (linkedNotebook.isLocallyModified() ? 1 : 0));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put linked notebook into the local storage database"),
        false);

    return true;
}

bool putSavedSearch(
    const qevercloud::SavedSearch & savedSearch, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG("local_storage::sql::utils", "putSavedSearch: " << savedSearch);

    const ErrorString errorPrefix(QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't put saved search into the local storage database"));

    ErrorString error;
    if (!checkSavedSearch(savedSearch, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils",
            error << "\nSaved search: " << savedSearch);
        return false;
    }

    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO SavedSearches"
        "(localUid, guid, name, nameLower, query, format, "
        "updateSequenceNumber, isDirty, isLocal, includeAccount, "
        "includePersonalLinkedNotebooks, "
        "includeBusinessLinkedNotebooks, isFavorited) VALUES("
        ":localUid, :guid, :name, :nameLower, :query, :format, "
        ":updateSequenceNumber, :isDirty, :isLocal, "
        ":includeAccount, :includePersonalLinkedNotebooks, "
        ":includeBusinessLinkedNotebooks, :isFavorited)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put saved search into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":localUid"), savedSearch.localId());

    query.bindValue(
        QStringLiteral(":guid"),
        (savedSearch.guid() ? *savedSearch.guid() : *gNullValue));

    query.bindValue(
        QStringLiteral(":name"),
        (savedSearch.name() ? *savedSearch.name() : *gNullValue));

    query.bindValue(
        QStringLiteral(":nameLower"),
        (savedSearch.name() ? savedSearch.name()->toLower() : *gNullValue));

    query.bindValue(
        QStringLiteral(":query"),
        (savedSearch.query() ? *savedSearch.query() : *gNullValue));

    query.bindValue(
        QStringLiteral(":format"),
        (savedSearch.format() ? static_cast<int>(*savedSearch.format())
                              : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (savedSearch.updateSequenceNum() ? *savedSearch.updateSequenceNum()
                                         : *gNullValue));

    query.bindValue(
        QStringLiteral(":isDirty"), (savedSearch.isLocallyModified() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isLocal"), (savedSearch.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":includeAccount"),
        (savedSearch.scope()
             ? (savedSearch.scope()->includeAccount()
                    ? (*savedSearch.scope()->includeAccount() ? 1 : 0)
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":includePersonalLinkedNotebooks"),
        (savedSearch.scope()
             ? (savedSearch.scope()->includePersonalLinkedNotebooks()
                    ? (*savedSearch.scope()->includePersonalLinkedNotebooks()
                           ? 1
                           : 0)
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":includeBusinessLinkedNotebooks"),
        (savedSearch.scope()
             ? (savedSearch.scope()->includeBusinessLinkedNotebooks()
                    ? (*savedSearch.scope()->includeBusinessLinkedNotebooks()
                           ? 1
                           : 0)
                    : *gNullValue)
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":isFavorited"),
        (savedSearch.isLocallyFavorited() ? 1 : 0));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put saved search into the local storage database"),
        false);

    return true;
}

bool putResource(
    const QDir & localStorageDir, qevercloud::Resource & resource,
    const int indexInNote, QSqlDatabase & database,
    ErrorString & errorDescription,
    const PutResourceBinaryDataOption putResourceBinaryDataOption,
    const TransactionOption transactionOption)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "putResource: " << resource << "\nPut resource binary data option: "
                        << putResourceBinaryDataOption
                        << ", transaction option: " << transactionOption);

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't put resource into the local storage database")};

    ErrorString error;
    if (!checkResource(resource, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils", error << "\nResource: " << resource);
        return false;
    }

    std::optional<Transaction> transaction;
    if (transactionOption == TransactionOption::UseSeparateTransaction) {
        transaction.emplace(database, Transaction::Type::Exclusive);
    }

    error.clear();
    const auto localId = resourceLocalId(resource, database, error);
    if (localId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils",
            errorDescription << "\nResource: " << resource);
        return false;
    }

    if (resource.localId() != localId) {
        resource.setLocalId(localId);
    }

    if (!putCommonResourceData(
            resource, indexInNote,
            (putResourceBinaryDataOption ==
                     PutResourceBinaryDataOption::WithBinaryData
                 ? PutResourceMetadataOption::WithBinaryDataProperties
                 : PutResourceMetadataOption::WithoutBinaryDataProperties),
            database, errorDescription))
    {
        return false;
    }

    if (resource.attributes()) {
        if (!putResourceAttributes(
                localId, *resource.attributes(), database, errorDescription))
        {
            return false;
        }

        if (resource.attributes()->applicationData()) {
            if (!removeResourceAttributesAppDataKeysOnly(
                    localId, database, errorDescription))
            {
                return false;
            }

            if (resource.attributes()->applicationData()->keysOnly() &&
                !resource.attributes()
                     ->applicationData()
                     ->keysOnly()
                     ->isEmpty() &&
                !putResourceAttributesAppDataKeysOnly(
                    localId,
                    *resource.attributes()->applicationData()->keysOnly(),
                    database, errorDescription))
            {
                return false;
            }

            if (!removeResourceAttributesAppDataFullMap(
                    localId, database, errorDescription))
            {
                return false;
            }

            if (resource.attributes()->applicationData()->fullMap() &&
                !resource.attributes()
                     ->applicationData()
                     ->fullMap()
                     ->isEmpty() &&
                !putResourceAttributesAppDataFullMap(
                    localId,
                    *resource.attributes()->applicationData()->fullMap(),
                    database, errorDescription))
            {
                return false;
            }
        }
        else {
            if (!removeResourceAttributesAppDataKeysOnly(
                    localId, database, errorDescription))
            {
                return false;
            }

            if (!removeResourceAttributesAppDataFullMap(
                    localId, database, errorDescription))
            {
                return false;
            }
        }
    }
    else {
        if (!removeResourceAttributes(localId, database, errorDescription)) {
            return false;
        }

        if (!removeResourceAttributesAppDataKeysOnly(
                localId, database, errorDescription))
        {
            return false;
        }

        if (!removeResourceAttributesAppDataFullMap(
                localId, database, errorDescription))
        {
            return false;
        }
    }

    QString resourceDataBodyVersionId;
    QString resourceAlternateDataBodyVersionId;

    if (putResourceBinaryDataOption ==
        PutResourceBinaryDataOption::WithBinaryData) {
        if (resource.data() && resource.data()->body()) {
            resourceDataBodyVersionId = UidGenerator::Generate();
            if (!putResourceDataBodyVersionId(
                    localId, resourceDataBodyVersionId, database,
                    errorDescription))
            {
                return false;
            }

            if (!writeResourceDataBodyToFile(
                    localStorageDir, resource.noteLocalId(), localId,
                    resourceDataBodyVersionId, *resource.data()->body(),
                    errorDescription))
            {
                return false;
            }
        }

        if (resource.alternateData() && resource.alternateData()->body()) {
            resourceAlternateDataBodyVersionId = UidGenerator::Generate();
            if (!putResourceAlternateDataBodyVersionId(
                    localId, resourceAlternateDataBodyVersionId, database,
                    errorDescription))
            {
                return false;
            }

            if (!writeResourceAlternateDataBodyToFile(
                    localStorageDir, resource.noteLocalId(), localId,
                    resourceDataBodyVersionId,
                    *resource.alternateData()->body(), errorDescription))
            {
                return false;
            }
        }
    }

    if (transaction) {
        const bool res = transaction->commit();
        if (!res) {
            if (putResourceBinaryDataOption ==
                PutResourceBinaryDataOption::WithBinaryData) {
                if (resource.data() && resource.data()->body() &&
                    !removeResourceDataBodyFile(
                        localStorageDir, resource.noteLocalId(), localId,
                        resourceDataBodyVersionId, errorDescription))
                {
                    return false;
                }

                if (resource.alternateData() &&
                    resource.alternateData()->body() &&
                    !removeResourceAlternateDataBodyFile(
                        localStorageDir, resource.noteLocalId(), localId,
                        resourceAlternateDataBodyVersionId, errorDescription))
                {
                    return false;
                }
            }

            ENSURE_DB_REQUEST_RETURN(
                res, database, "local_storage::sql::utils",
                QT_TRANSLATE_NOOP(
                    "local_storage::sql::utils",
                    "Cannot put resource into the local storage database, "
                    "failed to commit"),
                false);
        }
        else if (
            putResourceBinaryDataOption ==
            PutResourceBinaryDataOption::WithBinaryData)
        {
            removeStaleResourceDataBodyFiles(
                localStorageDir, resource.noteLocalId(), localId,
                resourceDataBodyVersionId);

            removeStaleResourceAlternateDataBodyFiles(
                localStorageDir, resource.noteLocalId(), localId,
                resourceAlternateDataBodyVersionId);
        }
    }

    return true;
}

bool putCommonResourceData(
    const qevercloud::Resource & resource, const int indexInNote,
    const PutResourceMetadataOption putResourceMetadataOption,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QString queryString;
    {
        QTextStream strm{&queryString};

        strm << "INSERT OR REPLACE INTO Resources (resourceGuid, "
             << "noteGuid, noteLocalUid, mime, "
             << "width, height, recognitionDataBody, recognitionDataSize, "
             << "recognitionDataHash, resourceUpdateSequenceNumber, "
             << "resourceIsDirty, resourceIndexInNote, resourceLocalUid";

        if (putResourceMetadataOption ==
            PutResourceMetadataOption::WithBinaryDataProperties)
        {
            strm << ", dataSize, dataHash, alternateDataSize, "
                 << "alternateDataHash";
        }

        strm << ") VALUES(:resourceGuid, :noteGuid, :noteLocalUid, "
             << ":mime, :width, :height, "
             << ":recognitionDataBody, :recognitionDataSize, "
             << ":recognitionDataHash, :resourceUpdateSequenceNumber, "
             << ":resourceIsDirty, :resourceIndexInNote, :resourceLocalUid";

        if (putResourceMetadataOption ==
            PutResourceMetadataOption::WithBinaryDataProperties)
        {
            strm << ":dataSize, :dataHash, :alternateDataSize, "
                 << ":alternateDataHash";
        }

        strm << ")";
    }

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource metadata into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(
        QStringLiteral(":resourceGuid"),
        (resource.guid() ? *resource.guid() : *gNullValue));

    query.bindValue(
        QStringLiteral(":noteGuid"),
        (resource.noteGuid() ? *resource.noteGuid() : *gNullValue));

    query.bindValue(QStringLiteral(":noteLocalUid"), resource.noteLocalId());

    query.bindValue(
        QStringLiteral(":mime"),
        (resource.mime() ? *resource.mime() : *gNullValue));

    query.bindValue(
        QStringLiteral(":width"),
        (resource.width() ? *resource.width() : *gNullValue));

    query.bindValue(
        QStringLiteral(":height"),
        (resource.height() ? *resource.height() : *gNullValue));

    query.bindValue(
        QStringLiteral(":recognitionDataBody"),
        ((resource.recognition() && resource.recognition()->body())
             ? *resource.recognition()->body()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":recognitionDataSize"),
        ((resource.recognition() && resource.recognition()->size())
             ? *resource.recognition()->size()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":recognitionDataHash"),
        ((resource.recognition() && resource.recognition()->bodyHash())
             ? *resource.recognition()->bodyHash()
             : *gNullValue));

    query.bindValue(
        QStringLiteral(":resourceUpdateSequenceNumber"),
        (resource.updateSequenceNum() ? *resource.updateSequenceNum()
                                      : *gNullValue));

    query.bindValue(
        QStringLiteral(":resourceIsDirty"),
        (resource.isLocallyModified() ? 1 : 0));

    query.bindValue(QStringLiteral(":resourceIndexInNote"), indexInNote);
    query.bindValue(QStringLiteral(":resourceLocalUid"), resource.localId());

    if (putResourceMetadataOption ==
        PutResourceMetadataOption::WithBinaryDataProperties)
    {
        query.bindValue(
            QStringLiteral(":dataSize"),
            ((resource.data() && resource.data()->size())
                 ? *resource.data()->size()
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":dataHash"),
            ((resource.data() && resource.data()->bodyHash())
                 ? *resource.data()->bodyHash()
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":alternateDataSize"),
            ((resource.alternateData() && resource.alternateData()->size())
                 ? *resource.alternateData()->size()
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":alternateDataHash"),
            ((resource.alternateData() && resource.alternateData()->bodyHash())
                 ? *resource.alternateData()->bodyHash()
                 : *gNullValue));
    }

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource metadata into the local storage database"),
        false);

    return true;
}

bool putResourceAttributes(
    const QString & localId, const qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceAttributes"
        "(resourceLocalUid, resourceSourceURL, timestamp, "
        "resourceLatitude, resourceLongitude, resourceAltitude, "
        "cameraMake, cameraModel, clientWillIndex, "
        "fileName, attachment) VALUES(:resourceLocalUid, "
        ":resourceSourceURL, :timestamp, :resourceLatitude, "
        ":resourceLongitude, :resourceAltitude, :cameraMake, "
        ":cameraModel, :clientWillIndex, :fileName, :attachment)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource attributes into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    query.bindValue(
        QStringLiteral(":resourceSourceURL"),
        (attributes.sourceURL() ? *attributes.sourceURL() : *gNullValue));

    query.bindValue(
        QStringLiteral(":timestamp"),
        (attributes.timestamp() ? *attributes.timestamp() : *gNullValue));

    query.bindValue(
        QStringLiteral(":resourceLatitude"),
        (attributes.latitude() ? *attributes.latitude() : *gNullValue));

    query.bindValue(
        QStringLiteral(":resourceLongitude"),
        (attributes.longitude() ? *attributes.longitude() : *gNullValue));

    query.bindValue(
        QStringLiteral(":resourceAltitude"),
        (attributes.altitude() ? *attributes.altitude() : *gNullValue));

    query.bindValue(
        QStringLiteral(":cameraMake"),
        (attributes.cameraMake() ? *attributes.cameraMake() : *gNullValue));

    query.bindValue(
        QStringLiteral(":cameraModel"),
        (attributes.cameraModel() ? *attributes.cameraModel() : *gNullValue));

    query.bindValue(
        QStringLiteral(":clientWillIndex"),
        (attributes.clientWillIndex() ? (*attributes.clientWillIndex() ? 1 : 0)
                                      : *gNullValue));

    query.bindValue(
        QStringLiteral(":fileName"),
        (attributes.fileName() ? *attributes.fileName() : *gNullValue));

    query.bindValue(
        QStringLiteral(":attachment"),
        (attributes.attachment() ? (*attributes.attachment() ? 1 : 0)
                                 : *gNullValue));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource attributes into the local storage database"),
        false);

    return true;
}

bool putResourceAttributesAppDataKeysOnly(
    const QString & localId, const QSet<QString> & keysOnly,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (keysOnly.isEmpty()) {
        return true;
    }

    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceAttributesApplicationDataKeysOnly"
        "(resourceLocalUid, resourceKey) VALUES(:resourceLocalUid, "
        ":resourceKey)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource attributes' application data keys only "
            "into the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    for (const auto & key: keysOnly) {
        query.bindValue(QStringLiteral(":resourceKey"), key);
        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot put resource attributes' application data keys only "
                "into the local storage database"),
            false);
    }

    return true;
}

bool putResourceAttributesAppDataFullMap(
    const QString & localId, const QMap<QString, QString> & fullMap,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (fullMap.isEmpty()) {
        return true;
    }

    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO ResourceAttributesApplicationDataFullMap"
        "(resourceLocalUid, resourceMapKey, resourceValue) "
        "VALUES(:resourceLocalUid, :resourceMapKey, :resourceValue)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot put resource attributes' application data full map "
            "into the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":resourceLocalUid"), localId);

    for (const auto it: qevercloud::toRange(fullMap)) {
        query.bindValue(QStringLiteral(":resourceMapKey"), it.key());
        query.bindValue(QStringLiteral(":resourceValue"), it.value());
        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Cannot put resource attributes' application data full map "
                "into the local storage database"),
            false);
    }

    return true;
}

bool putCommonNoteData(
    const qevercloud::Note & note, const QString & notebookLocalId,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO Notes("
        "localUid, guid, updateSequenceNumber, isDirty, "
        "isLocal, isFavorited, title, titleNormalized, content, "
        "contentLength, contentHash, contentPlainText, "
        "contentListOfWords, contentContainsFinishedToDo, "
        "contentContainsUnfinishedToDo, "
        "contentContainsEncryption, creationTimestamp, "
        "modificationTimestamp, deletionTimestamp, isActive, "
        "hasAttributes, thumbnail, notebookLocalUid, notebookGuid, "
        "subjectDate, latitude, longitude, altitude, author, "
        "source, sourceURL, sourceApplication, shareDate, "
        "reminderOrder, reminderDoneTime, reminderTime, placeName, "
        "contentClass, lastEditedBy, creatorId, lastEditorId, "
        "sharedWithBusiness, conflictSourceNoteGuid, "
        "noteTitleQuality, applicationDataKeysOnly, "
        "applicationDataKeysMap, applicationDataValues, "
        "classificationKeys, classificationValues) VALUES("
        ":localUid, :guid, :updateSequenceNumber, :isDirty, "
        ":isLocal, :isFavorited, :title, :titleNormalized, "
        ":content, :contentLength, :contentHash, "
        ":contentPlainText, :contentListOfWords, "
        ":contentContainsFinishedToDo, "
        ":contentContainsUnfinishedToDo, "
        ":contentContainsEncryption, :creationTimestamp, "
        ":modificationTimestamp, :deletionTimestamp, :isActive, "
        ":hasAttributes, :thumbnail, :notebookLocalUid, "
        ":notebookGuid, :subjectDate, :latitude, :longitude, "
        ":altitude, :author, :source, :sourceURL, "
        ":sourceApplication, :shareDate, :reminderOrder, "
        ":reminderDoneTime, :reminderTime, :placeName, "
        ":contentClass, :lastEditedBy, :creatorId, :lastEditorId, "
        ":sharedWithBusiness, :conflictSourceNoteGuid, "
        ":noteTitleQuality, :applicationDataKeysOnly, "
        ":applicationDataKeysMap, :applicationDataValues, "
        ":classificationKeys, :classificationValues)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't put common note data into the locla storage database: "
            "failed to prepare query"),
        false);

    StringUtils stringUtils;

    const QString titleNormalized = [&]
    {
        if (!note.title()) {
            return QString{};
        }

        auto title = note.title()->toLower();
        stringUtils.removeDiacritics(title);
        return title;
    }();

    query.bindValue(QStringLiteral(":localUid"), note.localId());

    query.bindValue(
        QStringLiteral(":guid"), (note.guid() ? *note.guid() : *gNullValue));

    query.bindValue(
        QStringLiteral(":updateSequenceNumber"),
        (note.updateSequenceNum() ? *note.updateSequenceNum() : *gNullValue));

    query.bindValue(
        QStringLiteral(":isDirty"), (note.isLocallyModified() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isLocal"), (note.isLocalOnly() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":isFavorited"),
        (note.isLocallyFavorited() ? 1 : 0));

    query.bindValue(
        QStringLiteral(":title"),
        (note.title() ? *note.title() : *gNullValue));

    query.bindValue(
        QStringLiteral(":titleNormalized"),
        (titleNormalized.isEmpty() ? *gNullValue : titleNormalized));

    query.bindValue(
        QStringLiteral(":content"),
        (note.content() ? *note.content() : *gNullValue));

    query.bindValue(
        QStringLiteral(":contentLength"),
        (note.contentLength() ? *note.contentLength() : *gNullValue));

    query.bindValue(
        QStringLiteral(":contentHash"),
        (note.contentHash() ? *note.contentHash() : *gNullValue));

    query.bindValue(
        QStringLiteral(":contentContainsFinishedToDo"),
        (note.content() ? static_cast<int>(noteContentContainsCheckedToDo(
                    *note.content()))
            : *gNullValue));

    query.bindValue(
        QStringLiteral(":contentContainsUnfinishedToDo"),
        (note.content() ? static_cast<int>(noteContentContainsUncheckedToDo(
                    *note.content()))
            : *gNullValue));

    query.bindValue(
        QStringLiteral(":contentContainsEncryption"),
        (note.content()
         ? static_cast<int>(
             noteContentContainsEncryptedFragments(*note.content()))
         : *gNullValue));

    if (note.content()) {
        ErrorString error;

        const auto plainTextAndListOfWords =
            noteContentToPlainTextAndListOfWords(*note.content(), &error);

        if (!error.isEmpty()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "can't get note's plain text and list of words"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(
                "local_storage::sql::utils",
                errorDescription << ", note: " << note);
            return false;
        }

        QString listOfWords =
            plainTextAndListOfWords.second.join(QStringLiteral(" "));

        stringUtils.removePunctuation(listOfWords);
        listOfWords = listOfWords.toLower();
        stringUtils.removeDiacritics(listOfWords);

        query.bindValue(
            QStringLiteral(":contentPlainText"),
            (plainTextAndListOfWords.first.isEmpty()
             ? *gNullValue
             : plainTextAndListOfWords.first));

        query.bindValue(
            QStringLiteral(":contentListOfWords"),
            (listOfWords.isEmpty() ? *gNullValue : listOfWords));
    }
    else {
        query.bindValue(QStringLiteral(":contentPlainText"), *gNullValue);
        query.bindValue(QStringLiteral(":contentListOfWords"), *gNullValue);
    }

    query.bindValue(
        QStringLiteral(":creationTimestamp"),
        (note.created() ? *note.created() : *gNullValue));

    query.bindValue(
        QStringLiteral(":modificationTimestamp"),
        (note.updated() ? *note.updated() : *gNullValue));

    query.bindValue(
        QStringLiteral(":deletionTimestamp"),
        (note.deleted() ? *note.deleted() : *gNullValue));

    query.bindValue(
        QStringLiteral(":isActive"),
        (note.active() ? (*note.active() ? 1 : 0) : *gNullValue));

    query.bindValue(
        QStringLiteral(":hasAttributes"), (note.attributes() ? 1 : 0));

    const QByteArray thumbnailData = note.thumbnailData();

    query.bindValue(
        QStringLiteral(":thumbnail"),
        (thumbnailData.isEmpty() ? *gNullValue : thumbnailData));

    query.bindValue(
        QStringLiteral(":notebookLocalUid"),
        (notebookLocalId.isEmpty() ? *gNullValue : notebookLocalId));

    query.bindValue(
        QStringLiteral(":notebookGuid"),
        (note.notebookGuid() ? *note.notebookGuid() : *gNullValue));

    if (note.attributes()) {
        bindNoteAttributes(*note.attributes(), query);
    }
    else {
        bindNullNoteAttributes(query);
    }

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't put common note data into the locla storage database"),
        false);

    return true;
}

bool putNoteRestrictions(
    const QString & noteLocalId,
    const qevercloud::NoteRestrictions & restrictions, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NoteRestrictions "
        "(noteLocalUid, noUpdateNoteTitle, noUpdateNoteContent, "
        "noEmailNote, noShareNote, noShareNotePublicly) "
        "VALUES(:noteLocalUid, :noUpdateNoteTitle, "
        ":noUpdateNoteContent, :noEmailNote, "
        ":noShareNote, :noShareNotePublicly)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't put note restrictions into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

    const auto bindRestriction = [&](const QString & column, auto getter)
    {
        auto value = getter();
        query.bindValue(column, (value ? (*value ? 1 : 0) : *gNullValue));
    };

    bindRestriction(QStringLiteral(":noUpdateNoteTitle"), [&] {
        return restrictions.noUpdateTitle();
    });

    bindRestriction(QStringLiteral(":noUpdateNoteContent"), [&] {
        return restrictions.noUpdateContent();
    });

    bindRestriction(QStringLiteral(":noEmailNote"), [&] {
        return restrictions.noEmail();
    });

    bindRestriction(QStringLiteral(":noShareNote"), [&] {
        return restrictions.noShare();
    });

    bindRestriction(QStringLiteral(":noShareNotePublicly"), [&] {
        return restrictions.noSharePublicly();
    });

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't put note restrictions into the local storage database"),
        false);

    return true;
}

bool putNoteLimits(
    const QString & noteLocalId,
    const qevercloud::NoteLimits & limits, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO NoteLimits "
        "(noteLocalUid, noteResourceCountMax, uploadLimit, "
        "resourceSizeMax, noteSizeMax, uploaded) "
        "VALUES(:noteLocalUid, :noteResourceCountMax, "
        ":uploadLimit, :resourceSizeMax, :noteSizeMax, :uploaded)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't put note limits into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":noteLocalUid"), noteLocalId);

    const auto bindLimit = [&](const QString & column, auto getter)
    {
        auto value = getter();
        query.bindValue(column, value ? *value : *gNullValue);
    };

    bindLimit(QStringLiteral(":noteResourceCountMax"), [&] {
        return limits.noteResourceCountMax();
    });

    bindLimit(QStringLiteral(":uploadLimit"), [&] {
        return limits.uploadLimit();
    });

    bindLimit(QStringLiteral(":resourceSizeMax"), [&] {
        return limits.resourceSizeMax();
    });

    bindLimit(QStringLiteral(":noteSizeMax"), [&] {
        return limits.noteSizeMax();
    });

    bindLimit(QStringLiteral(":uploaded"), [&] {
        return limits.uploaded();
    });

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't put note limits into the local storage database"),
        false);

    return true;
}

bool putSharedNotes(
    const qevercloud::Guid & noteGuid,
    const QList<qevercloud::SharedNote> & sharedNotes, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    if (sharedNotes.isEmpty()) {
        return true;
    }

    static const QString queryString = QStringLiteral(
        "INSERT OR REPLACE INTO SharedNotes ("
        "sharedNoteNoteGuid, "
        "sharedNoteSharerUserId, "
        "sharedNoteRecipientIdentityId, "
        "sharedNoteRecipientContactName, "
        "sharedNoteRecipientContactId, "
        "sharedNoteRecipientContactType, "
        "sharedNoteRecipientContactPhotoUrl, "
        "sharedNoteRecipientContactPhotoLastUpdated, "
        "sharedNoteRecipientContactMessagingPermit, "
        "sharedNoteRecipientContactMessagingPermitExpires, "
        "sharedNoteRecipientUserId, "
        "sharedNoteRecipientDeactivated, "
        "sharedNoteRecipientSameBusiness, "
        "sharedNoteRecipientBlocked, "
        "sharedNoteRecipientUserConnected, "
        "sharedNoteRecipientEventId, "
        "sharedNotePrivilegeLevel, "
        "sharedNoteCreationTimestamp, "
        "sharedNoteModificationTimestamp, "
        "sharedNoteAssignmentTimestamp, "
        "indexInNote) "
        "VALUES("
        ":sharedNoteNoteGuid, "
        ":sharedNoteSharerUserId, "
        ":sharedNoteRecipientIdentityId, "
        ":sharedNoteRecipientContactName, "
        ":sharedNoteRecipientContactId, "
        ":sharedNoteRecipientContactType, "
        ":sharedNoteRecipientContactPhotoUrl, "
        ":sharedNoteRecipientContactPhotoLastUpdated, "
        ":sharedNoteRecipientContactMessagingPermit, "
        ":sharedNoteRecipientContactMessagingPermitExpires, "
        ":sharedNoteRecipientUserId, "
        ":sharedNoteRecipientDeactivated, "
        ":sharedNoteRecipientSameBusiness, "
        ":sharedNoteRecipientBlocked, "
        ":sharedNoteRecipientUserConnected, "
        ":sharedNoteRecipientEventId, "
        ":sharedNotePrivilegeLevel, "
        ":sharedNoteCreationTimestamp, "
        ":sharedNoteModificationTimestamp, "
        ":sharedNoteAssignmentTimestamp, "
        ":indexInNote)");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't put shared note into the local storage database: failed to "
            "prepare query"),
        false);

    int indexInNote = 0;
    for (const auto & sharedNote: qAsConst(sharedNotes))
    {
        query.bindValue(QStringLiteral(":sharedNoteNoteGuid"), noteGuid);

        query.bindValue(
            QStringLiteral(":sharedNoteSharerUserId"),
            (sharedNote.sharerUserID() ? *sharedNote.sharerUserID()
                                       : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientIdentityId"),
            (sharedNote.recipientIdentity()
                 ? sharedNote.recipientIdentity()->id()
                 : *gNullValue));

        const std::optional<qevercloud::Contact> & contact =
            (sharedNote.recipientIdentity()
                 ? sharedNote.recipientIdentity()->contact()
                 : std::nullopt);

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientContactName"),
            (contact ? contact->name().value_or(QString{}) : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientContactId"),
            (contact ? contact->id().value_or(QString{}) : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientContactType"),
            (contact ? (contact->type() ? static_cast<int>(*contact->type())
                                        : *gNullValue)
                     : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientContactPhotoUrl"),
            (contact ? contact->photoUrl().value_or(QString{}) : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientContactPhotoLastUpdated"),
            (contact
                 ? (contact->photoLastUpdated() ? *contact->photoLastUpdated()
                                                : *gNullValue)
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientContactMessagingPermit"),
            (contact ? (contact->messagingPermit() ? *contact->messagingPermit()
                                                   : *gNullValue)
                     : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientContactMessagingPermitExpires"),
            (contact ? (contact->messagingPermitExpires()
                            ? *contact->messagingPermitExpires()
                            : *gNullValue)
                     : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientUserId"),
            (sharedNote.recipientIdentity()
                 ? (sharedNote.recipientIdentity()->userId()
                        ? *sharedNote.recipientIdentity()->userId()
                        : *gNullValue)
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientDeactivated"),
            (sharedNote.recipientIdentity()
                 ? (sharedNote.recipientIdentity()->deactivated()
                        ? static_cast<int>(
                              *sharedNote.recipientIdentity()->deactivated())
                        : *gNullValue)
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientSameBusiness"),
            (sharedNote.recipientIdentity()
                 ? (sharedNote.recipientIdentity()->sameBusiness()
                        ? static_cast<int>(
                              *sharedNote.recipientIdentity()->sameBusiness())
                        : *gNullValue)
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientBlocked"),
            (sharedNote.recipientIdentity()
                 ? (sharedNote.recipientIdentity()->blocked()
                        ? static_cast<int>(
                              *sharedNote.recipientIdentity()->blocked())
                        : *gNullValue)
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientUserConnected"),
            (sharedNote.recipientIdentity()
                 ? (sharedNote.recipientIdentity()->userConnected()
                        ? static_cast<int>(
                              *sharedNote.recipientIdentity()->userConnected())
                        : *gNullValue)
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteRecipientEventId"),
            (sharedNote.recipientIdentity()
                 ? (sharedNote.recipientIdentity()->eventId()
                        ? static_cast<int>(
                              *sharedNote.recipientIdentity()->eventId())
                        : *gNullValue)
                 : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNotePrivilegeLevel"),
            (sharedNote.privilege() ? static_cast<int>(*sharedNote.privilege())
                                    : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteCreationTimestamp"),
            (sharedNote.serviceCreated() ? *sharedNote.serviceCreated()
                                         : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteModificationTimestamp"),
            (sharedNote.serviceUpdated() ? *sharedNote.serviceUpdated()
                                         : *gNullValue));

        query.bindValue(
            QStringLiteral(":sharedNoteAssignmentTimestamp"),
            (sharedNote.serviceAssigned() ? *sharedNote.serviceAssigned()
                                          : *gNullValue));

        query.bindValue(QStringLiteral(":indexInNote"), indexInNote);

        ++indexInNote;

        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::utils",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "Can't put shared note into the local storage database"),
            false);
    }

    return true;
}

bool putNote(const QDir & localStorageDir, qevercloud::Note & note,
    QSqlDatabase & database, ErrorString & errorDescription,
    PutNoteOptions putNoteOptions, TransactionOption transactionOption)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "putNote: "
            << note << ", put resource metadata: "
            << (putNoteOptions.testFlag(PutNoteOption::PutResourceMetadata)
                    ? "yes"
                    : "no")
            << ", put resource binary data: "
            << (putNoteOptions.testFlag(PutNoteOption::PutResourceBinaryData)
                    ? "yes"
                    : "no")
            << ", put tag ids: "
            << (putNoteOptions.testFlag(PutNoteOption::PutTagIds) ? "yes"
                                                                  : "no")
            << ", transaction option = " << transactionOption);

    std::optional<Transaction> transaction;
    if (transactionOption == TransactionOption::UseSeparateTransaction) {
        transaction.emplace(database, Transaction::Type::Exclusive);
    }

    const ErrorString errorPrefix(QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "Can't put note into the local storage database"));

    ErrorString error;
    QString notebookLocalId = utils::notebookLocalId(note, database, error);
    if (notebookLocalId.isEmpty()) {
        errorDescription.base() = errorPrefix.base();
        if (error.isEmpty()) {
            error.setBase(QT_TRANSLATE_NOOP(
                "local_storage::sql::utils",
                "cannot find notebook local id corresponding to note"));
        }
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils",
            errorDescription << ", note: " << note);
        return false;
    }

    const auto composeFullError = [&]
    {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage::sql::utils",
            errorDescription << ", note: " << note);
    };

    error.clear();
    QString notebookGuid = utils::notebookGuid(note, database, error);
    if (notebookGuid.isEmpty() && !error.isEmpty()) {
        composeFullError();
        return false;
    }

    if (notebookGuid.isEmpty()) {
        note.setNotebookGuid(std::nullopt);
    }
    else {
        note.setNotebookGuid(std::move(notebookGuid));
    }

    error.clear();
    if (!checkNote(note, error)) {
        composeFullError();
        return false;
    }

    setNoteIdsToNoteResources(note);

    QString previousNoteGuid;
    if (!note.guid()) {
        error.clear();
        previousNoteGuid =
            noteGuidByLocalId(note.localId(), database, error);
        if (previousNoteGuid.isEmpty() && !error.isEmpty()) {
            composeFullError();
            return false;
        }

        if (!previousNoteGuid.isEmpty() &&
            !clearNoteGuid(putNoteOptions, note, database, error))
        {
            composeFullError();
            return false;
        }
    }

    error.clear();
    if (!putCommonNoteData(note, notebookLocalId, database, error)) {
        composeFullError();
        return false;
    }

    error.clear();
    if (note.restrictions()) {
        if (!putNoteRestrictions(
                note.localId(), *note.restrictions(), database, error)) {
            composeFullError();
            return false;
        }
    }
    else if (!removeNoteRestrictions(note.localId(), database, error)) {
        composeFullError();
        return false;
    }

    error.clear();
    if (note.limits()) {
        if (!putNoteLimits(note.localId(), *note.limits(), database, error)) {
            composeFullError();
            return false;
        }
    }
    else if (!removeNoteLimits(note.localId(), database, error)) {
        composeFullError();
        return false;
    }

    if (!note.guid() && !previousNoteGuid.isEmpty()) {
        if (!removeSharedNotes(previousNoteGuid, database, error)) {
            composeFullError();
            return false;
        }
    }
    else if (note.guid()) {
        if (!removeSharedNotes(*note.guid(), database, error)) {
            composeFullError();
            return false;
        }

        if (note.sharedNotes()) {
            if (!putSharedNotes(
                    *note.guid(), *note.sharedNotes(), database, error)) {
                composeFullError();
                return false;
            }
        }
    }

    // TODO: implement further
    Q_UNUSED(localStorageDir)
    return true;
}

QDebug & operator<<(QDebug & dbg, const PutResourceBinaryDataOption option)
{
    switch (option) {
    case PutResourceBinaryDataOption::WithBinaryData:
        dbg << "With binary data";
        break;
    case PutResourceBinaryDataOption::WithoutBinaryData:
        dbg << "Without binary data";
        break;
    }
    return dbg;
}

} // namespace quentier::local_storage::sql::utils
