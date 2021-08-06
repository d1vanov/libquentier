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
#include "RemoveFromDatabaseUtils.h"
#include "ResourceDataFilesUtils.h"
#include "ResourceUtils.h"
#include "TagUtils.h"

#include "../ErrorHandling.h"
#include "../Transaction.h"
#include "../TypeChecks.h"

#include <quentier/types/ErrorString.h>
#include <quentier/utility/StringUtils.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
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
            << putResourceBinaryDataOption << ", transaction option: "
            << transactionOption);

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
                localId, *resource.attributes(), database, errorDescription)) {
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
        PutResourceMetadataOption::WithBinaryDataProperties) {
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
        (attributes.clientWillIndex()
                ? (*attributes.clientWillIndex() ? 1 : 0)
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

QDebug & operator<<(QDebug & dbg, const PutResourceBinaryDataOption option)
{
    switch (option)
    {
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
