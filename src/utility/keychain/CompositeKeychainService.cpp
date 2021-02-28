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

#include <QMetaObject>

#include <stdexcept>

#define CKDEBUG(message)                                                       \
    QNDEBUG("utility:keychain_composite", "[" << m_name << "]: " << message)

#define CKINFO(message)                                                        \
    QNINFO("utility:keychain_composite", "[" << m_name << "]: " << message)

#define CKERROR(message)                                                       \
    QNERROR("utility:keychain_composite", "[" << m_name << "]: " << message)

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
    IKeychainServicePtr secondaryKeychain, QObject * parent) :
    IKeychainService(parent),
    m_name{std::move(name)}, m_primaryKeychain{std::move(primaryKeychain)},
    m_secondaryKeychain{std::move(secondaryKeychain)}
{
    if (m_name.isEmpty()) {
        throw std::invalid_argument{
            "CompositeKeychainService ctor: name is empty"};
    }

    if (Q_UNLIKELY(!m_primaryKeychain)) {
        throw std::invalid_argument{
            "CompositeKeychainService ctor: primary keychain is null"};
    }

    if (Q_UNLIKELY(!m_secondaryKeychain)) {
        throw std::invalid_argument{
            "CompositeKeychainService ctor: secondary keychain is null"};
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

    m_serviceAndKeyByRequestId[secondaryKeychainRequestId] =
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
        m_serviceAndKeyByRequestId[requestId] = std::make_pair(service, key);
        CKDEBUG("Reading from primary keychain, request id = " << requestId);
        return requestId;
    }

    if (isServiceKeyPairAvailableInSecondaryKeychain(service, key)) {
        QUuid requestId =
            m_secondaryKeychain->startReadPasswordJob(service, key);

        m_secondaryKeychainReadPasswordJobIdsToPrimaryKeychainJobIds
            [requestId] = QUuid();

        m_serviceAndKeyByRequestId[requestId] = std::make_pair(service, key);
        CKDEBUG("Reading from secondary keychain, request id = " << requestId);
        return requestId;
    }

    CKDEBUG(
        "Service and key pair is not available in either primary or secondary "
        << "keychain, simulating failed read");

    const auto requestId = QUuid::createUuid();

    QMetaObject::invokeMethod(
        this, "readPasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::EntryNotFound),
        Q_ARG(ErrorString, ErrorString()), Q_ARG(QString, QString()));

    return requestId;
}

QUuid CompositeKeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    CKDEBUG(
        "CompositeKeychainService::startDeletePasswordJob: service = "
        << service << ", key = " << key);

    QUuid primaryKeychainRequestId;
    QUuid secondaryKeychainRequestId;

    if (isServiceKeyPairAvailableInPrimaryKeychain(service, key)) {
        primaryKeychainRequestId =
            m_primaryKeychain->startDeletePasswordJob(service, key);
    }

    if (isServiceKeyPairAvailableInSecondaryKeychain(service, key)) {
        secondaryKeychainRequestId =
            m_secondaryKeychain->startDeletePasswordJob(service, key);
    }

    if (primaryKeychainRequestId == QUuid() &&
        secondaryKeychainRequestId == QUuid())
    {
        CKDEBUG(
            "Service and key pair \""
            << service << "\" + \"" << key
            << "\" is not available in either keychain, simulating successful "
            << "deletion");

        const auto requestId = QUuid::createUuid();

        QMetaObject::invokeMethod(
            this, "deletePasswordJobFinished", Qt::QueuedConnection,
            Q_ARG(QUuid, requestId), Q_ARG(ErrorCode, ErrorCode::NoError),
            Q_ARG(ErrorString, ErrorString()));

        return requestId;
    }

    if (primaryKeychainRequestId == QUuid()) {
        m_secondaryKeychainSingleDeletePasswordJobIds.insert(
            secondaryKeychainRequestId);
    }
    else if (secondaryKeychainRequestId == QUuid()) {
        m_primaryKeychainSingleDeletePasswordJobIds.insert(
            primaryKeychainRequestId);
    }
    else {
        m_deletePasswordJobIds.insert(IdBimap::value_type(
            primaryKeychainRequestId, secondaryKeychainRequestId));
    }

    if (primaryKeychainRequestId != QUuid()) {
        m_serviceAndKeyByRequestId[primaryKeychainRequestId] =
            std::make_pair(service, key);
    }

    if (secondaryKeychainRequestId != QUuid()) {
        m_serviceAndKeyByRequestId[secondaryKeychainRequestId] =
            std::make_pair(service, key);
    }

    CKDEBUG(
        "CompositeKeychainService::startDeletePasswordJob: service = "
        << service << ", key = " << key
        << ", primary keychain request id = " << primaryKeychainRequestId
        << ", secondary keychain request id = " << secondaryKeychainRequestId);

    return (
        primaryKeychainRequestId == QUuid() ? secondaryKeychainRequestId
                                            : primaryKeychainRequestId);
}

