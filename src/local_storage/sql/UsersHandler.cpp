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
#include "Transaction.h"
#include "TypeChecks.h"
#include "UsersHandler.h"

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
#include <QSqlQuery>
#include <QThreadPool>

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
    auto promise = std::make_shared<QPromise<quint32>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise),
         self_weak = weak_from_this()]
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::UsersHandler",
                         "UsersHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const auto userCount = self->userCountImpl(
                 databaseConnection, errorDescription);

             if (!userCount) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
                 promise->finish();
                 return;
             }

             promise->addResult(*userCount);
             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<void> UsersHandler::putUser(qevercloud::User user)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    utility::postToThread(
        m_writerThread.get(),
        [promise = std::move(promise),
         self_weak = weak_from_this(),
         user = std::move(user)]
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::UsersHandler",
                         "UsersHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const bool res = self->putUserImpl(
                 user, databaseConnection, errorDescription);

             if (!res) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    return future;
}

QFuture<std::optional<qevercloud::User>> UsersHandler::findUserById(
    qevercloud::UserID userId) const
{
    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::User>>>();

    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this(), userId]
         {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::UsersHandler",
                         "UsersHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             auto user = self->findUserByIdImpl(
                 userId, databaseConnection, errorDescription);

             if (user) {
                 promise->addResult(std::move(*user));
             }
             else if (!errorDescription.isEmpty()) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
         });

    m_threadPool->start(runnable);
    return future;
}

QFuture<void> UsersHandler::expungeUserById(qevercloud::UserID userId)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    utility::postToThread(
        m_writerThread.get(),
        [promise = std::move(promise), self_weak = weak_from_this(), userId]
        {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::UsersHandler",
                         "UsersHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             const bool res = self->expungeUserByIdImpl(
                 userId, databaseConnection, errorDescription);

             if (!res) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
        });

    return future;
}

QFuture<QList<qevercloud::User>> UsersHandler::listUsers() const
{
    auto promise = std::make_shared<QPromise<QList<qevercloud::User>>>();
    auto future = promise->future();

    promise->start();

    auto * runnable = utility::createFunctionRunnable(
        [promise = std::move(promise), self_weak = weak_from_this()]
        {
             const auto self = self_weak.lock();
             if (!self) {
                 promise->setException(RuntimeError(ErrorString{
                     QT_TRANSLATE_NOOP(
                         "local_storage::sql::UsersHandler",
                         "UsersHandler is already destroyed")}));
                 promise->finish();
                 return;
             }

             auto databaseConnection = self->m_connectionPool->database();

             ErrorString errorDescription;
             auto users = self->listUsersImpl(
                 databaseConnection, errorDescription);

             if (!users.isEmpty()) {
                 promise->addResult(std::move(users));
             }
             else if (!errorDescription.isEmpty()) {
                 promise->setException(
                     DatabaseRequestException{errorDescription});
             }

             promise->finish();
        });

    m_threadPool->start(runnable);
    return future;
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
            "local_storage:sql",
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

    // TODO: implement further
    Q_UNUSED(user)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
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

bool UsersHandler::removeUserAttributes(
    const QString & userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // Clear entries from UserAttributesViewedPromotions table
    {
        static const QString queryString =
            QString::fromUtf8(
                "DELETE FROM UserAttributesViewedPromotions WHERE id=%1")
                .arg(userId);

        QSqlQuery query{database};
        const bool res = query.exec(queryString);
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::UsersHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Cannot remove user' viewed promotions from "
                "the local storage database"),
            false);
    }

    // Clear entries from UserAttributesRecentMailedAddresses table
    {
        static const QString queryString =
            QString::fromUtf8(
                "DELETE FROM UserAttributesRecentMailedAddresses WHERE "
                "id=%1")
                .arg(userId);

        QSqlQuery query{database};
        const bool res = query.exec(queryString);
        ENSURE_DB_REQUEST_RETURN(
            res, query, "local_storage::sql::UsersHandler",
            QT_TRANSLATE_NOOP(
                "local_storage::sql::UsersHandler",
                "Cannot remove user' recent mailed addresses from "
                "the local storage database"),
            false);
    }

    // Clear entries from UserAttributes table
    {
        static const QString queryString =
            QString::fromUtf8("DELETE FROM UserAttributes WHERE id=%1")
                .arg(userId);

        QSqlQuery query{database};
        const bool res = query.exec(queryString);
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
    static const QString queryString =
        QString::fromUtf8("DELETE FROM Accounting WHERE id=%1").arg(userId);

    QSqlQuery query{database};
    const bool res = query.exec(queryString);
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
    static const QString queryString =
        QString::fromUtf8("DELETE FROM AccountLimits WHERE id=%1")
        .arg(userId);

    QSqlQuery query{database};
    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::UsersHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::UsersHandler",
            "Cannot remove user's account limits from the local storage "
            "database"),
        false);

    return true;
}

std::optional<qevercloud::User> UsersHandler::findUserByIdImpl(
    const qevercloud::UserID userId, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(userId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

bool UsersHandler::expungeUserByIdImpl(
    qevercloud::UserID userId, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(userId)
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return true;
}

QList<qevercloud::User> UsersHandler::listUsersImpl(
    QSqlDatabase & database, ErrorString & errorDescription) const
{
    // TODO: implement
    Q_UNUSED(database)
    Q_UNUSED(errorDescription)
    return {};
}

} // namespace quentier::local_storage::sql
