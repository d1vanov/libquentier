/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include "AccountLimitsProvider.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/DateTime.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/services/IUserStore.h>

#include <QDateTime>
#include <QMutexLocker>
#include <QTextStream>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

namespace quentier::synchronization {

namespace {

////////////////////////////////////////////////////////////////////////////////

const QString gSynchronizationPersistence =
    QStringLiteral("SynchronizationPersistence");

const QString gAccountLimitsGroup = QStringLiteral("AccountLimits");
const QString gAccountLimitsLastSyncTime = QStringLiteral("lastSyncTime");

const QString gAccountLimitsUserMailLimitDaily =
    QStringLiteral("userMailLimitDaily");

const QString gAccountLimitsNoteSizeMax = QStringLiteral("noteSizeMax");

const QString gAccountLimitsResourceSizeMax = QStringLiteral("resourceSizeMax");

const QString gAccountLimitsUserLinkedNotebookMax =
    QStringLiteral("userLinkedNotebookMax");

const QString gAccountLimitsUploadLimit = QStringLiteral("uploadLimit");

const QString gAccountLimitsUserNoteCountMax =
    QStringLiteral("userNoteCountMax");

const QString gAccountLimitsUserNotebookCountMax =
    QStringLiteral("userNotebookCountMax");

const QString gAccountLimitsUserTagCountMax = QStringLiteral("userTagCountMax");

const QString gAccountLimitsUserSavedSearchCountMax =
    QStringLiteral("userSavedSearchCountMax");

const QString gAccountLimitsNoteResourceCountMax =
    QStringLiteral("noteResourceCountMax");

const QString gAccountLimitsNoteTagCountMax = QStringLiteral("noteTagCountMax");

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] QString appSettingsAccountLimitsGroupName(
    const qevercloud::ServiceLevel serviceLevel)
{
    QString res;
    QTextStream strm{&res};
    strm << gAccountLimitsGroup << "/" << serviceLevel;
    return res;
}

} // namespace

AccountLimitsProvider::AccountLimitsProvider(
    Account account,
    qevercloud::IUserStorePtr userStore) :
    m_account{std::move(account)},
    m_userStore{std::move(userStore)}
{
    if (Q_UNLIKELY(m_account.isEmpty())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountLimitsProvider ctor: account is empty")}};
    }

    if (Q_UNLIKELY(m_account.type() != Account::Type::Evernote)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountLimitsProvider ctor: account is not an Evernote one")}};
    }

    if (Q_UNLIKELY(!m_userStore)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "AccountLimitsProvider ctor: user store is null")}};
    }
}

QFuture<qevercloud::AccountLimits> AccountLimitsProvider::accountLimits(
    const qevercloud::ServiceLevel serviceLevel,
    qevercloud::IRequestContextPtr ctx)
{
    {
        const QMutexLocker locker{&m_accountLimitsCacheMutex};
        const auto it = m_accountLimitsCache.constFind(serviceLevel);
        if (it != m_accountLimitsCache.constEnd()) {
            return threading::makeReadyFuture(it.value());
        }

        auto accountLimits = readPersistentAccountLimits(serviceLevel);
        if (accountLimits) {
            m_accountLimitsCache[serviceLevel] = *accountLimits;
            return threading::makeReadyFuture(std::move(*accountLimits));
        }
    }

    auto promise = std::make_shared<QPromise<qevercloud::AccountLimits>>();
    auto future = promise->future();
    promise->start();

    auto selfWeak = weak_from_this();

    auto accountLimitsFuture = m_userStore->getAccountLimitsAsync(
        serviceLevel, std::move(ctx));

    threading::thenOrFailed(
        std::move(accountLimitsFuture), promise,
        [promise, selfWeak, serviceLevel](
            qevercloud::AccountLimits accountLimits) {
            if (const auto self = selfWeak.lock()) {
                const QMutexLocker locker{
                    &self->m_accountLimitsCacheMutex};

                // First let's check whether we are too late and
                // another call managed to bring the account limits
                // to the local cache faster
                const auto it =
                    self->m_accountLimitsCache.constFind(
                        serviceLevel);
                if (it != self->m_accountLimitsCache.constEnd()) {
                    promise->addResult(it.value());
                    promise->finish();
                    return;
                }

                self->m_accountLimitsCache[serviceLevel] =
                    accountLimits;
                self->writePersistentAccountLimits(
                    serviceLevel, accountLimits);
            }

            promise->addResult(std::move(accountLimits));
            promise->finish();
        });

    return future;
}