bool CompositeKeychainService::isPrimaryKeychainOperational() const
{
    ApplicationSettings settings{m_name};
    settings.beginGroup(keys::unavailablePrimaryKeychainGroup);
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

    const auto serviceKeyPair = serviceAndKeyForRequestId(requestId);
    const QString & service = serviceKeyPair.first;
    const QString & key = serviceKeyPair.second;

    CKDEBUG(
        "CompositeKeychainService::onPrimaryKeychainWritePasswordJobFinished: "
        << "request id = " << requestId << ", error code = " << errorCode
        << ", error description: " << errorDescription
        << ", service = " << service << ", key = " << key);

    const auto secondaryKeychainJobId = it->second;
    const auto resultIt =
        m_completedWritePasswordJobs.find(secondaryKeychainJobId);
    if (resultIt == m_completedWritePasswordJobs.end()) {
        // The corresponding secondary keychain's job hasn't finished yet, will
        // record the primary one's status and wait for the secondary keychain
        auto & status = m_completedWritePasswordJobs[requestId];
        status.m_errorCode = errorCode;
        status.m_errorDescription = errorDescription;
        cleanupServiceAndKeyForRequestId(requestId);
        return;
    }

    // Write jobs for this service and key pair have finished for both
    // keychains, need to analyze the results now

    const auto & secondaryKeychainStatus = resultIt.value();
    if (secondaryKeychainStatus.m_errorCode != ErrorCode::NoError) {
        // Writing failed for the secondary keychain
        markServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
    }
    else {
        unmarkServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
    }

    if (errorCode != ErrorCode::NoError) {
        // Writing failed for the primary keychain
        markServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
    }
    else {
        unmarkServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
    }

    // Clean things up and propagate the best result to the user
    const ErrorCode bestErrorCode =
        (errorCode == ErrorCode::NoError ? errorCode
                                         : secondaryKeychainStatus.m_errorCode);

    const ErrorString bestErrorDescription =
        (errorCode == ErrorCode::NoError
             ? errorDescription
             : secondaryKeychainStatus.m_errorDescription);

    m_completedWritePasswordJobs.erase(resultIt);
    m_writePasswordJobIds.left.erase(it);
    cleanupServiceAndKeyForRequestId(requestId);

    CKDEBUG(
        "Propagating best result to the user: error code = "
        << bestErrorCode << ", error description = " << bestErrorDescription);

    Q_EMIT writePasswordJobFinished(
        requestId, bestErrorCode, bestErrorDescription);
}

