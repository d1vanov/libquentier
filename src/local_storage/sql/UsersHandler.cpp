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

#include "ConnectionPool.h"
#include "ErrorHandling.h"
#include "Tasks.h"
#include "Transaction.h"
#include "TypeChecks.h"
#include "UsersHandler.h"

#include "utils/FillFromSqlRecordUtils.h"

#include <quentier/exception/DatabaseRequestException.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <utility/Threading.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QGlobalStatic>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QThreadPool>

#include <optional>
#include <type_traits>

namespace quentier::local_storage::sql {

namespace {

Q_GLOBAL_STATIC(QVariant, gNullValue)

} // namespace

UsersHandler::UsersHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    QThreadPtr writerThread) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool},
    m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "UsersHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "UsersHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "UsersHandler ctor: writer thread is null")}};
    }
}

QFuture<quint32> UsersHandler::userCount() const
{
    return makeReadTask<quint32>(
        makeTaskContext(),
        weak_from_this(),
        [](const UsersHandler & handler, QSqlDatabase & database,
           ErrorString & errorDescription)
        {
            return handler.userCountImpl(database, errorDescription);
        });
}

QFuture<void> UsersHandler::putUser(qevercloud::User user)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [user = std::move(user)]
        (UsersHandler & handler, QSqlDatabase & database,
         ErrorString & errorDescription)
        {
            return handler.putUserImpl(user, database, errorDescription);
        });
}

QFuture<qevercloud::User> UsersHandler::findUserById(
    qevercloud::UserID userId) const
{
    return makeReadTask<qevercloud::User>(
        makeTaskContext(),
        weak_from_this(),
        [userId](const UsersHandler & handler, QSqlDatabase & database,
                 ErrorString & errorDescription)
        {
            return handler.findUserByIdImpl(userId, database, errorDescription);
        });
}

QFuture<void> UsersHandler::expungeUserById(qevercloud::UserID userId)
{
    return makeWriteTask<void>(
        makeTaskContext(),
        weak_from_this(),
        [userId](UsersHandler & handler, QSqlDatabase & database,
                 ErrorString & errorDescription)
        {
            return handler.expungeUserByIdImpl(
                userId, database, errorDescription);
        });
}

std::optional<quint32> UsersHandler::userCountImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    QSqlQuery query{database};
    const bool res = query.exec(
        QStringLiteral("SELECT COUNT(id) FROM Users WHERE "
                       "userDeletionTimestamp IS NULL"));
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot count users in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::UsersHandler",
            "Found no users in the local storage database");
        return 0;
    }

    bool conversionResult = false;
    const int count = query.value(0).toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Cannot count users in the local storage database: failed to "
                "convert user count to int"));
        QNWARNING("local_storage:sql", errorDescription);
        return std::nullopt;
    }

    return count;
}

bool UsersHandler::putUserImpl(
    const qevercloud::User & user, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::UsersHandler",
        "UsersHandler::putUserImpl: " << user);

    const ErrorString errorPrefix(
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Can't put user into the local storage database"));

    ErrorString error;
    if (!checkUser(user, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING(
            "local_storage:sql:UsersHandler",
            error << "\nUser: " << user);
        return false;
    }

    Transaction transaction{database};

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
                *user.accountLimits(), userId, database, errorDescription)) {
            return false;
        }
    }
    else if (!removeAccountLimits(userId, database, errorDescription)) {
        return false;
    }

    if (user.businessUserInfo()) {
        if (!putBusinessUserInfo(
                *user.businessUserInfo(), userId, database, errorDescription)) {
            return false;
        }
    }
    else if (!removeBusinessUserInfo(userId, database, errorDescription)) {
        return false;
    }

    const bool res = transaction.commit();
    ENSURE_DB_REQUEST_RETURN(
        res, database, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user into the local storage database, failed to "
            "commit"),
        false);

    return true;
}

