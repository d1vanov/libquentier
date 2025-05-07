/*
 * Copyright 2020-2025 Dmitry Ivanov
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

#include "MigratingKeychainService.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>
#include <stdexcept>

namespace quentier::utility::keychain {

MigratingKeychainService::MigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain) :
    m_sourceKeychain(std::move(sourceKeychain)),
    m_sinkKeychain(std::move(sinkKeychain))
{
    if (Q_UNLIKELY(!m_sourceKeychain)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "utility::keychain::MigratingKeychainService",
            "MigratingKeychainService ctor: source keychain is null")}};
    }

    if (Q_UNLIKELY(!m_sinkKeychain)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "utility::keychain::MigratingKeychainService",
            "MigratingKeychainService ctor: sink keychain is null")}};
    }
}

MigratingKeychainService::~MigratingKeychainService() noexcept = default;

QFuture<void> MigratingKeychainService::writePassword(
    QString service, QString key, QString password)
{
    return m_sinkKeychain->writePassword(
        std::move(service), std::move(key), std::move(password));
}

QFuture<QString> MigratingKeychainService::readPassword(
    QString service, QString key) const
{
    auto promise = std::make_shared<QPromise<QString>>();
    auto future = promise->future();

    promise->start();

    auto sinkKeychainFuture = m_sinkKeychain->readPassword(service, key);

    auto sinkKeychainThenFuture = threading::then(
        std::move(sinkKeychainFuture), [promise](QString password) {
            promise->addResult(std::move(password));
            promise->finish();
        });

    threading::onFailed(
        std::move(sinkKeychainThenFuture),
        [promise, sourceKeychain = m_sourceKeychain,
         sinkKeychain = m_sinkKeychain, service = std::move(service),
         key = std::move(key)](const QException & e) mutable {
            bool isNoEntryError = false;
            try {
                e.raise();
            }
            catch (const IKeychainService::Exception & exc) {
                isNoEntryError =
                    (exc.errorCode() ==
                     IKeychainService::ErrorCode::EntryNotFound);
            }
            catch (...) {
            }

            if (!isNoEntryError) {
                promise->setException(e);
                promise->finish();
                return;
            }

            // Otherwise fallback to source keychain
            auto sourceKeychainFuture =
                sourceKeychain->readPassword(service, key);

            threading::thenOrFailed(
                std::move(sourceKeychainFuture), promise,
                [promise, sinkKeychain, sourceKeychain,
                 service = std::move(service),
                 key = std::move(key)](QString password) {
                    auto writeToSinkFuture =
                        sinkKeychain->writePassword(service, key, password);

                    auto thenFuture = threading::then(
                        std::move(writeToSinkFuture),
                        [promise, password, service, key,
                         sourceKeychain]() mutable {
                            auto deleteFromSourceFuture =
                                sourceKeychain->deletePassword(service, key);

                            auto thenFuture = threading::then(
                                std::move(deleteFromSourceFuture),
                                [promise, password]() mutable {
                                    promise->addResult(std::move(password));
                                    promise->finish();
                                });

                            threading::onFailed(
                                std::move(thenFuture),
                                [promise, password = std::move(password),
                                 service = std::move(service),
                                 key = std::move(key)](
                                    const QException & e) mutable {
                                    // Failed to delete from source keychain,
                                    // will just return the migrated password
                                    QNWARNING(
                                        "utility::keychain::"
                                        "MigratingKeychainService",
                                        "Failed to delete password from source "
                                        "keychain: service = "
                                            << service << ", key = " << key
                                            << ": " << e.what());

                                    promise->addResult(std::move(password));
                                    promise->finish();
                                });
                        });

                    threading::onFailed(
                        std::move(thenFuture),
                        [promise, password, service,
                         key](const QException & e) mutable {
                            // Failed to write to sink keychain, will just
                            // return the result from the source keychain
                            QNWARNING(
                                "utility::keychain::MigratingKeychainService",
                                "Failed to write password from source keychain "
                                "to sink keychain: service = "
                                    << service << ", key = " << key << ": "
                                    << e.what());

                            promise->addResult(std::move(password));
                            promise->finish();
                        });
                });
        });

    return future;
}

QFuture<void> MigratingKeychainService::deletePassword(
    QString service, QString key)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    auto sinkKeychainFuture = m_sinkKeychain->deletePassword(service, key);
    auto sourceKeychainFuture = m_sourceKeychain->deletePassword(service, key);

    QFuture<void> allFuture = threading::whenAll(
        QList<QFuture<void>>{} << sinkKeychainFuture << sourceKeychainFuture);

    auto allThenFuture =
        threading::then(std::move(allFuture), [promise] { promise->finish(); });

    threading::onFailed(
        std::move(allThenFuture),
        [promise, sinkKeychainFuture, sourceKeychainFuture,
         service = std::move(service),
         key = std::move(key)](const QException & e) mutable {
            std::shared_ptr<QException> allFutureError{e.clone()};

            auto sinkKeychainThenFuture = threading::then(
                std::move(sinkKeychainFuture),
                [promise, allFutureError = std::move(allFutureError)] {
                    // Deleting from sink keychain succeeded but allThenFuture
                    // is in a failed state. It means deleting from the source
                    // keychain has failed.
                    if (isNoEntryError(*allFutureError)) {
                        // EntryNotFound when deleting is factually equivalent
                        // to no error as the net result is the same -
                        // the source keychain doesn't have the key which
                        // deletion was attempted.
                        promise->finish();
                        return;
                    }

                    promise->setException(*allFutureError);
                    promise->finish();
                });

            threading::onFailed(
                std::move(sinkKeychainThenFuture),
                [promise,
                 sourceKeychainFuture = std::move(sourceKeychainFuture)](
                    const QException & e) mutable {
                    // Deleting from the sink keychain has failed.
                    if (!isNoEntryError(e)) {
                        promise->setException(e);
                        promise->finish();
                        return;
                    }

                    // EntryNotFound when deleting is factually equivalent
                    // to no error as the net result is the same -
                    // the sink keychain doesn't have the key which
                    // deletion was attempted.
                    // So will see what is the result of deleting from the
                    // source keychain.
                    auto sourceKeychainThenFuture = threading::then(
                        std::move(sourceKeychainFuture), [promise] {
                            // Deleting from source keychain succeeded, so
                            // considering the overall deletion successful.
                            promise->finish();
                        });

                    threading::onFailed(
                        std::move(sourceKeychainThenFuture),
                        [promise](const QException & e) {
                            if (isNoEntryError(e)) {
                                // EntryNotFound when deleting is factually
                                // equivalent to no error as the net result is
                                // the same - the source keychain doesn't have
                                // the key which deletion was attempted.
                                // So considering the overall deletion
                                // successful.
                                promise->finish();
                                return;
                            }

                            promise->setException(e);
                            promise->finish();
                        });
                });
        });

    return future;
}

} // namespace quentier::utility::keychain
