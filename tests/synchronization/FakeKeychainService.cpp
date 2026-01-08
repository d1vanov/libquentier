/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "FakeKeychainService.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Runnable.h>

#include <QMutexLocker>
#include <QThreadPool>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <optional>

namespace quentier::synchronization::tests {

FakeKeychainService::FakeKeychainService(
    threading::QThreadPoolPtr threadPool, PasswordByKeyByService passwords) :
    m_threadPool{
        threadPool ? std::move(threadPool) : threading::globalThreadPool()},
    m_servicesKeysAndPasswords{std::move(passwords)}
{
    Q_ASSERT(m_threadPool);
}

FakeKeychainService::PasswordByKeyByService FakeKeychainService::passwords()
    const
{
    const QMutexLocker locker{&m_mutex};
    return m_servicesKeysAndPasswords;
}

void FakeKeychainService::setPasswords(PasswordByKeyByService passwords)
{
    const QMutexLocker locker{&m_mutex};
    m_servicesKeysAndPasswords = std::move(passwords);
}

void FakeKeychainService::clear()
{
    const QMutexLocker locker{&m_mutex};
    m_servicesKeysAndPasswords.clear();
}

QFuture<void> FakeKeychainService::writePassword(
    QString service, QString key, QString password)
{
    QNDEBUG(
        "tests::synchronization::FakeKeychainService",
        "FakeKeychainService::writePassword: service = "
            << service << ", key = " << key << ", password = " << password);

    {
        const QMutexLocker locker{&m_mutex};
        m_servicesKeysAndPasswords[service][key] = std::move(password);
    }

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();
    promise->start();

    std::unique_ptr<QRunnable> runnable{threading::createFunctionRunnable(
        [promise = std::move(promise)] { promise->finish(); })};

    m_threadPool->start(runnable.release());
    return future;
}

QFuture<QString> FakeKeychainService::readPassword(
    QString service, QString key) const
{
    std::optional<QString> result;
    {
        const QMutexLocker locker{&m_mutex};

        const auto serviceIt = m_servicesKeysAndPasswords.constFind(service);
        if (serviceIt != m_servicesKeysAndPasswords.constEnd()) {
            const auto & passwordsByKey = serviceIt.value();

            const auto it = passwordsByKey.constFind(key);
            if (it != passwordsByKey.constEnd()) {
                result = it.value();
            }
        }
    }

    if (result) {
        QNDEBUG(
            "tests::synchronization::FakeKeychainService",
            "FakeKeychainService::readPassword: service = "
                << service << ", key = " << key
                << ": found result: " << *result);
    }
    else {
        QNDEBUG(
            "tests::synchronization::FakeKeychainService",
            "FakeKeychainService::readPassword: service = "
                << service << ", key = " << key << ": no result found");
    }

    auto promise = std::make_shared<QPromise<QString>>();
    auto future = promise->future();
    promise->start();

    std::unique_ptr<QRunnable> runnable{threading::createFunctionRunnable(
        [promise = std::move(promise), result = std::move(result)]() mutable {
            if (result) {
                promise->addResult(std::move(*result));
            }
            else {
                promise->setException(IKeychainService::Exception{
                    IKeychainService::ErrorCode::EntryNotFound});
            }

            promise->finish();
        })};

    m_threadPool->start(runnable.release());
    return future;
}

QFuture<void> FakeKeychainService::deletePassword(QString service, QString key)
{
    QNDEBUG(
        "tests::synchronization::FakeKeychainService",
        "FakeKeychainService::deletePassword: service = " << service
                                                          << ", key = " << key);

    {
        const QMutexLocker locker{&m_mutex};

        const auto serviceIt = m_servicesKeysAndPasswords.find(service);
        if (serviceIt != m_servicesKeysAndPasswords.end()) {
            auto & passwordsByKey = serviceIt.value();

            const auto it = passwordsByKey.find(key);
            if (it != passwordsByKey.end()) {
                passwordsByKey.erase(it);
            }
        }
    }

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();
    promise->start();

    std::unique_ptr<QRunnable> runnable{threading::createFunctionRunnable(
        [promise = std::move(promise)] { promise->finish(); })};

    m_threadPool->start(runnable.release());
    return future;
}

} // namespace quentier::synchronization::tests