bool UsersHandler::putCommonUserData(
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put common user data into the local storage database: "
            "failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        QStringLiteral(":username"),
        (user.username() ? *user.username() : *gNullValue));

    query.bindValue(
        QStringLiteral(":email"),
        (user.email() ? *user.email() : *gNullValue));

    query.bindValue(
        QStringLiteral(":name"), (user.name() ? *user.name() : *gNullValue));

    query.bindValue(
        QStringLiteral(":timezone"),
        (user.timezone() ? *user.timezone() : *gNullValue));

    query.bindValue(
        QStringLiteral(":privilege"),
        (user.privilege() ? static_cast<int>(*user.privilege())
                            : *gNullValue));

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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put common user data into the local storage database"),
        false);

    return true;
}

bool UsersHandler::putUserAttributes(
    const qevercloud::UserAttributes & userAttributes,
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
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
        (userAttributes.defaultLatitude()
         ? *userAttributes.defaultLatitude()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":defaultLongitude"),
        (userAttributes.defaultLongitude()
         ? *userAttributes.defaultLongitude()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":incomingEmailAddress"),
        (userAttributes.incomingEmailAddress()
         ? *userAttributes.incomingEmailAddress()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":comments"),
        (userAttributes.comments()
         ? *userAttributes.comments()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":dateAgreedToTermsOfService"),
        (userAttributes.dateAgreedToTermsOfService()
         ? *userAttributes.dateAgreedToTermsOfService()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":maxReferrals"),
        (userAttributes.maxReferrals()
        ? *userAttributes.maxReferrals()
        : *gNullValue));

    query.bindValue(
        QStringLiteral(":referralCount"),
        (userAttributes.referralCount()
         ? *userAttributes.referralCount()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":refererCode"),
        (userAttributes.refererCode()
         ? *userAttributes.refererCode()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":sentEmailDate"),
        (userAttributes.sentEmailDate()
         ? *userAttributes.sentEmailDate()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":sentEmailCount"),
        (userAttributes.sentEmailCount()
         ? *userAttributes.sentEmailCount()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":dailyEmailLimit"),
        (userAttributes.dailyEmailLimit()
         ? *userAttributes.dailyEmailLimit()
         : *gNullValue));

    query.bindValue(
        QStringLiteral(":emailOptOutDate"),
        (userAttributes.emailOptOutDate()
         ? *userAttributes.emailOptOutDate()
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
        (userAttributes.preferredCountry()
        ? *userAttributes.preferredCountry()
        : *gNullValue));

    query.bindValue(
        QStringLiteral(":twitterUserName"),
        (userAttributes.twitterUserName()
        ? *userAttributes.twitterUserName()
        : *gNullValue));

    query.bindValue(
        QStringLiteral(":twitterId"),
        (userAttributes.twitterId()
        ? *userAttributes.twitterId()
        : *gNullValue));

    query.bindValue(
        QStringLiteral(":groupName"),
        (userAttributes.groupName()
        ? *userAttributes.groupName()
        : *gNullValue));

    query.bindValue(
        QStringLiteral(":recognitionLanguage"),
        (userAttributes.recognitionLanguage()
        ? *userAttributes.recognitionLanguage()
        : *gNullValue));

    query.bindValue(
        QStringLiteral(":referralProof"),
        (userAttributes.referralProof()
        ? *userAttributes.referralProof()
        : *gNullValue));

    query.bindValue(
        QStringLiteral(":businessAddress"),
        (userAttributes.businessAddress()
        ? *userAttributes.businessAddress()
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
        (userAttributes.passwordUpdated()
         ? *userAttributes.passwordUpdated()
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user attributes into the local storage database"),
        false);

    return true;
}

bool UsersHandler::putUserAttributesViewedPromotions(
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user attributes' viewer promotions into the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    for (const auto & viewedPromotion: *viewedPromotions) {
        query.bindValue(QStringLiteral(":promotion"), viewedPromotion);
        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::UsersHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Cannot put user attributes' viewer promotions into the local "
                "storage database"),
            false);
    }

    return true;
}