void CompositeKeychainService::onSecondaryKeychainWritePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    auto it = m_writePasswordJobIds.right.find(requestId);
    if (it == m_writePasswordJobIds.right.end()) {
        return;
    }

    const auto serviceKeyPair = serviceAndKeyForRequestId(requestId);
    const QString & service = serviceKeyPair.first;
    const QString & key = serviceKeyPair.second;

    CKDEBUG(
        "CompositeKeychainService::onSecondaryKeychainWritePasswordJobFinished:"
        << " request id = " << requestId << ", error code = " << errorCode
        << ", error description: " << errorDescription
        << ", service = " << service << ", key = " << key);

    const auto primaryKeychainRequestId = it->second;

    const auto resultIt =
        m_completedWritePasswordJobs.find(primaryKeychainRequestId);

    if (resultIt == m_completedWritePasswordJobs.end()) {
        // The corresponding primary keychain's job hasn't finished yet, will
        // record the secondary one's status and wait for the primary keychain
        auto & status = m_completedWritePasswordJobs[requestId];
        status.m_errorCode = errorCode;
        status.m_errorDescription = errorDescription;
        cleanupServiceAndKeyForRequestId(requestId);
        return;
    }

    // Write jobs for this service and key pair have finished for both
    // keychains, need to analyze the results now

    const auto & primaryKeychainStatus = resultIt.value();
    if (primaryKeychainStatus.m_errorCode != ErrorCode::NoError) {
        // Writing failed for the primary keychain
        markServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
    }
    else {
        unmarkServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
    }

    if (errorCode != ErrorCode::NoError) {
        // Writing failed for the secondary keychain
        markServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
    }
    else {
        unmarkServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
    }

    // Clean things up and propagate the best result to the user
    // NOTE: if both write jobs have failed, the error from the primary
    // keychain should be propagated to the user
    const ErrorCode bestErrorCode =
        (primaryKeychainStatus.m_errorCode == ErrorCode::NoError
             ? primaryKeychainStatus.m_errorCode
             : (errorCode == ErrorCode::NoError
                    ? errorCode
                    : primaryKeychainStatus.m_errorCode));

    const ErrorString bestErrorDescription =
        (primaryKeychainStatus.m_errorCode == ErrorCode::NoError
             ? primaryKeychainStatus.m_errorDescription
             : (errorCode == ErrorCode::NoError
                    ? errorDescription
                    : primaryKeychainStatus.m_errorDescription));

    m_completedWritePasswordJobs.erase(resultIt);
    m_writePasswordJobIds.right.erase(it);
    cleanupServiceAndKeyForRequestId(requestId);

    CKDEBUG(
        "Propagating best result to the user: error code = "
        << bestErrorCode << ", error description = " << bestErrorDescription
        << ", request id = " << primaryKeychainRequestId);

    Q_EMIT writePasswordJobFinished(
        primaryKeychainRequestId, bestErrorCode, bestErrorDescription);
}

void CompositeKeychainService::onPrimaryKeychainReadPasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
    QString password)
{
    auto it = m_primaryKeychainReadPasswordJobIds.find(requestId);
    if (it == m_primaryKeychainReadPasswordJobIds.end()) {
        return;
    }

    const auto serviceKeyPair = serviceAndKeyForRequestId(requestId);
    const QString & service = serviceKeyPair.first;
    const QString & key = serviceKeyPair.second;

    CKDEBUG(
        "CompositeKeychainService::onPrimaryKeychainReadPasswordJobFinished: "
        << "request id = " << requestId << ", error code = " << errorCode
        << ", error errorDescription: " << errorDescription
        << ", service = " << service << ", key = " << key);

    if (errorCode == ErrorCode::NoError ||
        !isServiceKeyPairAvailableInSecondaryKeychain(service, key))
    {
        // Propagating the result as is to the user
        m_primaryKeychainReadPasswordJobIds.erase(it);
        cleanupServiceAndKeyForRequestId(requestId);

        Q_EMIT readPasswordJobFinished(
            requestId, errorCode, errorDescription, password);
        return;
    }

    CKINFO(
        "Failed to read password from the primary keychain: error code = "
        << errorCode << ", error description = " << errorDescription);

    const auto secondaryKeychainRequestId =
        m_secondaryKeychain->startReadPasswordJob(service, key);

    m_serviceAndKeyByRequestId[secondaryKeychainRequestId] =
        std::make_pair(service, key);

    cleanupServiceAndKeyForRequestId(requestId);
    m_secondaryKeychainReadPasswordJobIdsToPrimaryKeychainJobIds
        [secondaryKeychainRequestId] = requestId;

    CKDEBUG(
        "Reading from secondary keychain, request id = "
        << secondaryKeychainRequestId);
}

