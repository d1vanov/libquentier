/*
 * Copyright 2020 Dmitry Ivanov
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

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>

#include <stdexcept>

#define CKDEBUG(message)                                                       \
    QNDEBUG("keychain:composite", "[" << m_name << "]: " << message)

namespace quentier {

namespace keys {

constexpr const char * failedWritesGroup = "FailedWritePasswordJobs";
constexpr const char * failedWritePair = "ServiceAndKeyPairs";
constexpr const char * failedWriteService = "Service";
constexpr const char * failedWriteKey = "Key";

} // namespace keys

CompositeKeychainService::CompositeKeychainService(
    QString name, IKeychainServicePtr primaryKeychain,
    IKeychainServicePtr secondaryKeychain) :
    m_name{std::move(name)},
    m_primaryKeychain{std::move(primaryKeychain)},
    m_secondaryKeychain{std::move(secondaryKeychain)}
{
    if (m_name.isEmpty()) {
        throw std::invalid_argument{"Composite keychain name is empty"};
    }

    if (Q_UNLIKELY(!m_primaryKeychain)) {
        throw std::invalid_argument{"Primary keychain is null"};
    }

    if (Q_UNLIKELY(!m_secondaryKeychain)) {
        throw std::invalid_argument{"Secondary keychain is null"};
    }

    createConnections();
}

CompositeKeychainService::~CompositeKeychainService() = default;

QUuid CompositeKeychainService::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    QUuid primaryKeychainRequestId =
        m_primaryKeychain->startWritePasswordJob(service, key, password);

    QUuid secondaryKeychainRequestId =
        m_secondaryKeychain->startWritePasswordJob(service, key, password);

    m_writePasswordJobIds.insert(IdBimap::value_type(
        primaryKeychainRequestId, secondaryKeychainRequestId));

    m_serviceAndKeyByRequestId[primaryKeychainRequestId] =
        std::make_pair(service, key);

    CKDEBUG(
        "CompositeKeychainService::startWritePasswordJob: service = "
        << service << ", key = " << key
        << ", primary keychain request id = " << primaryKeychainRequestId
        << ", secondary keychain request id = " << secondaryKeychainRequestId);

    return primaryKeychainRequestId;
}

QUuid CompositeKeychainService::startReadPasswordJob(
    const QString & service, const QString & key)
{
    CKDEBUG(
        "CompositeKeychainService::startReadPasswordJob: service = "
        << service << ", key = " << key);

    if (isServiceKeyPairAvailableInPrimaryKeychain(service, key)) {
        QUuid requestId = m_primaryKeychain->startReadPasswordJob(service, key);
        m_primaryKeychainReadPasswordJobIds.insert(requestId);
        CKDEBUG("Reading from primary keychain, request id = " << requestId);
        return requestId;
    }

    QUuid requestId = m_secondaryKeychain->startReadPasswordJob(service, key);
    m_secondaryKeychainReadPasswordJobIds.insert(requestId);
    CKDEBUG("Reading from secondary keychain, request id = " << requestId);
    return requestId;
}

QUuid CompositeKeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    QUuid primaryKeychainRequestId =
        m_primaryKeychain->startDeletePasswordJob(service, key);

    QUuid secondaryKeychainRequestId =
        m_secondaryKeychain->startDeletePasswordJob(service, key);

    m_deletePasswordJobIds.insert(IdBimap::value_type(
        primaryKeychainRequestId, secondaryKeychainRequestId));

    CKDEBUG(
        "CompositeKeychainService::startDeletePasswordJob: service = "
        << service << ", key = " << key
        << ", primary keychain request id = " << primaryKeychainRequestId
        << ", secondary keychain request id = " << secondaryKeychainRequestId);

    return primaryKeychainRequestId;
}

bool CompositeKeychainService::isPrimaryKeychainOperational() const
{
    ApplicationSettings settings{m_name};
    settings.beginGroup(keys::failedWritesGroup);
    int keyCount = settings.allKeys().size();
    settings.endGroup();
    return (keyCount < 100);
}

void CompositeKeychainService::onPrimaryKeychainWritePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    auto it = m_writePasswordJobIds.left.find(requestId);
    if (it == m_writePasswordJobIds.left.end()) {
        return;
    }

    auto serviceKeyIt = m_serviceAndKeyByRequestId.find(requestId);
    if (Q_UNLIKELY(serviceKeyIt == m_serviceAndKeyByRequestId.end())) {
        QNERROR(
            "keychain:composite",
            "Unable to find service and key for request id " << requestId);
        throw std::logic_error{
            "CompositeKeychainService: could not find service and key for "
            "request id"};
    }

    const QString & service = serviceKeyIt.value().first;
    const QString & key = serviceKeyIt.value().second;

    CKDEBUG(
        "CompositeKeychainService::onPrimaryKeychainWritePasswordJobFinished: "
        << "request id = " << requestId << ", error code = " << errorCode
        << ", error description: " << errorDescription);

    const auto secondaryKeychainJobId = it->second;
    auto resultIt = m_completedWritePasswordJobs.find(secondaryKeychainJobId);
    if (resultIt != m_completedWritePasswordJobs.end()) {
        // Write jobs for this service, key and password have finished for both
        // keychains, need to analyze the results now

        const auto & secondaryKeychainStatus = resultIt.value();
        if (secondaryKeychainStatus.m_errorCode != ErrorCode::NoError) {
            // Writing failed for the secondary keychain
            markServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
        }

        if (errorCode != ErrorCode::NoError) {
            // Writing failed for the primary keychain
            markServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
        }

        // Clean things up and propagate the best result to the user
        const ErrorCode bestErrorCode =
            (errorCode == ErrorCode::NoError
                 ? errorCode
                 : secondaryKeychainStatus.m_errorCode);

        const ErrorString bestErrorDescription =
            (errorCode == ErrorCode::NoError
                 ? errorDescription
                 : secondaryKeychainStatus.m_errorDescription);

        m_completedWritePasswordJobs.erase(resultIt);
        m_writePasswordJobIds.left.erase(it);

        CKDEBUG(
            "Propagating best result to the user: error code = "
            << bestErrorCode
            << ", error description = " << bestErrorDescription);

        Q_EMIT writePasswordJobFinished(
            requestId, bestErrorCode, bestErrorDescription);
        return;
    }

    // The corresponding secondary keychain's job hasn't finished yet, will
    // record the primary one's status and wait for the secondary keychain
    auto & status = m_completedWritePasswordJobs[requestId];
    status.m_errorCode = errorCode;
    status.m_errorDescription = errorDescription;
}

} // namespace quentier
