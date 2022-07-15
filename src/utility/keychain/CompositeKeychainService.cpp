/*
 * Copyright 2020-2022 Dmitry Ivanov
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

#include "CompositeKeychainService.h"
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>

#include <QMetaObject>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>
#include <stdexcept>

namespace quentier {

namespace keys {

constexpr const char * unavailablePrimaryKeychainGroup =
    "UnavailablePrimaryKeychainServiceKeyPairs";

constexpr const char * unavailableSecondaryKeychainGroup =
    "UnavailableSecondaryKeychainServiceKeyPairs";

constexpr const char * serviceKeyPair = "ServiceKeyPairs";
constexpr const char * service = "Service";
constexpr const char * key = "Key";

} // namespace keys

CompositeKeychainService::CompositeKeychainService(
    QString name, IKeychainServicePtr primaryKeychain,
    IKeychainServicePtr secondaryKeychain) :
    m_name{std::move(name)}, m_primaryKeychain{std::move(primaryKeychain)},
    m_secondaryKeychain{std::move(secondaryKeychain)}
{
    if (m_name.isEmpty()) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "utility::keychain::CompositeKeychainService",
            "CompositeKeychainService ctor: name is empty")}};
    }

    if (Q_UNLIKELY(!m_primaryKeychain)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "utility::keychain::CompositeKeychainService",
            "CompositeKeychainService ctor: primary keychain is null")}};
    }

    if (Q_UNLIKELY(!m_secondaryKeychain)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "utility::keychain::CompositeKeychainService",
            "CompositeKeychainService ctor: secondary keychain is null")}};
    }
}

CompositeKeychainService::~CompositeKeychainService() noexcept = default;

QFuture<void> CompositeKeychainService::writePassword(
    QString service, QString key, QString password)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    QFuture<void> primaryKeychainFuture =
        m_primaryKeychain->writePassword(service, key, password);

    QFuture<void> secondaryKeychainFuture =
        m_secondaryKeychain->writePassword(service, key, password);

    QFuture<void> allFuture = threading::whenAll(
        QList<QFuture<void>>{} << primaryKeychainFuture
                               << secondaryKeychainFuture);

    auto allThenFuture =
        threading::then(std::move(allFuture), [promise] { promise->finish(); });

    threading::onFailed(
        std::move(allThenFuture),
        [promise, primaryKeychainFuture = std::move(primaryKeychainFuture),
         secondaryKeychainFuture = std::move(secondaryKeychainFuture),
         selfWeak = weak_from_this(), service = std::move(service),
         key = std::move(key)](const QException & e) mutable {
            QString allFutureError = [&] {
                QString str;
                QTextStream strm{&str};
                strm << e.what();
                return str;
            }();

            auto primaryKeychainThenFuture = threading::then(
                std::move(primaryKeychainFuture),
                [promise, selfWeak, service, key,
                 allFutureError = std::move(allFutureError)] {
                    // Writing to primary keychain succeeded but allThenFuture
                    // is in a failed state. That means writing to the secondary
                    // keychain has failed.
                    if (const auto self = selfWeak.lock()) {
                        self->unmarkServiceKeyPairAsUnavailableInPrimaryKeychain(
                            service, key);

                        self->markServiceKeyPairAsUnavailableInSecondaryKeychain(
                            service, key);

                        QNWARNING(
                            "utility::keychain::CompositeKeychainService",
                            "Failed to write password to secondary keychain: "
                                << "name = " << self->m_name << ", service = "
                                << service << ", key = " << key
                                << ", error: " << allFutureError);
                    }

                    promise->finish();
                });

            threading::onFailed(
                std::move(primaryKeychainThenFuture),
                [promise, selfWeak, service = std::move(service),
                 key = std::move(key),
                 secondaryKeychainFuture](const QException & e) mutable {
                    auto primaryKeychainError =
                        std::shared_ptr<QException>(e.clone());

                    // Writing to primary keychain has failed. Need to figure
                    // out the state of writing to the secondary keychain.
                    if (const auto self = selfWeak.lock()) {
                        self->markServiceKeyPairAsUnavailableInPrimaryKeychain(
                            service, key);

                        QNWARNING(
                            "utility::keychain::CompositeKeychainService",
                            "Failed to write password to primary keychain: "
                                << "name = " << self->m_name << ", service = "
                                << service << ", key = " << key
                                << ", error: " << e.what());
                    }

                    auto secondaryKeychainThenFuture = threading::then(
                        std::move(secondaryKeychainFuture),
                        [promise, selfWeak, service, key] {
                            // Writing to secondary keychain succeeded.
                            if (const auto self = selfWeak.lock()) {
                                self->unmarkServiceKeyPairAsUnavailableInSecondaryKeychain(
                                    service, key);
                            }

                            promise->finish();
                        });

                    threading::onFailed(
                        std::move(secondaryKeychainThenFuture),
                        [promise, selfWeak, service = std::move(service),
                         key = std::move(key),
                         primaryKeychainError](const QException & e) {
                            // Writing to secondary keychain failed as well
                            if (const auto self = selfWeak.lock()) {
                                self->markServiceKeyPairAsUnavailableInSecondaryKeychain(
                                    service, key);

                                QNWARNING(
                                    "utility::keychain::"
                                    "CompositeKeychainService",
                                    "Failed to write password to primary "
                                    "keychain: "
                                        << "name = " << self->m_name
                                        << ", service = " << service
                                        << ", key = " << key
                                        << ", error: " << e.what());
                            }

                            promise->setException(*primaryKeychainError);
                            promise->finish();
                        });
                });
        });

    return future;
}

QFuture<QString> CompositeKeychainService::readPassword(
    QString service, QString key) const
{
    auto promise = std::make_shared<QPromise<QString>>();
    auto future = promise->future();

    promise->start();

    QFuture<QString> primaryKeychainFuture = [&] {
        if (isServiceKeyPairAvailableInPrimaryKeychain(service, key)) {
            return m_primaryKeychain->readPassword(service, key);
        }

        return threading::makeExceptionalFuture<QString>(
            Exception{ErrorCode::EntryNotFound});
    }();

    auto primaryKeychainThenFuture = threading::then(
        std::move(primaryKeychainFuture), [promise](QString password) {
            promise->addResult(std::move(password));
            promise->finish();
        });

    threading::onFailed(
        std::move(primaryKeychainThenFuture),
        [promise, selfWeak = weak_from_this(), service = std::move(service),
         key = std::move(key)](const QException & e) mutable {
            if (const auto self = selfWeak.lock()) {
                if (!utility::utils::isNoEntryError(e)) {
                    QNWARNING(
                        "utility::keychain::CompositeKeychainService",
                        "Failed to read password from the primary keychain: "
                            << "name = " << self->m_name
                            << ", service = " << service << ", key = " << key
                            << ", error: " << e.what());
                }

                QFuture<QString> secondaryKeychainFuture = [&] {
                    if (self->isServiceKeyPairAvailableInSecondaryKeychain(
                            service, key)) {
                        return self->m_secondaryKeychain->readPassword(
                            service, key);
                    }

                    return threading::makeExceptionalFuture<QString>(
                        Exception{ErrorCode::EntryNotFound});
                }();

                auto secondaryKeychainThenFuture = threading::then(
                    std::move(secondaryKeychainFuture),
                    [promise](QString password) {
                        promise->addResult(std::move(password));
                        promise->finish();
                    });

                threading::onFailed(
                    std::move(secondaryKeychainThenFuture),
                    [promise, selfWeak, service = std::move(service),
                     key = std::move(key)](const QException & e) {
                        if (const auto self = selfWeak.lock()) {
                            if (!utility::utils::isNoEntryError(e)) {
                                QNWARNING(
                                    "utility::keychain::"
                                    "CompositeKeychainService",
                                    "Failed to read password from the "
                                    "secondary keychain: "
                                        << "name = " << self->m_name
                                        << ", service = " << service
                                        << ", key = " << key
                                        << ", error: " << e.what());
                            }
                        }

                        promise->setException(e);
                        promise->finish();
                    });

                return;
            }

            promise->setException(e);
            promise->finish();
        });

    return future;
}

QFuture<void> CompositeKeychainService::deletePassword(
    QString service, QString key)
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    QFuture<void> primaryKeychainFuture =
        m_primaryKeychain->deletePassword(service, key);

    QFuture<void> secondaryKeychainFuture =
        m_secondaryKeychain->deletePassword(service, key);

    QFuture<void> allFuture = threading::whenAll(
        QList<QFuture<void>>{} << primaryKeychainFuture
                               << secondaryKeychainFuture);

    auto allThenFuture =
        threading::then(std::move(allFuture), [promise] { promise->finish(); });

    threading::onFailed(
        std::move(allThenFuture),
        [promise, selfWeak = weak_from_this(), service = std::move(service),
         key = std::move(key), primaryKeychainFuture,
         secondaryKeychainFuture](const QException & e) mutable {
            QString allFutureError = [&] {
                QString str;
                QTextStream strm{&str};
                strm << e.what();
                return str;
            }();

            auto primaryKeychainThenFuture = threading::then(
                std::move(primaryKeychainFuture),
                [promise, selfWeak, service, key,
                 allFutureError = std::move(allFutureError)] {
                    // Deleting from primary keychain succeeded but
                    // allThenFuture is in a failed state. That means deleting
                    // from the secondary keychain has failed.
                    if (const auto self = selfWeak.lock()) {
                        self->markServiceKeyPairAsUnavailableInSecondaryKeychain(
                            service, key);

                        QNWARNING(
                            "utility::keychain::CompositeKeychainService",
                            "Failed to delete password from secondary "
                            "keychain: "
                                << "name = " << self->m_name << ", service = "
                                << service << ", key = " << key
                                << ", error: " << allFutureError);
                    }

                    promise->finish();
                });

            threading::onFailed(
                std::move(primaryKeychainThenFuture),
                [promise, selfWeak, service = std::move(service),
                 key = std::move(key),
                 secondaryKeychainFuture = std::move(secondaryKeychainFuture)](
                    const QException & e) mutable {
                    // Deleting from primary keychain has failed
                    if (const auto self = selfWeak.lock()) {
                        self->markServiceKeyPairAsUnavailableInPrimaryKeychain(
                            service, key);

                        QNWARNING(
                            "utility::keychain::CompositeKeychainService",
                            "Failed to delete password from primary keychain: "
                                << "name = " << self->m_name << ", service = "
                                << service << ", key = " << key
                                << ", error: " << e.what());
                    }

                    auto secondaryKeychainThenFuture = threading::then(
                        std::move(secondaryKeychainFuture), [promise] {
                            // Deleting from secondary keychain succeeded
                            promise->finish();
                        });

                    threading::onFailed(
                        std::move(secondaryKeychainThenFuture),
                        [promise, selfWeak, service = std::move(service),
                         key = std::move(key)](const QException & e) {
                            // Deleting from secondary keychain failed
                            if (const auto self = selfWeak.lock()) {
                                self->markServiceKeyPairAsUnavailableInSecondaryKeychain(
                                    service, key);

                                QNWARNING(
                                    "utility::keychain::"
                                    "CompositeKeychainService",
                                    "Failed to delete password from secondary "
                                    "keychain: "
                                        << "name = " << self->m_name
                                        << ", service = " << service
                                        << ", key = " << key
                                        << ", error: " << e.what());
                            }

                            promise->finish();
                        });
                });
        });

    return future;
}

void CompositeKeychainService::markServiceKeyPairAsUnavailableInPrimaryKeychain(
    const QString & service, const QString & key)
{
    if (!isServiceKeyPairAvailableInPrimaryKeychain(service, key)) {
        return;
    }

    m_serviceKeysUnavailableInPrimaryKeychain[service].insert(key);

    persistUnavailableServiceKeyPairs(
        keys::unavailablePrimaryKeychainGroup, service, key);
}

void CompositeKeychainService::
    unmarkServiceKeyPairAsUnavailableInPrimaryKeychain(
        const QString & service, const QString & key)
{
    if (isServiceKeyPairAvailableInPrimaryKeychain(service, key)) {
        return;
    }

    m_serviceKeysUnavailableInPrimaryKeychain[service].remove(key);

    persistUnavailableServiceKeyPairs(
        keys::unavailablePrimaryKeychainGroup, service, key);
}

bool CompositeKeychainService::isServiceKeyPairAvailableInPrimaryKeychain(
    const QString & service, const QString & key) const // NOLINT
{
    checkAndInitializeServiceKeysCaches();

    const auto serviceIt =
        m_serviceKeysUnavailableInPrimaryKeychain.find(service);

    if (serviceIt == m_serviceKeysUnavailableInPrimaryKeychain.end()) {
        return true;
    }

    return !serviceIt.value().contains(key);
}

void CompositeKeychainService::
    markServiceKeyPairAsUnavailableInSecondaryKeychain(
        const QString & service, const QString & key)
{
    if (!isServiceKeyPairAvailableInSecondaryKeychain(service, key)) {
        return;
    }

    m_serviceKeysUnavailableInSecondaryKeychain[service].insert(key);

    persistUnavailableServiceKeyPairs(
        keys::unavailableSecondaryKeychainGroup, service, key);
}

void CompositeKeychainService::
    unmarkServiceKeyPairAsUnavailableInSecondaryKeychain(
        const QString & service, const QString & key)
{
    if (isServiceKeyPairAvailableInSecondaryKeychain(service, key)) {
        return;
    }

    m_serviceKeysUnavailableInSecondaryKeychain[service].remove(key);

    persistUnavailableServiceKeyPairs(
        keys::unavailableSecondaryKeychainGroup, service, key);
}

bool CompositeKeychainService::isServiceKeyPairAvailableInSecondaryKeychain(
    const QString & service, const QString & key) const // NOLINT
{
    checkAndInitializeServiceKeysCaches();

    const auto serviceIt =
        m_serviceKeysUnavailableInSecondaryKeychain.find(service);

    if (serviceIt == m_serviceKeysUnavailableInSecondaryKeychain.end()) {
        return true;
    }

    return !serviceIt.value().contains(key);
}

void CompositeKeychainService::persistUnavailableServiceKeyPairs(
    const char * groupName, const QString & service, const QString & key)
{
    ApplicationSettings settings{m_name};
    settings.beginGroup(groupName);

    bool foundItem = false;
    const int size = settings.beginReadArray(keys::serviceKeyPair);
    QVector<std::pair<QString, QString>> serviceAndKeyPairs;
    serviceAndKeyPairs.reserve(size + 1);
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);

        QString serviceItem = settings.value(keys::service).toString();
        QString keyItem = settings.value(keys::service).toString();

        if (serviceItem == service && keyItem == key) {
            foundItem = true;
            break;
        }

        serviceAndKeyPairs.push_back(
            std::make_pair(std::move(serviceItem), std::move(keyItem)));
    }

    settings.endArray();

    if (foundItem) {
        settings.endGroup();
        return;
    }

    serviceAndKeyPairs.push_back(std::make_pair(service, key));

    settings.beginWriteArray(keys::serviceKeyPair, size + 1);
    for (int i = 0; i < size + 1; ++i) {
        settings.setArrayIndex(i);
        settings.setValue(keys::service, serviceAndKeyPairs[i].first);
        settings.setValue(keys::key, serviceAndKeyPairs[i].second);
    }
    settings.endArray();

    settings.endGroup();
}

void CompositeKeychainService::checkAndInitializeServiceKeysCaches() const
{
    if (m_serviceKeysCachesInitialized) {
        return;
    }

    m_serviceKeysUnavailableInPrimaryKeychain =
        readServiceKeyPairsUnavailableInPrimaryKeychain();

    m_serviceKeysUnavailableInSecondaryKeychain =
        readServiceKeyPairsUnavailableInSecondaryKeychain();

    m_serviceKeysCachesInitialized = true;
}

CompositeKeychainService::ServiceKeyPairsCache
CompositeKeychainService::readServiceKeyPairsUnavailableInPrimaryKeychain()
    const
{
    return readServiceKeyPairsUnavailableInKeychainImpl(
        keys::unavailablePrimaryKeychainGroup);
}

CompositeKeychainService::ServiceKeyPairsCache
CompositeKeychainService::readServiceKeyPairsUnavailableInSecondaryKeychain()
    const
{
    return readServiceKeyPairsUnavailableInKeychainImpl(
        keys::unavailableSecondaryKeychainGroup);
}

CompositeKeychainService::ServiceKeyPairsCache
CompositeKeychainService::readServiceKeyPairsUnavailableInKeychainImpl(
    const char * groupName) const
{
    ApplicationSettings settings{m_name};
    settings.beginGroup(groupName);

    ServiceKeyPairsCache cache;
    const int size = settings.beginReadArray(keys::serviceKeyPair);
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);

        const QString service = settings.value(keys::service).toString();
        const QString key = settings.value(keys::service).toString();

        cache[service].insert(key);
    }

    settings.endArray();
    settings.endGroup();

    return cache;
}

} // namespace quentier