void CompositeKeychainService::onSecondaryKeychainReadPasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription,
    QString password)
{
    auto it = m_secondaryKeychainReadPasswordJobIdsToPrimaryKeychainJobIds.find(
        requestId);

    // clang-format off
    if (it ==
        m_secondaryKeychainReadPasswordJobIdsToPrimaryKeychainJobIds.end())
    {
        return;
    }
    // clang-format on

    const auto serviceKeyPair = serviceAndKeyForRequestId(requestId);
    const QString & service = serviceKeyPair.first;
    const QString & key = serviceKeyPair.second;

    CKDEBUG(
        "CompositeKeychainService::onSecondaryKeychainReadPasswordJobFinished: "
        << "request id = " << requestId << ", error code = " << errorCode
        << ", error description = " << errorDescription
        << ", service = " << service << ", key = " << key);

    const auto primaryKeychainRequestId = it.value();

    // Reading from the secondary keychain is a fallback, so doing the cleanup
    // and propagating whatever result we have received to the user
    m_secondaryKeychainReadPasswordJobIdsToPrimaryKeychainJobIds.erase(it);
    cleanupServiceAndKeyForRequestId(requestId);

    if (primaryKeychainRequestId != QUuid()) {
        requestId = primaryKeychainRequestId;
    }

    Q_EMIT readPasswordJobFinished(
        requestId, errorCode, errorDescription, password);
}

void CompositeKeychainService::onPrimaryKeychainDeletePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    auto bimapIt = m_deletePasswordJobIds.left.find(requestId);
    auto singleIt = m_primaryKeychainSingleDeletePasswordJobIds.end();
    if (bimapIt == m_deletePasswordJobIds.left.end()) {
        singleIt = m_primaryKeychainSingleDeletePasswordJobIds.find(requestId);
        if (singleIt == m_primaryKeychainSingleDeletePasswordJobIds.end()) {
            return;
        }
    }

    const auto serviceKeyPair = serviceAndKeyForRequestId(requestId);
    const QString & service = serviceKeyPair.first;
    const QString & key = serviceKeyPair.second;

    CKDEBUG(
        "CompositeKeychainService::onPrimaryKeychainDeletePasswordJobFinished: "
        << "request id = " << requestId << ", error code = " << errorCode
        << ", error description = " << errorDescription
        << ", service = " << service << ", key = " << key);

    if (bimapIt == m_deletePasswordJobIds.left.end()) {
        // Deletion attempt was issued only for the primary keychain but not
        // for the secondary one

        if (errorCode != ErrorCode::NoError) {
            CKINFO(
                "Failed to delete service and key from the primary keychain: "
                << "error code = " << errorCode << ", error description: "
                << errorDescription << "; simulating successful deletion "
                << "through marking service and key pair \"" << service
                << "\" + \"" << key << "\" as unavailable in the "
                << "primary keychain");

            markServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
            errorCode = ErrorCode::NoError;
            errorDescription.clear();
        }

        m_primaryKeychainSingleDeletePasswordJobIds.erase(singleIt);
        cleanupServiceAndKeyForRequestId(requestId);

        Q_EMIT deletePasswordJobFinished(
            requestId, errorCode, errorDescription);
        return;
    }

    // Deletion attempt was issued for both keychains

    const auto secondaryKeychainRequestId = bimapIt->second;
    const auto resultIt =
        m_completedDeletePasswordJobs.find(secondaryKeychainRequestId);
    if (resultIt == m_completedDeletePasswordJobs.end()) {
        // The corresponding secondary keychain's job hasn't finished yet, will
        // record the primary one's status and wait for the secondary keychain
        auto & status = m_completedDeletePasswordJobs[requestId];
        status.m_errorCode = errorCode;
        status.m_errorDescription = errorDescription;
        cleanupServiceAndKeyForRequestId(requestId);
        return;
    }

    // Delete jobs for this service and key pair have finished for both
    // keychains, need to analyze the results now

    if (errorCode != ErrorCode::NoError) {
        CKINFO(
            "Failed to delete service and key from the primary keychain: "
            << "error code = " << errorCode << ", error description: "
            << errorDescription << "; simulating successful deletion "
            << "through marking service and key pair \"" << service << "\" + \""
            << key << "\" as unavailable in the "
            << "primary keychain");

        markServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
        errorCode = ErrorCode::NoError;
        errorDescription.clear();
    }

    const auto & secondaryKeychainStatus = resultIt.value();
    if (secondaryKeychainStatus.m_errorCode != ErrorCode::NoError) {
        CKINFO(
            "Failed to delete service and key from the secondary keychain: "
            << "error code = " << errorCode << ", error description: "
            << errorDescription << "; simulating successful deletion "
            << "through marking service and key pair \"" << service << "\" + \""
            << key << "\" as unavailable in the "
            << "secondary keychain");

        markServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
    }

    m_completedDeletePasswordJobs.erase(resultIt);
    m_deletePasswordJobIds.left.erase(bimapIt);
    cleanupServiceAndKeyForRequestId(requestId);

    Q_EMIT deletePasswordJobFinished(requestId, errorCode, errorDescription);
}