bool UsersHandler::putUserAttributesRecentMailedAddresses(
    const QString & userId,
    const std::optional<QStringList> & recentMailedAddresses,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    if (!removeUserAttributesRecentMailedAddresses(
            userId, database, errorDescription)) {
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user attributes' recent mailed addresses into "
            "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    for (const auto & recentMailedAddress: *recentMailedAddresses) {
        query.bindValue(QStringLiteral(":address"), recentMailedAddress);
        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::UsersHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Cannot put user attributes' recent mailed addresses into "
                "the local storage database"),
            false);
    }

    return true;
}

bool UsersHandler::removeUserAttributesViewedPromotions(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM UserAttributesViewedPromotions WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user' viewed promotions from "
            "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user' viewed promotions from "
            "the local storage database"),
        false);

    return true;
}

bool UsersHandler::removeUserAttributesRecentMailedAddresses(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM UserAttributesRecentMailedAddresses WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user' recent mailed addresses from "
            "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user' recent mailed addresses from "
            "the local storage database"),
        false);

    return true;
}

bool UsersHandler::removeUserAttributes(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    if (!removeUserAttributesViewedPromotions(
            userId, database, errorDescription)) {
        return false;
    }

    if (!removeUserAttributesRecentMailedAddresses(
            userId, database, errorDescription))
    {
        return false;
    }

    // Clear entries from UserAttributes table
    {
        static const QString queryString = QStringLiteral(
            "DELETE FROM UserAttributes WHERE id=:id");

        QSqlQuery query{database};
        bool res = query.prepare(queryString);
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::UsersHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Cannot remove user attributes from "
                "the local storage database: failed to prepare query"),
            false);

        query.bindValue(QStringLiteral(":id"), userId);

        res = query.exec();
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::UsersHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Cannot remove user attributes from the local storage "
                "database"),
            false);
    }

    return true;
}

bool UsersHandler::putAccounting(
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user's accounting data into the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        ":uploadLimitEnd",
        accounting.uploadLimitEnd()
        ? *accounting.uploadLimitEnd()
        : *gNullValue);

    query.bindValue(
        ":uploadLimitNextMonth",
        accounting.uploadLimitNextMonth()
        ? *accounting.uploadLimitNextMonth()
        : *gNullValue);

    query.bindValue(
        ":premiumServiceStatus",
        accounting.premiumServiceStatus()
        ? static_cast<int>(*accounting.premiumServiceStatus())
        : *gNullValue);

    query.bindValue(
        ":premiumOrderNumber",
        accounting.premiumOrderNumber()
        ? *accounting.premiumOrderNumber()
        : *gNullValue);

    query.bindValue(
        ":premiumCommerceService",
        accounting.premiumCommerceService()
        ? *accounting.premiumCommerceService()
        : *gNullValue);

    query.bindValue(
        ":premiumServiceStart",
        accounting.premiumServiceStart()
        ? *accounting.premiumServiceStart()
        : *gNullValue);

    query.bindValue(
        ":premiumServiceSKU",
        accounting.premiumServiceSKU()
        ? *accounting.premiumServiceSKU()
        : *gNullValue);

    query.bindValue(
        ":lastSuccessfulCharge",
        accounting.lastSuccessfulCharge()
        ? *accounting.lastSuccessfulCharge()
        : *gNullValue);

    query.bindValue(
        ":lastFailedCharge",
        accounting.lastFailedCharge()
        ? *accounting.lastFailedCharge()
        : *gNullValue);

    query.bindValue(
        ":lastFailedChargeReason",
        accounting.lastFailedChargeReason()
        ? *accounting.lastFailedChargeReason()
        : *gNullValue);

    query.bindValue(
        ":nextPaymentDue",
        accounting.nextPaymentDue()
        ? *accounting.nextPaymentDue()
        : *gNullValue);

    query.bindValue(
        ":premiumLockUntil",
        accounting.premiumLockUntil()
        ? *accounting.premiumLockUntil()
        : *gNullValue);

    query.bindValue(
        ":updated",
        accounting.updated()
        ? *accounting.updated()
        : *gNullValue);

    query.bindValue(
        ":premiumSubscriptionNumber",
        accounting.premiumSubscriptionNumber()
        ? *accounting.premiumSubscriptionNumber()
        : *gNullValue);

    query.bindValue(
        ":lastRequestedCharge",
        accounting.lastRequestedCharge()
        ? *accounting.lastRequestedCharge()
        : *gNullValue);

    query.bindValue(
        ":currency",
        accounting.currency()
        ? *accounting.currency()
        : *gNullValue);

    query.bindValue(
        ":unitPrice",
        accounting.unitPrice()
        ? *accounting.unitPrice()
        : *gNullValue);

    query.bindValue(
        ":unitDiscount",
        accounting.unitDiscount()
        ? *accounting.unitDiscount()
        : *gNullValue);

    query.bindValue(
        ":nextChargeDate",
        accounting.nextChargeDate()
        ? *accounting.nextChargeDate()
        : *gNullValue);

    query.bindValue(
        ":availablePoints",
        accounting.availablePoints()
        ? *accounting.availablePoints()
        : *gNullValue);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user's accounting data into the local storage "
            "database"),
        false);

    return true;
}