std::optional<qevercloud::AccountLimits>
    AccountLimitsProvider::readPersistentAccountLimits(
        qevercloud::ServiceLevel serviceLevel) const
{
    ApplicationSettings appSettings{m_account, gSynchronizationPersistence};

    const QString groupName = appSettingsAccountLimitsGroupName(serviceLevel);
    appSettings.beginGroup(groupName);
    const ApplicationSettings::GroupCloser groupCloser{appSettings};

    const auto lastSyncTimestampValue =
        appSettings.value(gAccountLimitsLastSyncTime);

    if (lastSyncTimestampValue.isNull()) {
        QNDEBUG(
            "synchronization::AccountLimitsProvider",
            "No stored last sync timestamp for account limits");
        return std::nullopt;
    }

    const std::optional<qint64> lastSyncTimestamp =
        [&]() -> std::optional<qint64> {
        bool conversionResult = false;
        const qint64 res = lastSyncTimestampValue.toLongLong(&conversionResult);
        if (Q_UNLIKELY(!conversionResult)) {
            QNWARNING(
                "synchronization::AccountLimitsProvider",
                "Failed to convert stored last sync timestamp for account "
                    << "limits to qint64: " << lastSyncTimestampValue);
            return std::nullopt;
        }

        QNDEBUG(
            "synchronization::AccountLimitsProvider",
            "Last account limits sync time: "
                << printableDateTimeFromTimestamp(res));

        return res;
    }();

    if (!lastSyncTimestamp) {
        return std::nullopt;
    }

    const auto currentTimestamp = QDateTime::currentMSecsSinceEpoch();
    if (Q_UNLIKELY(currentTimestamp < *lastSyncTimestamp)) {
        QNWARNING(
            "synchronization::AccountLimitsProvider",
            "Current time "
                << printableDateTimeFromTimestamp(currentTimestamp)
                << " is less than last sync time for account limits: "
                << printableDateTimeFromTimestamp(*lastSyncTimestamp));
        return std::nullopt;
    }

    constexpr qint64 thirty_days_in_msec = 2592000000;
    const auto diff = currentTimestamp - *lastSyncTimestamp;
    if (diff > thirty_days_in_msec) {
        QNINFO(
            "synchronization::AccountLimitsProvider",
            "Last sync time for account limits is too old: "
                << printableDateTimeFromTimestamp(*lastSyncTimestamp)
                << ", current time is "
                << printableDateTimeFromTimestamp(currentTimestamp));
        return std::nullopt;
    }

    qevercloud::AccountLimits accountLimits;

    const auto readNumericAccountLimitValue =
        [&](auto(QVariant::*variantReadFunc),
            auto(qevercloud::AccountLimits::*setterFunc), const QString & key) {
            const auto variantValue = appSettings.value(key);
            if (variantValue.isNull()) {
                return;
            }

            bool conversionResult = false;
            const auto value =
                (variantValue.*variantReadFunc)(&conversionResult);
            if (!conversionResult) {
                QNWARNING(
                    "synchronization::AccountLimitsProvider",
                    "Failed to convert " << key << " account limit to "
                                         << "numeric value: " << variantValue);
                return;
            }

            (accountLimits.*setterFunc)(value);
        };

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setUserMailLimitDaily,
        gAccountLimitsUserMailLimitDaily);

    readNumericAccountLimitValue(
        &QVariant::toLongLong, &qevercloud::AccountLimits::setNoteSizeMax,
        gAccountLimitsNoteSizeMax);

    readNumericAccountLimitValue(
        &QVariant::toLongLong, &qevercloud::AccountLimits::setResourceSizeMax,
        gAccountLimitsResourceSizeMax);

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setUserLinkedNotebookMax,
        gAccountLimitsUserLinkedNotebookMax);

    readNumericAccountLimitValue(
        &QVariant::toLongLong, &qevercloud::AccountLimits::setUploadLimit,
        gAccountLimitsUploadLimit);

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setUserNoteCountMax,
        gAccountLimitsUserNoteCountMax);

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setUserNotebookCountMax,
        gAccountLimitsUserNotebookCountMax);

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setUserTagCountMax,
        gAccountLimitsUserTagCountMax);

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setUserSavedSearchesMax,
        gAccountLimitsUserSavedSearchCountMax);

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setNoteResourceCountMax,
        gAccountLimitsNoteResourceCountMax);

    readNumericAccountLimitValue(
        &QVariant::toInt, &qevercloud::AccountLimits::setNoteTagCountMax,
        gAccountLimitsNoteTagCountMax);

    return accountLimits;
}

void AccountLimitsProvider::writePersistentAccountLimits(
    qevercloud::ServiceLevel serviceLevel,
    const qevercloud::AccountLimits & accountLimits)
{
    const auto now = QDateTime::currentMSecsSinceEpoch();

    ApplicationSettings appSettings{m_account, gSynchronizationPersistence};

    const QString groupName = appSettingsAccountLimitsGroupName(serviceLevel);
    appSettings.beginGroup(groupName);
    const ApplicationSettings::GroupCloser groupCloser{appSettings};

    appSettings.setValue(gAccountLimitsLastSyncTime, now);

    const auto writeAccountLimitValue =
        [&](auto(qevercloud::AccountLimits::*readerFunc), const QString & key) {
            const auto & value = (accountLimits.*readerFunc)();
            if (value) {
                appSettings.setValue(key, *value);
            }
            else {
                appSettings.remove(key);
            }
        };

    writeAccountLimitValue(
        &qevercloud::AccountLimits::userMailLimitDaily,
        gAccountLimitsUserMailLimitDaily);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::noteSizeMax, gAccountLimitsNoteSizeMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::resourceSizeMax,
        gAccountLimitsResourceSizeMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::userLinkedNotebookMax,
        gAccountLimitsUserLinkedNotebookMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::uploadLimit, gAccountLimitsUploadLimit);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::userNoteCountMax,
        gAccountLimitsUserNoteCountMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::userNotebookCountMax,
        gAccountLimitsUserNotebookCountMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::userTagCountMax,
        gAccountLimitsUserTagCountMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::userSavedSearchesMax,
        gAccountLimitsUserSavedSearchCountMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::noteResourceCountMax,
        gAccountLimitsNoteResourceCountMax);

    writeAccountLimitValue(
        &qevercloud::AccountLimits::noteTagCountMax,
        gAccountLimitsNoteTagCountMax);
}

} // namespace quentier::synchronization