void CompositeKeychainService::onSecondaryKeychainDeletePasswordJobFinished(
    QUuid requestId, ErrorCode errorCode, ErrorString errorDescription)
{
    auto bimapIt = m_deletePasswordJobIds.right.find(requestId);
    auto singleIt = m_secondaryKeychainSingleDeletePasswordJobIds.end();
    if (bimapIt == m_deletePasswordJobIds.right.end()) {
        singleIt =
            m_secondaryKeychainSingleDeletePasswordJobIds.find(requestId);

        if (singleIt == m_secondaryKeychainSingleDeletePasswordJobIds.end()) {
            return;
        }
    }

    const auto serviceKeyPair = serviceAndKeyForRequestId(requestId);
    const QString & service = serviceKeyPair.first;
    const QString & key = serviceKeyPair.second;

    CKDEBUG(
        "CompositeKeychainService::onSecondaryKeychainDeletePasswordJobFinished"
        << ": request id = " << requestId << ", error code = " << errorCode
        << ", error description = " << errorDescription
        << ", service = " << service << ", key = " << key);

    if (bimapIt == m_deletePasswordJobIds.right.end()) {
        // Deletion attempt was issues only for the secondary keychain but not
        // for the primary one

        if (errorCode != ErrorCode::NoError) {
            CKINFO(
                "Failed to delete service and key from the secondary keychain: "
                << "error code = " << errorCode << ", error description: "
                << errorDescription << "; simulating successful deletion "
                << "through marking service and key pair \"" << service
                << "\" + \"" << key << "\" as unavailable in the "
                << "secondary keychain");

            markServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
            errorCode = ErrorCode::NoError;
            errorDescription.clear();
        }

        m_secondaryKeychainSingleDeletePasswordJobIds.erase(singleIt);
        cleanupServiceAndKeyForRequestId(requestId);

        Q_EMIT deletePasswordJobFinished(
            requestId, errorCode, errorDescription);
        return;
    }

    // Deletion attempt was issued for both keychains

    const auto primaryKeychainRequestId = bimapIt->second;
    const auto resultIt =
        m_completedDeletePasswordJobs.find(primaryKeychainRequestId);
    if (resultIt == m_completedDeletePasswordJobs.end()) {
        // The corresponding primary keychain's job hasn't finished yet, will
        // record the secondary one's status and wait for the primary keychain
        auto & status = m_completedDeletePasswordJobs[requestId];
        status.m_errorCode = errorCode;
        status.m_errorDescription = errorDescription;
        cleanupServiceAndKeyForRequestId(requestId);
        return;
    }

    // Delete jobs for this service and key pair have finished for both
    // keychains, need to analyze the results now

    const auto & primaryKeychainStatus = resultIt.value();
    if (primaryKeychainStatus.m_errorCode != ErrorCode::NoError) {
        CKINFO(
            "Failed to delete service and key from the primary keychain: "
            << "error code = " << errorCode << ", error description: "
            << errorDescription << "; simulating successful deletion "
            << "through marking service and key pair \"" << service << "\" + \""
            << key << "\" as unavailable in the "
            << "primary keychain");

        markServiceKeyPairAsUnavailableInPrimaryKeychain(service, key);
    }

    if (errorCode != ErrorCode::NoError) {
        CKINFO(
            "Failed to delete service and key from the secondary keychain: "
            << "error code = " << errorCode << ", error description: "
            << errorDescription << "; simulating successful deletion "
            << "through marking service and key pair \"" << service << "\" + \""
            << key << "\" as unavailable in the "
            << "secondary keychain");

        markServiceKeyPairAsUnavailableInSecondaryKeychain(service, key);
        errorCode = ErrorCode::NoError;
        errorDescription.clear();
    }

    m_completedDeletePasswordJobs.erase(resultIt);
    m_deletePasswordJobIds.right.erase(bimapIt);
    cleanupServiceAndKeyForRequestId(requestId);

    Q_EMIT deletePasswordJobFinished(
        primaryKeychainRequestId, errorCode, errorDescription);
}

