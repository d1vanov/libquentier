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

#include "MigratingKeychainService.h"

#include <quentier/logging/QuentierLogger.h>

#include <stdexcept>

namespace quentier {

MigratingKeychainService::MigratingKeychainService(
    IKeychainServicePtr sourceKeychain, IKeychainServicePtr sinkKeychain,
    QObject * parent) :
    IKeychainService(parent),
    m_sourceKeychain{std::move(sourceKeychain)}, m_sinkKeychain{
                                                     std::move(sinkKeychain)}
{
    if (Q_UNLIKELY(!m_sourceKeychain)) {
        throw std::invalid_argument{
            "MigratingKeychainService ctor: source keychain is null"};
    }

    if (Q_UNLIKELY(!m_sinkKeychain)) {
        throw std::invalid_argument{
            "MigratingKeychainService ctor: sink keychain is null"};
    }
}

MigratingKeychainService::~MigratingKeychainService() = default;

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
    const auto requestId = m_sinkKeychain->startDeletePasswordJob(service, key);

    m_sinkKeychainDeleteRequestIdsToServiceAndKey[requestId] =
        std::make_pair(service, key);

    return requestId;
}

void MigratingKeychainService::onSinkKeychainWritePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    const auto it = m_sinkKeychainWriteRequestIds.find(requestId);
    if (it != m_sinkKeychainWriteRequestIds.end())
    {
        QNDEBUG(
            "utility:keychain_migrating",
            "MigratingKeychainService::onSinkKeychainWritePasswordJobFinished: "
            << "request id = " << requestId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

        m_sinkKeychainWriteRequestIds.erase(it);
        Q_EMIT writePasswordJobFinished(requestId, errorCode, errorDescription);
        return;
    }

    // TODO: handle the case of writing from source to sink hidden from user
}

void MigratingKeychainService::onSinkKeychainReadPasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
    QString password)
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

    m_sourceToSinkKeychainReadRequestIds[sourceKeychainRequestId] = requestId;
}

void MigratingKeychainService::onSourceKeychainReadPasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
    QString password)
{
    auto it = m_sourceToSinkKeychainReadRequestIds.find(requestId);
    if (it == m_sourceToSinkKeychainReadRequestIds.end()) {
        return;
    }

    QNDEBUG(
        "utility:keychain_migrating",
        "MigratingKeychainService::onSourceKeychainReadPasswordJobFinished: "
            << "request id = " << requestId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    requestId = it.value();
    m_sourceToSinkKeychainReadRequestIds.erase(it);

    Q_EMIT readPasswordJobFinished(
        requestId, errorCode, errorDescription, password);

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        return;
    }

    // TODO: write the read password to sink keychain
}

} // namespace quentier
