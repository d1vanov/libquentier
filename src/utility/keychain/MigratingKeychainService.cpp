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

    createConnections();
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

    m_sourceKeychainReadRequestData[sourceKeychainRequestId] =
        ReadPasswordJobData{
            requestId, it.value().first, it.value().second, password};

    m_sinkKeychainReadRequestIdsToServiceAndKey.erase(it);
}

void MigratingKeychainService::onSourceKeychainReadPasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
    QString password)
{
    auto it = m_sourceKeychainReadRequestData.find(requestId);
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
        requestId, errorCode, errorDescription, password);

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
    auto it = m_sinkKeychainDeleteRequestIdsToServiceAndKey.find(requestId);
    if (it == m_sinkKeychainDeleteRequestIdsToServiceAndKey.end()) {
        return;
    }

    QNDEBUG(
        "utility:keychain_migrating",
        "MigratingKeychainService::onSinkKeychainDeletePasswordJobFinished: "
            << "request id = " << requestId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    if (errorCode != IKeychainService::ErrorCode::EntryNotFound) {
        m_sinkKeychainDeleteRequestIdsToServiceAndKey.erase(it);
        Q_EMIT deletePasswordJobFinished(
            requestId, errorCode, errorDescription);
        return;
    }

    // Could not find the entry in the sink keychain, will try to remove
    // from source keychain
    const auto sourceKeychainDeleteRequestId =
        m_sourceKeychain->startDeletePasswordJob(
            it.value().first, it.value().second);

    m_sinkKeychainDeleteRequestIdsToServiceAndKey.erase(it);

    QNDEBUG(
        "utility:keychain_migrating",
        "Trying to delete the password from the source keychain: delete "
            << "request id = " << sourceKeychainDeleteRequestId);

    m_sourceToSinkKeychainDeleteRequestIds[sourceKeychainDeleteRequestId] =
        requestId;
}

void MigratingKeychainService::onSourceKeychainDeletePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    const auto it = m_sourceToSinkKeychainDeleteRequestIds.find(requestId);

    const auto internalIt =
        (it == m_sourceToSinkKeychainDeleteRequestIds.end()
         ? m_internalSourceKeychainDeleteRequestIds.find(requestId)
         : m_internalSourceKeychainDeleteRequestIds.end());

    if (it != m_sourceToSinkKeychainDeleteRequestIds.end() ||
        internalIt != m_internalSourceKeychainDeleteRequestIds.end())
    {
        QNDEBUG(
            "utility:keychain_migrating",
            "MigratingKeychainService::"
                << "onSourceKeychainDeletePasswordJobFinished: request id = "
                << requestId << ", error code = " << errorCode
                << ", error description = " << errorDescription);
    }

    if (it != m_sourceToSinkKeychainDeleteRequestIds.end()) {
        requestId = it.value();
        m_sourceToSinkKeychainDeleteRequestIds.erase(it);
        Q_EMIT deletePasswordJobFinished(
            requestId, errorCode, errorDescription);
        return;
    }

    if (internalIt != m_internalSourceKeychainDeleteRequestIds.end()) {
        if (errorCode != IKeychainService::ErrorCode::NoError) {
            QNWARNING(
                "utility:keychain_migrating",
                "MigratingKeychainService::"
                    << "onSourceKeychainDeletePasswordJobFinished: failed to "
                    << "delete password from source keychain: error code = "
                    << errorCode << ", error description = "
                    << errorDescription);
        }

        m_internalSourceKeychainDeleteRequestIds.erase(internalIt);
        return;
    }
}

void MigratingKeychainService::createConnections()
{
    QObject::connect(
        m_sinkKeychain.get(),
        &IKeychainService::writePasswordJobFinished,
        this,
        &MigratingKeychainService::onSinkKeychainWritePasswordJobFinished);

    QObject::connect(
        m_sinkKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        this,
        &MigratingKeychainService::onSinkKeychainReadPasswordJobFinished);

    QObject::connect(
        m_sourceKeychain.get(),
        &IKeychainService::readPasswordJobFinished,
        this,
        &MigratingKeychainService::onSourceKeychainReadPasswordJobFinished);

    QObject::connect(
        m_sinkKeychain.get(),
        &IKeychainService::deletePasswordJobFinished,
        this,
        &MigratingKeychainService::onSinkKeychainDeletePasswordJobFinished);

    QObject::connect(
        m_sourceKeychain.get(),
        &IKeychainService::deletePasswordJobFinished,
        this,
        &MigratingKeychainService::onSourceKeychainDeletePasswordJobFinished);
}

} // namespace quentier