void CompositeKeychainService::createConnections()
{
    QObject::connect(
        m_primaryKeychain.get(), &IKeychainService::writePasswordJobFinished,
        this,
        &CompositeKeychainService::onPrimaryKeychainWritePasswordJobFinished);

    QObject::connect(
        m_primaryKeychain.get(), &IKeychainService::readPasswordJobFinished,
        this,
        &CompositeKeychainService::onPrimaryKeychainReadPasswordJobFinished);

    QObject::connect(
        m_primaryKeychain.get(), &IKeychainService::deletePasswordJobFinished,
        this,
        &CompositeKeychainService::onPrimaryKeychainDeletePasswordJobFinished);

    QObject::connect(
        m_secondaryKeychain.get(), &IKeychainService::writePasswordJobFinished,
        this,
        &CompositeKeychainService::onSecondaryKeychainWritePasswordJobFinished);

    QObject::connect(
        m_secondaryKeychain.get(), &IKeychainService::readPasswordJobFinished,
        this,
        &CompositeKeychainService::onSecondaryKeychainReadPasswordJobFinished);

    QObject::connect(
        m_secondaryKeychain.get(), &IKeychainService::deletePasswordJobFinished,
        this,
        &CompositeKeychainService::
            onSecondaryKeychainDeletePasswordJobFinished);
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
    const QString & service, const QString & key) const
{
    checkAndInitializeServiceKeysCaches();

    auto serviceIt = m_serviceKeysUnavailableInPrimaryKeychain.find(service);
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
    const QString & service, const QString & key) const
{
    checkAndInitializeServiceKeysCaches();

    auto serviceIt = m_serviceKeysUnavailableInSecondaryKeychain.find(service);
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
    int size = settings.beginReadArray(keys::serviceKeyPair);
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

std::pair<QString, QString> CompositeKeychainService::serviceAndKeyForRequestId(
    const QUuid & requestId) const
{
    auto serviceKeyIt = m_serviceAndKeyByRequestId.find(requestId);
    if (Q_UNLIKELY(serviceKeyIt == m_serviceAndKeyByRequestId.end())) {
        CKERROR("Unable to find service and key for request id " << requestId);
        throw std::logic_error{
            "CompositeKeychainService: could not find service and key for "
            "request id"};
    }

    return std::make_pair(
        serviceKeyIt.value().first, serviceKeyIt.value().second);
}

void CompositeKeychainService::cleanupServiceAndKeyForRequestId(
    const QUuid & requestId)
{
    auto it = m_serviceAndKeyByRequestId.find(requestId);
    if (it != m_serviceAndKeyByRequestId.end()) {
        m_serviceAndKeyByRequestId.erase(it);
    }
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
    int size = settings.beginReadArray(keys::serviceKeyPair);
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);

        QString service = settings.value(keys::service).toString();
        QString key = settings.value(keys::service).toString();

        cache[service].insert(key);
    }

    settings.endArray();
    settings.endGroup();

    return cache;
}

} // namespace quentier