bool UsersHandler::removeAccounting(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM Accounting WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user' accounting data from "
            "the local storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user's accounting data from the local storage "
            "database"),
        false);

    return true;
}

bool UsersHandler::putAccountLimits(
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user's account limits into the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    query.bindValue(
        QStringLiteral(":userMailLimitDaily"),
        accountLimits.userMailLimitDaily()
        ? *accountLimits.userMailLimitDaily()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":noteSizeMax"),
        accountLimits.noteSizeMax()
        ? *accountLimits.noteSizeMax()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":resourceSizeMax"),
        accountLimits.resourceSizeMax()
        ? *accountLimits.resourceSizeMax()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":userLinkedNotebookMax"),
        accountLimits.userLinkedNotebookMax()
        ? *accountLimits.userLinkedNotebookMax()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":uploadLimit"),
        accountLimits.uploadLimit()
        ? *accountLimits.uploadLimit()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":userNoteCountMax"),
        accountLimits.userNoteCountMax()
        ? *accountLimits.userNoteCountMax()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":userNotebookCountMax"),
        accountLimits.userNotebookCountMax()
        ? *accountLimits.userNotebookCountMax()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":userTagCountMax"),
        accountLimits.userTagCountMax()
        ? *accountLimits.userTagCountMax()
        : *gNullValue);

    query.bindValue(
        QStringLiteral(":noteTagCountMax"),
        accountLimits.noteTagCountMax()
        ? *accountLimits.noteTagCountMax()
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put user's account limits into the local storage database"),
        false);

    return true;
}

bool UsersHandler::removeAccountLimits(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM AccountLimits WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user' account limits from the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user's account limits from the local storage "
            "database"),
        false);

    return true;
}

bool UsersHandler::putBusinessUserInfo(
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
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
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot put business user info into the local storage database"),
        false);

    return true;
}

bool UsersHandler::removeBusinessUserInfo(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    static const QString queryString = QStringLiteral(
        "DELETE FROM BusinessUserInfo WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove business user info from the local storage "
            "database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove business user info from the local storage "
            "database"),
        false);

    return true;
}

