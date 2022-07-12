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

namespace quentier {

MigratingKeychainService::MigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain,
    QObject * parent) :
    IKeychainService(parent),
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

    createConnections();
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
        QList<QFuture<void>>{} << sinkKeychainFuture
                               << sourceKeychainFuture);

    auto allThenFuture =
        threading::then(std::move(allFuture), [promise] { promise->finish(); });

    threading::onFailed(
        std::move(allThenFuture),
        [promise, sinkKeychainFuture, sourceKeychainFuture,
         service = std::move(service), key = std::move(key)](
            const QException & e) mutable {
            std::shared_ptr<QException> allFutureError{e.clone()};

            auto sinkKeychainThenFuture = threading::then(
                std::move(sinkKeychainFuture),
                [promise, allFutureError = std::move(allFutureError)] {
                    // Deleting from sink keychain succeeded but allThenFuture
                    // is in a failed state. It means deleting from the source
                    // keychain has failed.
                    if (utility::utils::isNoEntryError(*allFutureError)) {
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
                    if (!utility::utils::isNoEntryError(e)) {
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
                        std::move(sourceKeychainFuture),
                        [promise] {
                            // Deleting from source keychain succeeded, so
                            // considering the overall deletion successful.
                            promise->finish();
                        });

                    threading::onFailed(
                        std::move(sourceKeychainThenFuture),
                        [promise](const QException & e)
                        {
                            if (utility::utils::isNoEntryError(e)) {
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

QUuid MigratingKeychainService::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    const auto requestId =
        m_sinkKeychain->startWritePasswordJob(service, key, password);

    m_sinkKeychainWriteRequestIds.insert(requestId);
    return requestId;
}

QUuid MigratingKeychainService::startReadPasswordJob(
    const QString & service, const QString & key)
{
    const auto requestId = m_sinkKeychain->startReadPasswordJob(service, key);

    m_sinkKeychainReadRequestIdsToServiceAndKey[requestId] =
        std::make_pair(service, key);

    return requestId;
}

QUuid MigratingKeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    const auto sinkKeychainRequestId =
        m_sinkKeychain->startDeletePasswordJob(service, key);

    const auto sourceKeychainRequestId =
        m_sourceKeychain->startDeletePasswordJob(service, key);

    m_deletePasswordJobIds.insert(
        IdBimap::value_type(sinkKeychainRequestId, sourceKeychainRequestId));

    return sinkKeychainRequestId;
}

void MigratingKeychainService::onSinkKeychainWritePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode,
    ErrorString errorDescription) // NOLINT
{
    const auto it = m_sinkKeychainWriteRequestIds.find(requestId);

    const auto internalIt =
        (it == m_sinkKeychainWriteRequestIds.end()
             ? m_internalSinkKeychainWriteRequestIdsToServiceAndKey.find(
                   requestId)
             : m_internalSinkKeychainWriteRequestIdsToServiceAndKey.end());

    if (it != m_sinkKeychainWriteRequestIds.end() ||
        internalIt !=
            m_internalSinkKeychainWriteRequestIdsToServiceAndKey.end())
    {
        QNDEBUG(
            "utility:keychain_migrating",
            "MigratingKeychainService::onSinkKeychainWritePasswordJobFinished: "
                << "request id = " << requestId << ", error code = "
                << errorCode << ", error description = " << errorDescription);
    }

    if (it != m_sinkKeychainWriteRequestIds.end()) {
        m_sinkKeychainWriteRequestIds.erase(it);
        Q_EMIT writePasswordJobFinished(requestId, errorCode, errorDescription);
        return;
    }

    if (internalIt !=
        m_internalSinkKeychainWriteRequestIdsToServiceAndKey.end()) {
        if (errorCode != IKeychainService::ErrorCode::NoError) {
            QNWARNING(
                "utility:keychain_migrating",
                "MigratingKeychainService::"
                    << "onSinkKeychainWritePasswordJobFinished: failed to "
                    << "copy password from source to sink keychain: error "
                    << "code = " << errorCode
                    << ", error description = " << errorDescription);
        }
        else {
            const auto sourceKeychainDeleteRequestId =
                m_sourceKeychain->startDeletePasswordJob(
                    internalIt.value().first, internalIt.value().second);

            QNDEBUG(
                "utility:keychain_migrating",
                "Deleting password from "
                    << "the source keychain: request id = "
                    << sourceKeychainDeleteRequestId
                    << ", service = " << internalIt.value().first
                    << ", key = " << internalIt.value().second);

            m_internalSourceKeychainDeleteRequestIds.insert(
                sourceKeychainDeleteRequestId);
        }

        m_internalSinkKeychainWriteRequestIdsToServiceAndKey.erase(internalIt);
        return;
    }
}

void MigratingKeychainService::onSinkKeychainReadPasswordJobFinished(
    QUuid requestId, ErrorCode errorCode,
    ErrorString errorDescription, // NOLINT
    QString password)             // NOLINT
{
    const auto it = m_sinkKeychainReadRequestIdsToServiceAndKey.find(requestId);
    if (it == m_sinkKeychainReadRequestIdsToServiceAndKey.end()) {
        return;
    }

    QNDEBUG(
        "utility:keychain_migrating",
        "MigratingKeychainService::onSinkKeychainReadPasswordJobFinished: "
            << "request id = " << requestId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    if (errorCode != IKeychainService::ErrorCode::EntryNotFound) {
        m_sinkKeychainReadRequestIdsToServiceAndKey.erase(it);
        Q_EMIT readPasswordJobFinished(
            requestId, errorCode, errorDescription, password);
        return;
    }

    // Could not find entry in the sink keychain, will try to read from
    // the source keychain
    const auto sourceKeychainRequestId = m_sourceKeychain->startReadPasswordJob(
        it.value().first, it.value().second);

    m_sourceKeychainReadRequestData[sourceKeychainRequestId] =
        ReadPasswordJobData{
            requestId, it.value().first, it.value().second, password};

    m_sinkKeychainReadRequestIdsToServiceAndKey.erase(it);
}

void MigratingKeychainService::onSourceKeychainReadPasswordJobFinished(
    QUuid requestId, ErrorCode errorCode,
    ErrorString errorDescription, // NOLINT
    QString password)             // NOLINT
{
    const auto it = m_sourceKeychainReadRequestData.find(requestId);
    if (it == m_sourceKeychainReadRequestData.end()) {
        return;
    }

    QNDEBUG(
        "utility:keychain_migrating",
        "MigratingKeychainService::onSourceKeychainReadPasswordJobFinished: "
            << "request id = " << requestId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    requestId = it.value().m_sinkKeychainReadRequestId;

    Q_EMIT readPasswordJobFinished(
        requestId, errorCode, errorDescription, password); // NOLINT

    if (errorCode == IKeychainService::ErrorCode::NoError) {
        const auto sinkKeychainWriteRequestId =
            m_sinkKeychain->startWritePasswordJob(
                it.value().m_service, it.value().m_key, it.value().m_password);

        QNDEBUG(
            "utility:keychain_migrating",
            "Copying the password to sink keychain: write request id = "
                << sinkKeychainWriteRequestId);

        m_internalSinkKeychainWriteRequestIdsToServiceAndKey
            [sinkKeychainWriteRequestId] =
                std::make_pair(it.value().m_service, it.value().m_key);
    }

    m_sourceKeychainReadRequestData.erase(it);
}

void MigratingKeychainService::onSinkKeychainDeletePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    const auto it = m_deletePasswordJobIds.left.find(requestId);
    if (it == m_deletePasswordJobIds.left.end()) {
        return;
    }

    QNDEBUG(
        "utility:keychain_migrating",
        "MigratingKeychainService::onSinkKeychainDeletePasswordJobFinished: "
            << "request id = " << requestId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    const auto sourceKeychainRequestId = it->second;

    const auto resultIt =
        m_completedDeletePasswordJobs.find(sourceKeychainRequestId);

    if (resultIt == m_completedDeletePasswordJobs.end()) {
        // The corresponding source keychain's job hasn't finished yet, will
        // record the sink one's status and wait for the source keychain
        auto & status = m_completedDeletePasswordJobs[requestId];
        status.m_errorCode = errorCode;
        status.m_errorDescription = errorDescription;
        return;
    }

    // Delete jobs for both sink and source keychain have finished

    if (errorCode == IKeychainService::ErrorCode::EntryNotFound &&
        resultIt->m_errorCode == IKeychainService::ErrorCode::NoError)
    {
        // Didn't find the entry in the sink keychain but successfully
        // deleted it from the source keychain, propagating successful
        // deletion to the user
        errorCode = IKeychainService::ErrorCode::NoError;
        errorDescription.clear();
    }

    // Otherwise propagating the sink keychain's error code as is to the user
    m_deletePasswordJobIds.left.erase(it);
    m_completedDeletePasswordJobs.erase(resultIt);

    Q_EMIT deletePasswordJobFinished(requestId, errorCode, errorDescription);
}

void MigratingKeychainService::onSourceKeychainDeletePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    const auto it = m_deletePasswordJobIds.right.find(requestId);

    const auto internalIt =
        (it == m_deletePasswordJobIds.right.end()
             ? m_internalSourceKeychainDeleteRequestIds.find(requestId)
             : m_internalSourceKeychainDeleteRequestIds.end());

    if (it != m_deletePasswordJobIds.right.end() ||
        internalIt != m_internalSourceKeychainDeleteRequestIds.end())
    {
        QNDEBUG(
            "utility:keychain_migrating",
            "MigratingKeychainService::"
                << "onSourceKeychainDeletePasswordJobFinished: request id = "
                << requestId << ", error code = " << errorCode
                << ", error description = " << errorDescription);
    }

    if (it != m_deletePasswordJobIds.right.end()) {
        const auto sinkKeychainRequestId = it->second;

        const auto resultIt =
            m_completedDeletePasswordJobs.find(sinkKeychainRequestId);

        if (resultIt == m_completedDeletePasswordJobs.end()) {
            // The corresponding sink keychain's job hasn't finished yet, will
            // record the source one's status and wait for the sink keychain
            auto & status = m_completedDeletePasswordJobs[requestId];
            status.m_errorCode = errorCode;
            status.m_errorDescription = errorDescription;
            return;
        }

        // Delete jobs for both sink and source keychain have finished

        if (resultIt->m_errorCode ==
                IKeychainService::ErrorCode::EntryNotFound &&
            errorCode == IKeychainService::ErrorCode::NoError)
        {
            // Didn't find the entry in the sink keychain but successfully
            // deleted it from the source keychain, propagating successful
            // deletion to the user
        }
        else {
            // Otherwise propagating the sink keychain's error code as is to
            // the user
            errorCode = resultIt->m_errorCode;
            errorDescription = resultIt->m_errorDescription;
        }

        m_deletePasswordJobIds.right.erase(it);
        m_completedDeletePasswordJobs.erase(resultIt);

        Q_EMIT deletePasswordJobFinished(
            sinkKeychainRequestId, errorCode, errorDescription);
        return;
    }

    if (internalIt != m_internalSourceKeychainDeleteRequestIds.end()) {
        if (errorCode != IKeychainService::ErrorCode::NoError) {
            QNWARNING(
                "utility:keychain_migrating",
                "MigratingKeychainService::"
                    << "onSourceKeychainDeletePasswordJobFinished: failed to "
                    << "delete password from source keychain: error code = "
                    << errorCode
                    << ", error description = " << errorDescription);
        }

        m_internalSourceKeychainDeleteRequestIds.erase(internalIt);
        return;
    }
}

void MigratingKeychainService::createConnections()
{
    QObject::connect(
        m_sinkKeychain.get(), &IKeychainService::writePasswordJobFinished, this,
        &MigratingKeychainService::onSinkKeychainWritePasswordJobFinished);

    QObject::connect(
        m_sinkKeychain.get(), &IKeychainService::readPasswordJobFinished, this,
        &MigratingKeychainService::onSinkKeychainReadPasswordJobFinished);

    QObject::connect(
        m_sourceKeychain.get(), &IKeychainService::readPasswordJobFinished,
        this,
        &MigratingKeychainService::onSourceKeychainReadPasswordJobFinished);

    QObject::connect(
        m_sinkKeychain.get(), &IKeychainService::deletePasswordJobFinished,
        this,
        &MigratingKeychainService::onSinkKeychainDeletePasswordJobFinished);

    QObject::connect(
        m_sourceKeychain.get(), &IKeychainService::deletePasswordJobFinished,
        this,
        &MigratingKeychainService::onSourceKeychainDeletePasswordJobFinished);
}

} // namespace quentier