std::optional<qevercloud::User> UsersHandler::findUserByIdImpl(
    const qevercloud::UserID userId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage::sql::UsersHandler",
        "UsersHandler::findUserByIdImpl: user id = " << userId);

    static const QString queryString = QStringLiteral(
        "SELECT * FROM Users LEFT OUTER JOIN UserAttributes "
        "ON Users.id = UserAttributes.id "
        "LEFT OUTER JOIN Accounting ON Users.id = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON Users.id = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON Users.id = BusinessUserInfo.id "
        "WHERE Users.id = :id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user in the local storage database: failed to prepare "
            "query"),
        std::nullopt);

    const auto id = QString::number(userId);
    query.bindValue(":id", id);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user in the local storage database"),
        std::nullopt);

    if (!query.next()) {
        return std::nullopt;
    }

    const auto record = query.record();
    qevercloud::User user;
    user.setId(userId);
    ErrorString error;
    if (!utils::fillUserFromSqlRecord(record, user, error)) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Failed to find user by id in the local storage database"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::UsersHandler", errorDescription);
        return std::nullopt;
    }

    if (user.attributes())
    {
        if (!findUserAttributesViewedPromotionsById(
                id, database, *user.mutableAttributes(), errorDescription)) {
            return std::nullopt;
        }

        if (!findUserAttributesRecentMailedAddressesById(
                id, database, *user.mutableAttributes(), errorDescription)) {
            return std::nullopt;
        }
    }

    return user;
}

bool UsersHandler::findUserAttributesViewedPromotionsById(
    const QString & userId, QSqlDatabase & database,
    qevercloud::UserAttributes & userAttributes,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT * FROM UserAttributesViewedPromotions WHERE id = :id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' viewed promotions in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' viewed promotions in the local "
            "storage database"),
        false);

    while (query.next())
    {
        const auto record = query.record();
        const int promotionIndex = record.indexOf(QStringLiteral("promotion"));
        if (promotionIndex < 0) {
            continue;
        }

        const QVariant value = record.value(promotionIndex);
        if (value.isNull()) {
            continue;
        }

        if (!userAttributes.viewedPromotions()) {
            userAttributes.setViewedPromotions(QStringList{});
        }

        *userAttributes.mutableViewedPromotions() << value.toString();
    }

    return true;
}

bool UsersHandler::findUserAttributesRecentMailedAddressesById(
    const QString & userId, QSqlDatabase & database,
    qevercloud::UserAttributes & userAttributes,
    ErrorString & errorDescription) const
{
    static const QString queryString = QStringLiteral(
        "SELECT * FROM UserAttributesRecentMailedAddresses WHERE id = :id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' recent mailed addresses in the local "
            "storage database: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), userId);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot find user attributes' recent mailed addresses in the local "
            "storage database"),
        false);

    while (query.next())
    {
        const auto record = query.record();
        const int addressIndex = record.indexOf(QStringLiteral("address"));
        if (addressIndex < 0) {
            continue;
        }

        const QVariant value = record.value(addressIndex);
        if (value.isNull()) {
            continue;
        }

        if (!userAttributes.recentMailedAddresses()) {
            userAttributes.setRecentMailedAddresses(QStringList{});
        }

        *userAttributes.mutableRecentMailedAddresses() << value.toString();
    }

    return true;
}

bool UsersHandler::expungeUserByIdImpl(
    qevercloud::UserID userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::UsersHandler",
        "UsersHandler::expungeUserByIdImpl: user id = " << userId);

    static const QString queryString =
        QStringLiteral("DELETE FROM Users WHERE id=:id");

    QSqlQuery query{database};
    bool res = query.prepare(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot expunge user from the local storage database: failed to "
            "prepare query"),
        false);

    query.bindValue(QStringLiteral(":id"), QString::number(userId));

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot expunge user from the local storage database: failed to "
            "prepare query"),
        false);

    return true;
}

TaskContext UsersHandler::makeTaskContext() const
{
    return TaskContext{
        m_threadPool, m_writerThread, m_connectionPool,
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "UsersHandler is already destroyed")},
        ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::NotebooksHandler",
            "Request has been calceled")}};
}

} // namespace quentier::local_storage::sql
