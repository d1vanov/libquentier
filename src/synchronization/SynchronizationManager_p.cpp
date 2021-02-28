/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "SynchronizationManager_p.h"

#include "NoteStore.h"
#include "SyncStateStorage.h"
#include "SynchronizationShared.h"
#include "UserStore.h"

#include "../utility/keychain/QtKeychainService.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/DateTime.h>

#include <quentier/synchronization/IAuthenticationManager.h>
#include <quentier/synchronization/INoteStore.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/synchronization/IUserStore.h>

#if LIB_QUENTIER_HAS_AUTHENTICATION_MANAGER
#include <quentier/synchronization/AuthenticationManager.h>
#endif

#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/Printable.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/StandardPaths.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QTimeZone>

#include <limits>

namespace quentier {

////////////////////////////////////////////////////////////////////////////////

class SynchronizationManagerPrivate::
    RemoteToLocalSynchronizationManagerController final :
    public RemoteToLocalSynchronizationManager::IManager
{
public:
    RemoteToLocalSynchronizationManagerController(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManagerPrivate & syncManager);

    virtual LocalStorageManagerAsync & localStorageManagerAsync() override;
    virtual INoteStore & noteStore() override;
    virtual IUserStore & userStore() override;

    virtual INoteStore * noteStoreForLinkedNotebook(
        const LinkedNotebook & linkedNotebook) override;

private:
    LocalStorageManagerAsync & m_localStorageManagerAsync;
    SynchronizationManagerPrivate & m_syncManager;
};

////////////////////////////////////////////////////////////////////////////////

class SynchronizationManagerPrivate::SendLocalChangesManagerController final :
    public SendLocalChangesManager::IManager
{
public:
    SendLocalChangesManagerController(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManagerPrivate & syncManager);

    virtual LocalStorageManagerAsync & localStorageManagerAsync() override;
    virtual INoteStore & noteStore() override;

    virtual INoteStore * noteStoreForLinkedNotebook(
        const LinkedNotebook & linkedNotebook) override;

private:
    LocalStorageManagerAsync & m_localStorageManagerAsync;
    SynchronizationManagerPrivate & m_syncManager;
};

////////////////////////////////////////////////////////////////////////////////

SynchronizationManagerPrivate::SynchronizationManagerPrivate(
    QString host, LocalStorageManagerAsync & localStorageManagerAsync,
    IAuthenticationManager & authenticationManager, QObject * parent,
    INoteStorePtr pNoteStore, IUserStorePtr pUserStore,
    IKeychainServicePtr pKeychainService,
    ISyncStateStoragePtr pSyncStateStorage) :
    QObject(parent),
    m_host(std::move(host)), m_pSyncStateStorage(std::move(pSyncStateStorage)),
    m_pNoteStore(std::move(pNoteStore)), m_pUserStore(std::move(pUserStore)),
    m_pRemoteToLocalSyncManagerController(
        new RemoteToLocalSynchronizationManagerController(
            localStorageManagerAsync, *this)),
    m_pRemoteToLocalSyncManager(new RemoteToLocalSynchronizationManager(
        *m_pRemoteToLocalSyncManagerController, m_host, this)),
    m_pSendLocalChangesManagerController(
        new SendLocalChangesManagerController(localStorageManagerAsync, *this)),
    m_pSendLocalChangesManager(new SendLocalChangesManager(
        *m_pSendLocalChangesManagerController, this)),
    m_pKeychainService(std::move(pKeychainService))
{
    m_OAuthResult.m_userId = -1;

    if (!m_pNoteStore) {
        m_pNoteStore = newNoteStore(this);
    }
    else {
        m_pNoteStore->setParent(this);
    }

    if (!m_pUserStore) {
        m_pUserStore = newUserStore(
            QStringLiteral("https://") + m_host + QStringLiteral("/edam/user"));
    }

    if (!m_pKeychainService) {
        m_pKeychainService = newQtKeychainService(this);
    }
    else {
        m_pKeychainService->setParent(this);
    }

    if (!m_pSyncStateStorage) {
        m_pSyncStateStorage = newSyncStateStorage(this);
    }
    else {
        m_pSyncStateStorage->setParent(this);
    }

    createConnections(authenticationManager);
}

SynchronizationManagerPrivate::~SynchronizationManagerPrivate() {}

bool SynchronizationManagerPrivate::active() const
{
    return m_pRemoteToLocalSyncManager->active() ||
        m_pSendLocalChangesManager->active();
}

bool SynchronizationManagerPrivate::downloadNoteThumbnailsOption() const
{
    return m_pRemoteToLocalSyncManager->shouldDownloadThumbnailsForNotes();
}

void SynchronizationManagerPrivate::setAccount(const Account & account)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::setAccount: " << account);

    Account currentAccount = m_pRemoteToLocalSyncManager->account();
    if (currentAccount == account) {
        QNDEBUG(
            "synchronization",
            "The same account is already set, nothing "
                << "to do");
        return;
    }

    clear();

    m_OAuthResult = AuthData();
    m_OAuthResult.m_userId = -1;

    if (account.type() == Account::Type::Local) {
        return;
    }

    m_OAuthResult.m_userId = account.id();
    m_pRemoteToLocalSyncManager->setAccount(account);
    // NOTE: send local changes manager doesn't have any use for the account
}

void SynchronizationManagerPrivate::synchronize()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::synchronize");

    bool writingAuthToken = isWritingAuthToken(m_OAuthResult.m_userId);
    bool writingShardId = isWritingShardId(m_OAuthResult.m_userId);

    if (m_authenticationInProgress || writingAuthToken || writingShardId) {
        ErrorString error(
            QT_TR_NOOP("Authentication is not finished yet, please wait"));

        QNDEBUG(
            "synchronization",
            error << ", authentication in progress = "
                  << (m_authenticationInProgress ? "true" : "false")
                  << ", writing OAuth token = "
                  << (writingAuthToken ? "true" : "false")
                  << ", writing shard id = "
                  << (writingShardId ? "true" : "false"));

        Q_EMIT notifyError(error);
        return;
    }

    clear();
    authenticateImpl(AuthContext::SyncLaunch);
}

void SynchronizationManagerPrivate::authenticate()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::authenticate");

    bool writingAuthToken = isWritingAuthToken(m_OAuthResult.m_userId);
    bool writingShardId = isWritingShardId(m_OAuthResult.m_userId);

    if (m_authenticationInProgress || writingAuthToken || writingShardId) {
        ErrorString error(QT_TR_NOOP(
            "Previous authentication is not finished yet, please wait"));

        QNDEBUG(
            "synchronization",
            error << ", authentication in progress = "
                  << (m_authenticationInProgress ? "true" : "false")
                  << ", writing OAuth token = "
                  << (writingAuthToken ? "true" : "false")
                  << ", writing shard id = "
                  << (writingShardId ? "true" : "false"));

        Q_EMIT authenticationFinished(/* success = */ false, error, Account());
        return;
    }

    authenticateImpl(AuthContext::NewUserRequest);
}

void SynchronizationManagerPrivate::authenticateCurrentAccount()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::authenticateCurrentAccount");

    bool writingAuthToken = isWritingAuthToken(m_OAuthResult.m_userId);
    bool writingShardId = isWritingShardId(m_OAuthResult.m_userId);

    if (m_authenticationInProgress || writingAuthToken || writingShardId) {
        ErrorString error(
            QT_TR_NOOP("Previous authentication is not finished yet, please "
                       "wait"));

        QNDEBUG(
            "synchronization",
            error << ", authentication in progress = "
                  << (m_authenticationInProgress ? "true" : "false")
                  << ", writing OAuth token = "
                  << (writingAuthToken ? "true" : "false")
                  << ", writing shard id = "
                  << (writingShardId ? "true" : "false"));

        Q_EMIT authenticationFinished(/* success = */ false, error, Account());
        return;
    }

    authenticateImpl(AuthContext::CurrentUserRequest);
}

void SynchronizationManagerPrivate::stop()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::stop");

    tryUpdateLastSyncStatus();

    m_pNoteStore->stop();

    for (auto it = m_noteStoresByLinkedNotebookGuids.begin(),
              end = m_noteStoresByLinkedNotebookGuids.end();
         it != end; ++it)
    {
        INoteStore * pNoteStore = it.value();
        pNoteStore->stop();
    }

    if (!m_pRemoteToLocalSyncManager->active() &&
        !m_pSendLocalChangesManager->active())
    {
        Q_EMIT notifyStop();
    }

    m_pRemoteToLocalSyncManager->stop();
    m_pSendLocalChangesManager->stop();
}

void SynchronizationManagerPrivate::revokeAuthentication(
    const qevercloud::UserID userId)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::revokeAuthentication: user id = "
            << userId);

    QString deleteAuthTokenService =
        QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;

    QString deleteAuthTokenKey = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(userId);

    auto deleteAuthTokenJobId = m_pKeychainService->startDeletePasswordJob(
        deleteAuthTokenService, deleteAuthTokenKey);

    m_deleteAuthTokenJobIdsWithUserIds.insert(
        KeychainJobIdWithUserId::value_type(userId, deleteAuthTokenJobId));

    QString deleteShardIdService =
        QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;

    QString deleteShardIdKey = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(userId);

    auto deleteShardIdJobId = m_pKeychainService->startDeletePasswordJob(
        deleteShardIdService, deleteShardIdKey);

    m_deleteShardIdJobIdsWithUserIds.insert(
        KeychainJobIdWithUserId::value_type(userId, deleteShardIdJobId));
}

void SynchronizationManagerPrivate::setDownloadNoteThumbnails(const bool flag)
{
    m_pRemoteToLocalSyncManager->setDownloadNoteThumbnails(flag);
}

void SynchronizationManagerPrivate::setDownloadInkNoteImages(const bool flag)
{
    m_pRemoteToLocalSyncManager->setDownloadInkNoteImages(flag);
}

void SynchronizationManagerPrivate::setInkNoteImagesStoragePath(
    const QString & path)
{
    m_pRemoteToLocalSyncManager->setInkNoteImagesStoragePath(path);
}

void SynchronizationManagerPrivate::onOAuthResult(
    bool success, qevercloud::UserID userId, QString authToken,
    qevercloud::Timestamp authTokenExpirationTime, QString shardId,
    QString noteStoreUrl, QString webApiUrlPrefix,
    QList<QNetworkCookie> cookies, ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onOAuthResult: "
            << (success ? "success" : "failure") << ", user id = " << userId
            << ", auth token expiration time = "
            << printableDateTimeFromTimestamp(authTokenExpirationTime)
            << ", error: " << errorDescription);

    m_authenticationInProgress = false;

    if (!success) {
        if ((m_authContext == AuthContext::NewUserRequest) ||
            (m_authContext == AuthContext::CurrentUserRequest))
        {
            Q_EMIT authenticationFinished(
                /* success = */ false, errorDescription, Account());
        }
        else {
            Q_EMIT notifyError(errorDescription);
        }

        return;
    }

    AuthData authData;
    authData.m_userId = userId;
    authData.m_authToken = authToken;
    authData.m_expirationTime = authTokenExpirationTime;
    authData.m_shardId = shardId;
    authData.m_noteStoreUrl = noteStoreUrl;
    authData.m_webApiUrlPrefix = webApiUrlPrefix;
    authData.m_cookies = std::move(cookies);

    authData.m_authenticationTime =
        static_cast<qevercloud::Timestamp>(QDateTime::currentMSecsSinceEpoch());

    m_OAuthResult = authData;
    QNDEBUG("synchronization", "OAuth result = " << m_OAuthResult);

    Account previousAccount = m_pRemoteToLocalSyncManager->account();

    Account newAccount(
        QString(), Account::Type::Evernote, userId,
        Account::EvernoteAccountType::Free, m_host);

    m_pRemoteToLocalSyncManager->setAccount(newAccount);
    m_pUserStore->setAuthData(authToken, m_OAuthResult.m_cookies);

    ErrorString error;
    bool res = m_pRemoteToLocalSyncManager->syncUser(
        userId, error,
        /* write user data to * local storage = */ false);

    if (Q_UNLIKELY(!res)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't switch to new Evernote "
                       "account: failed to sync user data"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("synchronization", errorDescription);
        Q_EMIT notifyError(errorDescription);

        m_pRemoteToLocalSyncManager->setAccount(previousAccount);

        return;
    }

    const User & user = m_pRemoteToLocalSyncManager->user();
    if (Q_UNLIKELY(!user.hasUsername())) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't switch to new Evernote account: the synched "
                       "user data lacks username"));
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("synchronization", errorDescription);
        Q_EMIT notifyError(errorDescription);

        m_pRemoteToLocalSyncManager->setAccount(previousAccount);

        return;
    }

    launchStoreOAuthResult(authData);
}

void SynchronizationManagerPrivate::onWritePasswordJobFinished(
    QUuid jobId, IKeychainService::ErrorCode errorCode,
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onWritePasswordJobFinished: job id = "
            << jobId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    {
        auto it = m_writeAuthTokenJobIdsWithUserIds.right.find(jobId);
        if (it != m_writeAuthTokenJobIdsWithUserIds.right.end()) {
            // TODO: make use of userId from the bimap
            m_writeAuthTokenJobIdsWithUserIds.right.erase(it);
            onWriteAuthTokenFinished(errorCode, errorDescription);
            return;
        }
    }

    {
        auto it = m_writeShardIdJobIdsWithUserIds.right.find(jobId);
        if (it != m_writeShardIdJobIdsWithUserIds.right.end()) {
            // TODO: make use of userId from the bimap
            m_writeShardIdJobIdsWithUserIds.right.erase(it);
            onWriteShardIdFinished(errorCode, errorDescription);
            return;
        }
    }

    auto writeAuthTokenIt =
        m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.find(
            jobId);
    if (writeAuthTokenIt !=
        m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(
            "synchronization",
            "Write linked notebook auth token job "
                << "finished: linked notebook guid = "
                << writeAuthTokenIt->second);

        QString guid = writeAuthTokenIt->second;
        Q_UNUSED(m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids
                     .right.erase(writeAuthTokenIt))

        auto pendingItemIt =
            m_linkedNotebookAuthTokensPendingWritingByGuid.find(guid);
        if (pendingItemIt !=
            m_linkedNotebookAuthTokensPendingWritingByGuid.end()) {
            // NOTE: ignore the status of previous write job for this key,
            // it doesn't matter if we need to write another token
            QString token = pendingItemIt.value();

            Q_UNUSED(m_linkedNotebookAuthTokensPendingWritingByGuid.erase(
                pendingItemIt))

            QNDEBUG(
                "synchronization",
                "Writing postponed auth token for "
                    << "linked notebook guid " << guid);

            QString keyPrefix = QCoreApplication::applicationName() +
                QStringLiteral("_") + m_host + QStringLiteral("_") +
                QString::number(m_OAuthResult.m_userId);

            QUuid jobId = m_pKeychainService->startWritePasswordJob(
                WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB,
                keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid, token);

            m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.insert(
                KeychainJobIdWithGuidBimap::value_type(guid, jobId));
        }
        else if (errorCode != IKeychainService::ErrorCode::NoError) {
            ErrorString error(
                QT_TR_NOOP("Error saving linked notebook's "
                           "authentication token to the keychain"));
            error.appendBase(errorDescription.base());
            error.appendBase(errorDescription.additionalBases());
            error.details() = QStringLiteral("error code = ");
            error.details() += ToString(errorCode);

            const QString & errorDetails = errorDescription.details();
            if (!errorDetails.isEmpty()) {
                error.details() += QStringLiteral(": ");
                error.details() += errorDetails;
            }

            QNWARNING("synchronization", error);
            Q_EMIT notifyError(error);
        }

        return;
    }

    auto writeShardIdIt =
        m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.find(
            jobId);

    if (writeShardIdIt !=
        m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(
            "synchronization",
            "Write linked notebook shard id job "
                << "finished: linked notebook guid = "
                << writeShardIdIt->second);

        QString guid = writeShardIdIt->second;

        Q_UNUSED(m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right
                     .erase(writeShardIdIt))

        auto pendingItemIt =
            m_linkedNotebookShardIdsPendingWritingByGuid.find(guid);

        if (pendingItemIt != m_linkedNotebookShardIdsPendingWritingByGuid.end())
        {
            // NOTE: ignore the status of previous write job for this key,
            // it doesn't matter if we need to write another shard id
            QString shardId = pendingItemIt.value();

            Q_UNUSED(m_linkedNotebookShardIdsPendingWritingByGuid.erase(
                pendingItemIt))

            QNDEBUG(
                "synchronization",
                "Writing postponed shard id "
                    << shardId << " for linked notebook guid " << guid);

            QString keyPrefix = QCoreApplication::applicationName() +
                QStringLiteral("_") + m_host + QStringLiteral("_") +
                QString::number(m_OAuthResult.m_userId);

            QUuid jobId = m_pKeychainService->startWritePasswordJob(
                WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB,
                keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid, shardId);

            m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.insert(
                KeychainJobIdWithGuidBimap::value_type(guid, jobId));
        }
        else if (errorCode != IKeychainService::ErrorCode::NoError) {
            ErrorString error(
                QT_TR_NOOP("Error saving linked notebook's "
                           "shard id to the keychain"));
            error.appendBase(errorDescription.base());
            error.appendBase(errorDescription.additionalBases());
            error.details() = QStringLiteral("error code = ");
            error.details() += ToString(errorCode);

            const QString & errorDetails = errorDescription.details();
            if (!errorDetails.isEmpty()) {
                error.details() += QStringLiteral(": ");
                error.details() += errorDetails;
            }

            QNWARNING("synchronization", error);
            Q_EMIT notifyError(error);
        }

        return;
    }

    QNDEBUG(
        "synchronization",
        "Couldn't identify the write password from "
            << "keychain job");
}

void SynchronizationManagerPrivate::onReadPasswordJobFinished(
    QUuid jobId, IKeychainService::ErrorCode errorCode,
    ErrorString errorDescription, QString password)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onReadPasswordJobFinished: job id = "
            << jobId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    {
        auto it = m_readAuthTokenJobIdsWithUserIds.right.find(jobId);
        if (it != m_readAuthTokenJobIdsWithUserIds.right.end()) {
            // TODO: make use of userId from the bimap
            m_readAuthTokenJobIdsWithUserIds.right.erase(it);
            onReadAuthTokenFinished(errorCode, errorDescription, password);
            return;
        }
    }

    {
        auto it = m_readShardIdJobIdsWithUserIds.right.find(jobId);
        if (it != m_readShardIdJobIdsWithUserIds.right.end()) {
            // TODO: make use of userId from the bimap
            m_readShardIdJobIdsWithUserIds.right.erase(it);
            onReadShardIdFinished(errorCode, errorDescription, password);
            return;
        }
    }

    auto readAuthTokenIt =
        m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.find(
            jobId);

    if (readAuthTokenIt !=
        m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(
            "synchronization",
            "Read linked notebook auth token job "
                << "finished: linked notebook guid = "
                << readAuthTokenIt->second);

        if (errorCode == IKeychainService::ErrorCode::NoError) {
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[readAuthTokenIt
                                                                  ->second]
                .first = password;
        }
        else if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
                readAuthTokenIt->second))
        }
        else {
            QNWARNING(
                "synchronization",
                "Failed to read linked notebook's "
                    << "authentication token from the keychain: error code = "
                    << errorCode
                    << ", error description: " << errorDescription);

            /**
             * Try to recover by making user to authenticate again in the blind
             * hope that the next time the persistence of auth settings in the
             * keychain would work
             */
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
                readAuthTokenIt->second))
        }

        Q_UNUSED(m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids
                     .right.erase(readAuthTokenIt))

        if (m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids
                .empty() &&
            m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.empty())
        {
            QNDEBUG(
                "synchronization",
                "No pending read linked notebook auth "
                    << "token or shard id job");
            authenticateToLinkedNotebooks();
        }

        return;
    }

    auto readShardIdIt =
        m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.find(
            jobId);

    if (readShardIdIt !=
        m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(
            "synchronization",
            "Read linked notebook shard id job "
                << "finished: linked notebook guid = "
                << readShardIdIt->second);

        if (errorCode == IKeychainService::ErrorCode::NoError) {
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[readAuthTokenIt
                                                                  ->second]
                .second = password;
        }
        else if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
                readShardIdIt->second))
        }
        else {
            QNWARNING(
                "synchronization",
                "Failed to read linked notebook's "
                    << "authentication token from the keychain: error code = "
                    << errorCode
                    << ", error description: " << errorDescription);

            /**
             * Try to recover by making user to authenticate again in the blind
             * hope that the next time the persistence of auth settings in the
             * keychain would work
             */
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
                readShardIdIt->second))
        }

        Q_UNUSED(m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right
                     .erase(readShardIdIt))

        if (m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.empty() &&
            m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.empty())
        {
            QNDEBUG(
                "synchronization",
                "No pending read linked notebook auth "
                    << "token or shard id job");
            authenticateToLinkedNotebooks();
        }

        return;
    }
}

void SynchronizationManagerPrivate::onDeletePasswordJobFinished(
    QUuid jobId, IKeychainService::ErrorCode errorCode,
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onDeletePasswordJobFinished: job id = "
            << jobId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    {
        auto it = m_deleteAuthTokenJobIdsWithUserIds.right.find(jobId);
        if (it != m_deleteAuthTokenJobIdsWithUserIds.right.end()) {
            auto userId = it->second;
            m_deleteAuthTokenJobIdsWithUserIds.right.erase(it);
            onDeleteAuthTokenFinished(errorCode, userId, errorDescription);
            return;
        }
    }

    {
        auto it = m_deleteShardIdJobIdsWithUserIds.right.find(jobId);
        if (it != m_deleteShardIdJobIdsWithUserIds.right.end()) {
            auto userId = it->second;
            m_deleteShardIdJobIdsWithUserIds.right.erase(it);
            onDeleteShardIdFinished(errorCode, userId, errorDescription);
            return;
        }
    }

    QNDEBUG(
        "synchronization",
        "Couldn't identify the delete password from "
            << "keychain job");
}

void SynchronizationManagerPrivate::onRequestAuthenticationToken()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onRequestAuthenticationToken");

    if (validAuthentication()) {
        QNDEBUG(
            "synchronization",
            "Found valid auth token and shard id, "
                << "returning them");

        Q_EMIT sendAuthenticationTokenAndShardId(
            m_OAuthResult.m_authToken, m_OAuthResult.m_shardId,
            m_OAuthResult.m_expirationTime);

        return;
    }

    authenticateImpl(AuthContext::SyncLaunch);
}

void SynchronizationManagerPrivate::
    onRequestAuthenticationTokensForLinkedNotebooks(
        QVector<LinkedNotebookAuthData> linkedNotebookAuthData)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate"
            << "::onRequestAuthenticationTokensForLinkedNotebooks");

    m_linkedNotebookAuthDataPendingAuthentication = linkedNotebookAuthData;
    authenticateToLinkedNotebooks();
}

void SynchronizationManagerPrivate::onRequestLastSyncParameters()
{
    if (m_onceReadLastSyncParams) {
        Q_EMIT sendLastSyncParameters(
            m_lastUpdateCount, m_lastSyncTime,
            m_cachedLinkedNotebookLastUpdateCountByGuid,
            m_cachedLinkedNotebookLastSyncTimeByGuid);
        return;
    }

    readLastSyncParameters();

    Q_EMIT sendLastSyncParameters(
        m_lastUpdateCount, m_lastSyncTime,
        m_cachedLinkedNotebookLastUpdateCountByGuid,
        m_cachedLinkedNotebookLastSyncTimeByGuid);
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncFinished(
    qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
    QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid,
    QHash<QString, qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onRemoteToLocalSyncFinished: "
            << "lastUpdateCount = " << lastUpdateCount << ", lastSyncTime = "
            << printableDateTimeFromTimestamp(lastSyncTime));

    bool somethingDownloaded = (m_lastUpdateCount != lastUpdateCount) ||
        (m_lastUpdateCount != m_previousUpdateCount) ||
        (m_cachedLinkedNotebookLastUpdateCountByGuid !=
         lastUpdateCountByLinkedNotebookGuid);

    QNTRACE(
        "synchronization",
        "Something downloaded = "
            << (somethingDownloaded ? "true" : "false")
            << ", m_lastUpdateCount = " << m_lastUpdateCount
            << ", m_previousUpdateCount = " << m_previousUpdateCount
            << ", m_cachedLinkedNotebookLastUpdateCountByGuid = "
            << m_cachedLinkedNotebookLastUpdateCountByGuid);

    m_lastUpdateCount = lastUpdateCount;
    m_previousUpdateCount = lastUpdateCount;
    m_lastSyncTime = lastSyncTime;

    m_cachedLinkedNotebookLastUpdateCountByGuid =
        lastUpdateCountByLinkedNotebookGuid;

    m_cachedLinkedNotebookLastSyncTimeByGuid = lastSyncTimeByLinkedNotebookGuid;

    updatePersistentSyncSettings();

    m_onceReadLastSyncParams = true;
    m_somethingDownloaded = somethingDownloaded;
    Q_EMIT notifyRemoteToLocalSyncDone(m_somethingDownloaded);

    sendChanges();
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncStopped()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onRemoteToLocalSyncStopped");

    Q_EMIT remoteToLocalSyncStopped();

    if (!m_pSendLocalChangesManager->active()) {
        Q_EMIT notifyStop();
    }
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncFailure(
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onRemoteToLocalSyncFailure: "
            << errorDescription);

    Q_EMIT notifyError(errorDescription);
    stop();
}

void SynchronizationManagerPrivate::
    onRemoteToLocalSynchronizedContentFromUsersOwnAccount(
        qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate"
            << "::onRemoteToLocalSynchronizedContentFromUsersOwnAccount: "
            << "last update count = " << lastUpdateCount
            << ", last sync time = "
            << printableDateTimeFromTimestamp(lastSyncTime));

    m_lastUpdateCount = lastUpdateCount;
    m_lastSyncTime = lastSyncTime;

    updatePersistentSyncSettings();
}

void SynchronizationManagerPrivate::onShouldRepeatIncrementalSync()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onShouldRepeatIncrementalSync");

    m_shouldRepeatIncrementalSyncAfterSendingChanges = true;
    Q_EMIT willRepeatRemoteToLocalSyncAfterSendingChanges();
}

void SynchronizationManagerPrivate::
    onConflictDetectedDuringLocalChangesSending()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate"
            << "::onConflictDetectedDuringLocalChangesSending");

    Q_EMIT detectedConflictDuringLocalChangesSending();

    m_pSendLocalChangesManager->stop();

    /**
     * NOTE: the detection of non-synchronized state with respect to remote
     * service often precedes the actual conflict detection; need to drop this
     * flag to prevent launching the incremental sync after sending the local
     * changes after the incremental sync which we'd launch now
     */
    m_shouldRepeatIncrementalSyncAfterSendingChanges = false;

    launchIncrementalSync();
}

void SynchronizationManagerPrivate::onLocalChangesSent(
    qint32 lastUpdateCount,
    QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onLocalChangesSent: "
            << "last update count = " << lastUpdateCount
            << ", last update count per linked notebook guid: "
            << lastUpdateCountByLinkedNotebookGuid);

    bool somethingSent = (m_lastUpdateCount != lastUpdateCount) ||
        (m_cachedLinkedNotebookLastUpdateCountByGuid !=
         lastUpdateCountByLinkedNotebookGuid);

    m_lastUpdateCount = lastUpdateCount;

    m_cachedLinkedNotebookLastUpdateCountByGuid =
        lastUpdateCountByLinkedNotebookGuid;

    updatePersistentSyncSettings();

    if (m_shouldRepeatIncrementalSyncAfterSendingChanges) {
        QNDEBUG(
            "synchronization",
            "Repeating the incremental sync after "
                << "sending the changes");
        m_shouldRepeatIncrementalSyncAfterSendingChanges = false;
        launchIncrementalSync();
        return;
    }

    QNINFO("synchronization", "Finished the whole synchronization procedure!");

    bool somethingDownloaded = m_somethingDownloaded;
    m_somethingDownloaded = false;

    Q_EMIT notifyFinish(
        m_pRemoteToLocalSyncManager->account(), somethingDownloaded,
        somethingSent);
}

void SynchronizationManagerPrivate::onSendLocalChangesStopped()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onSendLocalChangesStopped");

    Q_EMIT sendLocalChangesStopped();

    if (!m_pRemoteToLocalSyncManager->active()) {
        Q_EMIT notifyStop();
    }
}

void SynchronizationManagerPrivate::onSendLocalChangesFailure(
    ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onSendLocalChangesFailure: "
            << errorDescription);

    stop();
    Q_EMIT notifyError(errorDescription);
}

void SynchronizationManagerPrivate::onRateLimitExceeded(qint32 secondsToWait)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onRateLimitExceeded");

    /**
     * Before re-sending this signal to the outside world will attempt to
     * collect the update sequence numbers for the next sync, either for user's
     * own account or for each linked notebook - depending on what has been
     * synced right before the Evernote API rate limit was exceeded. The
     * collected update sequence numbers would be used to update the persistent
     * sync settings. So that if the sync ends now before it's automatically
     * restarted after the required waiting time (for example, user quits the
     * app now), the next time we'll request the sync chunks after the last
     * properly processed USN, either for user's own account or for linked
     * notebooks, so that we won't re-download the same stuff over and over
     * again and hit the rate limit at the very same sync stage
     */

    tryUpdateLastSyncStatus();
    Q_EMIT rateLimitExceeded(secondsToWait);
}

void SynchronizationManagerPrivate::createConnections(
    IAuthenticationManager & authenticationManager)
{
    // Connections with authentication manager
    QObject::connect(
        this, &SynchronizationManagerPrivate::requestAuthentication,
        &authenticationManager,
        &IAuthenticationManager::onAuthenticationRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &authenticationManager,
        &IAuthenticationManager::sendAuthenticationResult, this,
        &SynchronizationManagerPrivate::onOAuthResult,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connections with keychain service
    QObject::connect(
        m_pKeychainService.get(), &IKeychainService::writePasswordJobFinished,
        this, &SynchronizationManagerPrivate::onWritePasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pKeychainService.get(), &IKeychainService::readPasswordJobFinished,
        this, &SynchronizationManagerPrivate::onReadPasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pKeychainService.get(), &IKeychainService::deletePasswordJobFinished,
        this, &SynchronizationManagerPrivate::onDeletePasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connections with remote to local synchronization manager
    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::finished, this,
        &SynchronizationManagerPrivate::onRemoteToLocalSyncFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::rateLimitExceeded, this,
        &SynchronizationManagerPrivate::onRateLimitExceeded,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::DirectConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::requestAuthenticationToken, this,
        &SynchronizationManagerPrivate::onRequestAuthenticationToken,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::
            requestAuthenticationTokensForLinkedNotebooks,
        this,
        &SynchronizationManagerPrivate::
            onRequestAuthenticationTokensForLinkedNotebooks,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::stopped, this,
        &SynchronizationManagerPrivate::onRemoteToLocalSyncStopped,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::failure, this,
        &SynchronizationManagerPrivate::onRemoteToLocalSyncFailure,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::
            synchronizedContentFromUsersOwnAccount,
        this,
        &SynchronizationManagerPrivate::
            onRemoteToLocalSynchronizedContentFromUsersOwnAccount,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::requestLastSyncParameters, this,
        &SynchronizationManagerPrivate::onRequestLastSyncParameters,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::syncChunksDownloadProgress, this,
        &SynchronizationManagerPrivate::syncChunksDownloadProgress,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::syncChunksDownloaded, this,
        &SynchronizationManagerPrivate::syncChunksDownloaded,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::notesDownloadProgress, this,
        &SynchronizationManagerPrivate::notesDownloadProgress,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::
            linkedNotebookSyncChunksDownloadProgress,
        this,
        &SynchronizationManagerPrivate::
            linkedNotebookSyncChunksDownloadProgress,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::
            linkedNotebooksSyncChunksDownloaded,
        this,
        &SynchronizationManagerPrivate::linkedNotebooksSyncChunksDownloaded,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::resourcesDownloadProgress, this,
        &SynchronizationManagerPrivate::resourcesDownloadProgress,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::
            linkedNotebooksResourcesDownloadProgress,
        this,
        &SynchronizationManagerPrivate::
            linkedNotebooksResourcesDownloadProgress,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::
            linkedNotebooksNotesDownloadProgress,
        this,
        &SynchronizationManagerPrivate::linkedNotebooksNotesDownloadProgress,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &SynchronizationManagerPrivate::sendAuthenticationTokenAndShardId,
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::onAuthenticationInfoReceived,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this,
        &SynchronizationManagerPrivate::
            sendAuthenticationTokensForLinkedNotebooks,
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::
            onAuthenticationTokensForLinkedNotebooksReceived,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &SynchronizationManagerPrivate::sendLastSyncParameters,
        m_pRemoteToLocalSyncManager,
        &RemoteToLocalSynchronizationManager::onLastSyncParametersReceived,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connections with send local changes manager
    QObject::connect(
        m_pSendLocalChangesManager, &SendLocalChangesManager::finished, this,
        &SynchronizationManagerPrivate::onLocalChangesSent,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager, &SendLocalChangesManager::rateLimitExceeded,
        this, &SynchronizationManagerPrivate::onRateLimitExceeded,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::DirectConnection));

    QObject::connect(
        m_pSendLocalChangesManager,
        &SendLocalChangesManager::requestAuthenticationToken, this,
        &SynchronizationManagerPrivate::onRequestAuthenticationToken,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager,
        &SendLocalChangesManager::requestAuthenticationTokensForLinkedNotebooks,
        this,
        &SynchronizationManagerPrivate::
            onRequestAuthenticationTokensForLinkedNotebooks,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager,
        &SendLocalChangesManager::shouldRepeatIncrementalSync, this,
        &SynchronizationManagerPrivate::onShouldRepeatIncrementalSync,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager, &SendLocalChangesManager::conflictDetected,
        this,
        &SynchronizationManagerPrivate::
            onConflictDetectedDuringLocalChangesSending,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager, &SendLocalChangesManager::stopped, this,
        &SynchronizationManagerPrivate::onSendLocalChangesStopped,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager, &SendLocalChangesManager::failure, this,
        &SynchronizationManagerPrivate::onSendLocalChangesFailure,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager,
        &SendLocalChangesManager::receivedUserAccountDirtyObjects, this,
        &SynchronizationManagerPrivate::preparedDirtyObjectsForSending,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pSendLocalChangesManager,
        &SendLocalChangesManager::receivedDirtyObjectsFromLinkedNotebooks, this,
        &SynchronizationManagerPrivate::
            preparedLinkedNotebooksDirtyObjectsForSending,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this,
        &SynchronizationManagerPrivate::
            sendAuthenticationTokensForLinkedNotebooks,
        m_pSendLocalChangesManager,
        &SendLocalChangesManager::
            onAuthenticationTokensForLinkedNotebooksReceived,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

void SynchronizationManagerPrivate::readLastSyncParameters()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::readLastSyncParameters");

    auto syncState = m_pSyncStateStorage->getSyncState(
        m_pRemoteToLocalSyncManager->account());

    m_lastUpdateCount = syncState->userDataUpdateCount();
    m_lastSyncTime = syncState->userDataLastSyncTime();

    m_cachedLinkedNotebookLastUpdateCountByGuid =
        syncState->linkedNotebookUpdateCounts();

    m_cachedLinkedNotebookLastSyncTimeByGuid =
        syncState->linkedNotebookLastSyncTimes();

    m_previousUpdateCount = m_lastUpdateCount;
    m_onceReadLastSyncParams = true;
}

void SynchronizationManagerPrivate::authenticateImpl(
    const AuthContext authContext)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::authenticateImpl: auth context = "
            << authContext);

    m_authContext = authContext;

    if (m_authContext == AuthContext::NewUserRequest) {
        QNDEBUG(
            "synchronization",
            "Authentication of the new user is "
                << "requested, proceeding to OAuth");
        launchOAuth();
        return;
    }

    if (m_OAuthResult.m_userId < 0) {
        QNDEBUG(
            "synchronization",
            "No current user id, launching the OAuth "
                << "procedure");
        launchOAuth();
        return;
    }

    if (validAuthentication()) {
        QNDEBUG("synchronization", "Found already valid authentication info");
        finalizeAuthentication();
        return;
    }

    QNTRACE(
        "synchronization",
        "Trying to restore persistent authentication "
            << "settings...");

    ApplicationSettings appSettings(
        m_pRemoteToLocalSyncManager->account(),
        SYNCHRONIZATION_PERSISTENCE_NAME);

    QString keyGroup = QStringLiteral("Authentication/") + m_host +
        QStringLiteral("/") + QString::number(m_OAuthResult.m_userId) +
        QStringLiteral("/");

    QVariant authenticationTimestamp =
        appSettings.value(keyGroup + AUTHENTICATION_TIMESTAMP_KEY);

    QDateTime authenticationDateTime;
    if (!authenticationTimestamp.isNull()) {
        bool conversionResult = false;

        qint64 authenticationTimestampInt =
            authenticationTimestamp.toLongLong(&conversionResult);

        if (conversionResult) {
            authenticationDateTime =
                QDateTime::fromMSecsSinceEpoch(authenticationTimestampInt);
        }
    }

    if (!authenticationDateTime.isValid()) {
        QNINFO(
            "synchronization",
            "Authentication datetime was not found "
                << "within application settings, assuming it has never been "
                << "written & launching the OAuth procedure");
        launchOAuth();
        return;
    }

    if (authenticationDateTime <
        QDateTime(QDate(2020, 4, 22), QTime(0, 0), QTimeZone::utc()))
    {
        QNINFO(
            "synchronization",
            "Last authentication was performed before "
                << "Evernote introduced a bug which requires to set a "
                   "particular "
                << "cookie into API calls which was received during OAuth. "
                   "Forcing "
                << "new OAuth");
        launchOAuth();
        return;
    }

    m_OAuthResult.m_authenticationTime = static_cast<qevercloud::Timestamp>(
        authenticationDateTime.toMSecsSinceEpoch());

    QVariant tokenExpirationValue =
        appSettings.value(keyGroup + EXPIRATION_TIMESTAMP_KEY);

    if (tokenExpirationValue.isNull()) {
        QNINFO(
            "synchronization",
            "Authentication token expiration timestamp "
                << "was not found within application settings, assuming it has "
                << "never been written & launching the OAuth procedure");
        launchOAuth();
        return;
    }

    bool conversionResult = false;

    qevercloud::Timestamp tokenExpirationTimestamp =
        tokenExpirationValue.toLongLong(&conversionResult);

    if (!conversionResult) {
        ErrorString error(
            QT_TR_NOOP("Internal error: failed to convert QVariant "
                       "with authentication token expiration "
                       "timestamp to the actual timestamp"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (checkIfTimestampIsAboutToExpireSoon(tokenExpirationTimestamp)) {
        QNINFO(
            "synchronization",
            "Authentication token stored in persistent "
                << "application settings is about to expire soon "
                << "enough, launching the OAuth procedure");
        launchOAuth();
        return;
    }

    m_OAuthResult.m_expirationTime = tokenExpirationTimestamp;

    QNTRACE("synchronization", "Restoring persistent note store url");

    QVariant noteStoreUrlValue =
        appSettings.value(keyGroup + NOTE_STORE_URL_KEY);
    if (noteStoreUrlValue.isNull()) {
        ErrorString error(
            QT_TR_NOOP("Failed to find the note store url within "
                       "persistent application settings"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    QString noteStoreUrl = noteStoreUrlValue.toString();
    if (noteStoreUrl.isEmpty()) {
        ErrorString error(
            QT_TR_NOOP("Internal error: failed to convert the note "
                       "store url from QVariant to QString"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_OAuthResult.m_noteStoreUrl = noteStoreUrl;

    QNDEBUG("synchronization", "Restoring persistent web api url prefix");

    QVariant webApiUrlPrefixValue =
        appSettings.value(keyGroup + WEB_API_URL_PREFIX_KEY);
    if (webApiUrlPrefixValue.isNull()) {
        ErrorString error(
            QT_TR_NOOP("Failed to find the web API url prefix "
                       "within persistent application settings"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    QString webApiUrlPrefix = webApiUrlPrefixValue.toString();
    if (webApiUrlPrefix.isEmpty()) {
        ErrorString error(
            QT_TR_NOOP("Failed to convert the web api url prefix "
                       "from QVariant to QString"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_OAuthResult.m_webApiUrlPrefix = webApiUrlPrefix;

    QNDEBUG("synchronization", "Restoring cookie for UserStore");

    QByteArray rawCookie =
        appSettings.value(keyGroup + USER_STORE_COOKIE_KEY).toByteArray();

    m_OAuthResult.m_cookies = QNetworkCookie::parseCookies(rawCookie);

    QNDEBUG(
        "synchronization",
        "Trying to restore the authentication token and "
            << "the shard id from the keychain");

    QString readAuthTokenService =
        QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;

    QString readAuthTokenKey = QCoreApplication::applicationName() +
        QStringLiteral("_auth_token_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    auto readAuthTokenJobId = m_pKeychainService->startReadPasswordJob(
        readAuthTokenService, readAuthTokenKey);

    m_readAuthTokenJobIdsWithUserIds.insert(KeychainJobIdWithUserId::value_type(
        m_OAuthResult.m_userId, readAuthTokenJobId));

    QString readShardIdService =
        QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;

    QString readShardIdKey = QCoreApplication::applicationName() +
        QStringLiteral("_shard_id_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    auto readShardIdJobId = m_pKeychainService->startReadPasswordJob(
        readShardIdService, readShardIdKey);

    m_readShardIdJobIdsWithUserIds.insert(KeychainJobIdWithUserId::value_type(
        m_OAuthResult.m_userId, readShardIdJobId));
}

void SynchronizationManagerPrivate::launchOAuth()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::launchOAuth");

    if (m_authenticationInProgress) {
        QNDEBUG("synchronization", "Authentication is already in progress");
        return;
    }

    m_authenticationInProgress = true;
    Q_EMIT requestAuthentication();
}

void SynchronizationManagerPrivate::launchSync()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::launchSync");

    if (!m_onceReadLastSyncParams) {
        readLastSyncParameters();
    }

    Q_EMIT notifyStart();

    m_pNoteStore->setNoteStoreUrl(m_OAuthResult.m_noteStoreUrl);

    m_pNoteStore->setAuthData(
        m_OAuthResult.m_authToken, m_OAuthResult.m_cookies);

    m_pUserStore->setAuthData(
        m_OAuthResult.m_authToken, m_OAuthResult.m_cookies);

    if (m_lastUpdateCount <= 0) {
        QNDEBUG(
            "synchronization",
            "The client has never synchronized with "
                << "the remote service, performing the full sync");
        launchFullSync();
        return;
    }

    QNDEBUG("synchronization", "Performing incremental sync");
    launchIncrementalSync();
}

void SynchronizationManagerPrivate::launchFullSync()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::launchFullSync");

    m_somethingDownloaded = false;
    m_pRemoteToLocalSyncManager->start();
}

void SynchronizationManagerPrivate::launchIncrementalSync()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::launchIncrementalSync: "
            << "m_lastUpdateCount = " << m_lastUpdateCount);

    m_somethingDownloaded = false;
    m_pRemoteToLocalSyncManager->start(m_lastUpdateCount);
}

void SynchronizationManagerPrivate::sendChanges()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::sendChanges");

    m_pSendLocalChangesManager->start(
        m_lastUpdateCount, m_cachedLinkedNotebookLastUpdateCountByGuid);
}

void SynchronizationManagerPrivate::launchStoreOAuthResult(
    const AuthData & result)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::launchStoreOAuthResult");

    m_writtenOAuthResultByUserId[result.m_userId] = result;

    QString writeAuthTokenService =
        QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;

    QString writeAuthTokenKey = QCoreApplication::applicationName() +
        QStringLiteral("_auth_token_") + m_host + QStringLiteral("_") +
        QString::number(result.m_userId);

    auto writeAuthTokenJobId = m_pKeychainService->startWritePasswordJob(
        writeAuthTokenService, writeAuthTokenKey, result.m_authToken);

    m_writeAuthTokenJobIdsWithUserIds.insert(
        KeychainJobIdWithUserId::value_type(
            m_OAuthResult.m_userId, writeAuthTokenJobId));

    QString writeShardIdService =
        QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;

    QString writeShardIdKey = QCoreApplication::applicationName() +
        QStringLiteral("_shard_id_") + m_host + QStringLiteral("_") +
        QString::number(result.m_userId);

    auto writeShardIdJobId = m_pKeychainService->startWritePasswordJob(
        writeShardIdService, writeShardIdKey, result.m_shardId);

    m_writeShardIdJobIdsWithUserIds.insert(KeychainJobIdWithUserId::value_type(
        m_OAuthResult.m_userId, writeShardIdJobId));
}

void SynchronizationManagerPrivate::finalizeStoreOAuthResult()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::finalizeStoreOAuthResult");

    auto it = m_writtenOAuthResultByUserId.find(m_OAuthResult.m_userId);
    if (Q_UNLIKELY(it == m_writtenOAuthResultByUserId.end())) {
        ErrorString error(
            QT_TR_NOOP("Internal error: can't finalize the store of OAuth "
                       "result, no OAuth data found for user id"));

        error.details() = QString::number(m_OAuthResult.m_userId);
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    const auto & writtenOAuthResult = it.value();

    ApplicationSettings appSettings(
        m_pRemoteToLocalSyncManager->account(),
        SYNCHRONIZATION_PERSISTENCE_NAME);

    QString keyGroup = QStringLiteral("Authentication/") + m_host +
        QStringLiteral("/") + QString::number(writtenOAuthResult.m_userId) +
        QStringLiteral("/");

    appSettings.setValue(
        keyGroup + NOTE_STORE_URL_KEY, writtenOAuthResult.m_noteStoreUrl);

    appSettings.setValue(
        keyGroup + EXPIRATION_TIMESTAMP_KEY,
        writtenOAuthResult.m_expirationTime);

    appSettings.setValue(
        keyGroup + AUTHENTICATION_TIMESTAMP_KEY,
        writtenOAuthResult.m_authenticationTime);

    appSettings.setValue(
        keyGroup + WEB_API_URL_PREFIX_KEY,
        writtenOAuthResult.m_webApiUrlPrefix);

    for (const auto & cookie: qAsConst(m_OAuthResult.m_cookies)) {
        QString cookieName = QString::fromUtf8(cookie.name());
        if (!cookieName.startsWith(QStringLiteral("web")) ||
            !cookieName.endsWith(QStringLiteral("PreUserGuid")))
        {
            QNDEBUG(
                "synchronization",
                "Skipping cookie " << cookie.name() << " from persistence");
            continue;
        }

        appSettings.setValue(
            keyGroup + USER_STORE_COOKIE_KEY, cookie.toRawForm());
        QNDEBUG("synchronization", "Persisted cookie " << cookie.name());
    }

    QNDEBUG(
        "synchronization",
        "Successfully wrote the authentication result "
            << "info to the application settings for host " << m_host
            << ", user id " << writtenOAuthResult.m_userId
            << ": auth token expiration timestamp = "
            << printableDateTimeFromTimestamp(
                   writtenOAuthResult.m_expirationTime)
            << ", web API url prefix = "
            << writtenOAuthResult.m_webApiUrlPrefix);

    finalizeAuthentication();
}

void SynchronizationManagerPrivate::finalizeAuthentication()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::finalizeAuthentication: result = "
            << m_OAuthResult);

    switch (m_authContext) {
    case AuthContext::Blank:
    {
        ErrorString error(
            QT_TR_NOOP("Internal error: incorrect authentication "
                       "context: blank"));
        Q_EMIT notifyError(error);
        break;
    }
    case AuthContext::SyncLaunch:
    {
        launchSync();
        break;
    }
    case AuthContext::NewUserRequest:
    case AuthContext::CurrentUserRequest:
    {
        Account account = m_pRemoteToLocalSyncManager->account();
        QNDEBUG(
            "synchronization",
            "Emitting the authenticationFinished "
                << "signal: " << account);

        Q_EMIT authenticationFinished(
            /* success = */ true, ErrorString(), account);

        auto it = m_writtenOAuthResultByUserId.find(m_OAuthResult.m_userId);
        if (it != m_writtenOAuthResultByUserId.end()) {
            m_writtenOAuthResultByUserId.erase(it);
        }

        break;
    }
    case AuthContext::AuthToLinkedNotebooks:
        authenticateToLinkedNotebooks();
        break;
    default:
    {
        ErrorString error(
            QT_TR_NOOP("Internal error: unknown authentication context"));
        error.details() = ToString(m_authContext);
        Q_EMIT notifyError(error);
        break;
    }
    }

    m_authContext = AuthContext::Blank;
}

void SynchronizationManagerPrivate::finalizeRevokeAuthentication(
    const qevercloud::UserID userId)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::finalizeRevokeAuthentication: "
            << userId);

    removeNonSecretPersistentAuthInfo(userId);

    if (m_OAuthResult.m_userId == userId) {
        QNDEBUG(
            "synchronization",
            "Cleaning up the auth data for current "
                << "user: " << userId);
        m_OAuthResult = AuthData();
        m_OAuthResult.m_userId = userId;
    }

    Q_EMIT authenticationRevoked(
        /* success = */ true, ErrorString(), userId);
}

void SynchronizationManagerPrivate::removeNonSecretPersistentAuthInfo(
    const qevercloud::UserID userId)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::removeNonSecretPersistentAuthInfo: "
            << userId);

    // FIXME: not only user id but name and host are also required to access
    // the account's persistent settings. They are not easily available so
    // using the hacky way to get them. In future account should be passed in
    // here to avoid that.

    QString accountName;
    QString host;

    auto storagePath = applicationPersistentStoragePath();

    auto evernoteAccountsDirPath =
        storagePath + QStringLiteral("/EvernoteAccounts");

    QDir evernoteAccountsDir(evernoteAccountsDirPath);
    if (evernoteAccountsDir.exists() && evernoteAccountsDir.isReadable()) {
        auto subdirs = evernoteAccountsDir.entryList(
            QDir::Filters(QDir::AllDirs | QDir::NoDotAndDotDot));

        QString userIdStr = QString::number(userId);

        for (const auto & subdir: ::qAsConst(subdirs)) {
            if (!subdir.endsWith(userIdStr)) {
                continue;
            }

            QStringList nameParts = subdir.split(
                QStringLiteral("_"),
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
                Qt::SkipEmptyParts,
#else
                QString::SkipEmptyParts,
#endif
                Qt::CaseInsensitive);

            int numParts = nameParts.size();
            if (numParts < 3) {
                continue;
            }

            if (nameParts[numParts - 1] == userIdStr) {
                host = nameParts[numParts - 2];

                nameParts = nameParts.mid(0, numParts - 2);
                accountName = nameParts.join(QStringLiteral("_"));
                break;
            }
        }
    }

    if (accountName.isEmpty()) {
        QNWARNING(
            "synchronization",
            "Failed to detect existing Evernote "
                << "account for user id " << userId << ", cannot remove its "
                << "persistent auth info");
        return;
    }

    QNDEBUG(
        "synchronization",
        "Found Evernote account corresponding to user "
            << "id " << userId << ": name = " << accountName
            << ", host = " << host);

    // Now can actually create this account and mess with its persistent
    // settings

    Account account(
        accountName, Account::Type::Evernote, userId,
        Account::EvernoteAccountType::Free, // it doesn't really matter now
        host);

    ApplicationSettings appSettings(account, SYNCHRONIZATION_PERSISTENCE_NAME);

    QString authKeyGroup = QStringLiteral("Authentication/") + host +
        QStringLiteral("/") + QString::number(userId) + QStringLiteral("/");

    appSettings.remove(authKeyGroup);
}

void SynchronizationManagerPrivate::timerEvent(QTimerEvent * pTimerEvent)
{
    if (Q_UNLIKELY(!pTimerEvent)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Qt error: detected null pointer to QTimerEvent"));
        QNWARNING("synchronization", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    int timerId = pTimerEvent->timerId();
    killTimer(timerId);

    QNDEBUG("synchronization", "Timer event for timer id " << timerId);

    if (timerId == m_launchSyncPostponeTimerId) {
        QNDEBUG(
            "synchronization",
            "Re-launching the sync procedure due to "
                << "RATE_LIMIT_REACHED exception when trying to get "
                << "the sync state the last time");
        launchSync();
        return;
    }

    if (timerId == m_authenticateToLinkedNotebooksPostponeTimerId) {
        m_authenticateToLinkedNotebooksPostponeTimerId = -1;
        QNDEBUG(
            "synchronization",
            "Re-attempting to authenticate to "
                << "the remaining linked (shared) notebooks");
        onRequestAuthenticationTokensForLinkedNotebooks(
            m_linkedNotebookAuthDataPendingAuthentication);
        return;
    }
}

void SynchronizationManagerPrivate::clear()
{
    QNDEBUG("synchronization", "SynchronizationManagerPrivate::clear");

    m_lastUpdateCount = -1;
    m_previousUpdateCount = -1;
    m_lastSyncTime = -1;
    m_cachedLinkedNotebookLastUpdateCountByGuid.clear();
    m_cachedLinkedNotebookLastSyncTimeByGuid.clear();
    m_onceReadLastSyncParams = false;

    m_authContext = AuthContext::Blank;

    m_launchSyncPostponeTimerId = -1;

    m_pNoteStore->stop();

    for (auto it: qevercloud::toRange(m_noteStoresByLinkedNotebookGuids)) {
        INoteStore * pNoteStore = it.value();
        pNoteStore->stop();
        pNoteStore->setParent(nullptr);
        pNoteStore->deleteLater();
    }

    m_noteStoresByLinkedNotebookGuids.clear();

    m_pRemoteToLocalSyncManager->stop();
    m_somethingDownloaded = false;

    m_pSendLocalChangesManager->stop();

    m_linkedNotebookAuthDataPendingAuthentication.clear();
    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.clear();
    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.clear();

    m_authenticateToLinkedNotebooksPostponeTimerId = -1;

    m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.clear();
    m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.clear();
    m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.clear();
    m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.clear();

    m_linkedNotebookAuthTokensPendingWritingByGuid.clear();
    m_linkedNotebookShardIdsPendingWritingByGuid.clear();

    m_linkedNotebookGuidsWithoutLocalAuthData.clear();

    m_shouldRepeatIncrementalSyncAfterSendingChanges = false;
}

bool SynchronizationManagerPrivate::validAuthentication() const
{
    if (m_OAuthResult.m_expirationTime == static_cast<qint64>(0)) {
        // The value is not set
        return false;
    }

    if (m_OAuthResult.m_authenticationTime == static_cast<qint64>(0)) {
        // The value is not set
        return false;
    }

    return !checkIfTimestampIsAboutToExpireSoon(m_OAuthResult.m_expirationTime);
}

bool SynchronizationManagerPrivate::checkIfTimestampIsAboutToExpireSoon(
    const qevercloud::Timestamp timestamp) const
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::"
            << "checkIfTimestampIsAboutToExpireSoon: "
            << printableDateTimeFromTimestamp(timestamp));

    qevercloud::Timestamp currentTimestamp =
        QDateTime::currentMSecsSinceEpoch();
    QNTRACE(
        "synchronization",
        "Current datetime: "
            << printableDateTimeFromTimestamp(currentTimestamp));

    if ((timestamp - currentTimestamp) < HALF_AN_HOUR_IN_MSEC) {
        return true;
    }

    return false;
}

void SynchronizationManagerPrivate::authenticateToLinkedNotebooks()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::authenticateToLinkedNotebooks");

    if (Q_UNLIKELY(m_OAuthResult.m_userId < 0)) {
        ErrorString error(
            QT_TR_NOOP("Detected attempt to authenticate to linked "
                       "notebooks while there is no user id set "
                       "to the synchronization manager"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    const int numLinkedNotebooks =
        m_linkedNotebookAuthDataPendingAuthentication.size();
    if (numLinkedNotebooks == 0) {
        QNDEBUG(
            "synchronization",
            "No linked notebooks waiting for "
                << "authentication, sending the cached auth tokens, shard ids "
                   "and "
                << "expiration times");

        Q_EMIT sendAuthenticationTokensForLinkedNotebooks(
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
            m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
        return;
    }

    ApplicationSettings appSettings(
        m_pRemoteToLocalSyncManager->account(),
        SYNCHRONIZATION_PERSISTENCE_NAME);

    QString keyGroup = QStringLiteral("Authentication/") + m_host +
        QStringLiteral("/") + QString::number(m_OAuthResult.m_userId) +
        QStringLiteral("/");

    QHash<QString, std::pair<QString, QString>>
        authTokensAndShardIdsToCacheByGuid;
    QHash<QString, qevercloud::Timestamp>
        authTokenExpirationTimestampsToCacheByGuid;

    QString keyPrefix = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    QSet<QString> linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain;

    for (auto it = m_linkedNotebookAuthDataPendingAuthentication.begin();
         it != m_linkedNotebookAuthDataPendingAuthentication.end();)
    {
        const LinkedNotebookAuthData & authData = *it;

        const QString & guid = authData.m_guid;
        const QString & shardId = authData.m_shardId;
        const QString & sharedNotebookGlobalId =
            authData.m_sharedNotebookGlobalId;
        const QString & uri = authData.m_uri;
        const QString & noteStoreUrl = authData.m_noteStoreUrl;

        QNDEBUG(
            "synchronization",
            "Processing linked notebook guid = "
                << guid << ", shard id = " << shardId
                << ", shared notebook global id = " << sharedNotebookGlobalId
                << ", uri = " << uri << ", note store URL = " << noteStoreUrl);

        if (sharedNotebookGlobalId.isEmpty() && !uri.isEmpty()) {
            // This appears to be a public notebook and per the official
            // documentation from Evernote
            // (dev.evernote.com/media/pdf/edam-sync.pdf) it doesn't need the
            // authentication token at all so will use empty string for its
            // authentication token
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[guid] =
                std::make_pair(QString(), shardId);

            m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid[guid] =
                std::numeric_limits<qint64>::max();

            it = m_linkedNotebookAuthDataPendingAuthentication.erase(it);
            continue;
        }

        bool forceRemoteAuth = false;

        auto linkedNotebookAuthTokenIt =
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.find(guid);

        if (linkedNotebookAuthTokenIt ==
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.end())
        {
            auto noAuthDataIt =
                m_linkedNotebookGuidsWithoutLocalAuthData.find(guid);

            if (noAuthDataIt != m_linkedNotebookGuidsWithoutLocalAuthData.end())
            {
                forceRemoteAuth = true;
                Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.erase(
                    noAuthDataIt))
            }
            else {
                QNDEBUG(
                    "synchronization",
                    "Haven't found the authentication "
                        << "token and shard id for linked notebook guid "
                        << guid
                        << " in the local cache, will try to read them from "
                        << "the keychain");

                Q_UNUSED(
                    linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain
                        .insert(guid))

                ++it;
                continue;
            }
        }

        if (!forceRemoteAuth) {
            auto linkedNotebookAuthTokenExpirationIt =
                m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.find(guid);

            if (linkedNotebookAuthTokenExpirationIt ==
                m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end())
            {
                QVariant expirationTimeVariant = appSettings.value(
                    keyGroup + LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX +
                    guid);

                if (!expirationTimeVariant.isNull()) {
                    bool conversionResult = false;

                    qevercloud::Timestamp expirationTime =
                        expirationTimeVariant.toLongLong(&conversionResult);

                    if (conversionResult) {
                        linkedNotebookAuthTokenExpirationIt =
                            m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid
                                .insert(guid, expirationTime);
                    }
                    else {
                        QNWARNING(
                            "synchronization",
                            "Can't convert linked "
                                << "notebook's authentication token's "
                                   "expiration "
                                << "time from QVariant retrieved from app "
                                   "settings "
                                << "into timestamp: linked notebook guid = "
                                << guid
                                << ", variant = " << expirationTimeVariant);
                    }
                }
            }

            if ((linkedNotebookAuthTokenExpirationIt !=
                 m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end()) &&
                !checkIfTimestampIsAboutToExpireSoon(
                    linkedNotebookAuthTokenExpirationIt.value()))
            {
                QNDEBUG(
                    "synchronization",
                    "Found authentication data for "
                        << "linked notebook guid " << guid
                        << " + verified its expiration timestamp");
                it = m_linkedNotebookAuthDataPendingAuthentication.erase(it);
                continue;
            }
        }

        QNDEBUG(
            "synchronization",
            "Authentication data for linked notebook "
                << "guid " << guid << " was either not found in local cache "
                << "(and/or app settings / keychain) or has "
                << "expired, need to receive that from remote "
                << "Evernote service");

        if (m_authenticateToLinkedNotebooksPostponeTimerId >= 0) {
            QNDEBUG(
                "synchronization",
                "Authenticate to linked notebook "
                    << "postpone timer is active, will wait to preserve the "
                       "breach "
                    << "of Evernote rate API limit");
            ++it;
            continue;
        }

        if (m_authContext != AuthContext::Blank) {
            QNDEBUG(
                "synchronization",
                "Authentication context variable is not "
                    << "set to blank which means that authentication must be "
                       "in "
                    << "progress: " << m_authContext
                    << "; won't attempt to call "
                    << "remote Evernote API at this time");
            ++it;
            continue;
        }

        qevercloud::AuthenticationResult authResult;
        ErrorString errorDescription;
        qint32 rateLimitSeconds = 0;

        auto * pNoteStore = noteStoreForLinkedNotebookGuid(guid);
        if (Q_UNLIKELY(!pNoteStore)) {
            ErrorString error(
                QT_TR_NOOP("Can't sync the linked notebook contents: can't "
                           "find or create the note store for the linked "
                           "notebook"));
            Q_EMIT notifyError(error);
            return;
        }

        pNoteStore->setNoteStoreUrl(noteStoreUrl);

        pNoteStore->setAuthData(
            m_OAuthResult.m_authToken, m_OAuthResult.m_cookies);

        qint32 errorCode = pNoteStore->authenticateToSharedNotebook(
            sharedNotebookGlobalId, authResult, errorDescription,
            rateLimitSeconds);

        if (errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED)) {
            if (validAuthentication()) {
                ErrorString error(QT_TR_NOOP("Unexpected AUTH_EXPIRED error"));
                error.additionalBases().append(errorDescription.base());
                error.additionalBases().append(
                    errorDescription.additionalBases());
                error.details() = errorDescription.details();
                Q_EMIT notifyError(error);
            }
            else {
                authenticateImpl(AuthContext::AuthToLinkedNotebooks);
            }

            ++it;
            continue;
        }
        else if (
            errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
        {
            if (rateLimitSeconds < 0) {
                errorDescription.setBase(
                    QT_TR_NOOP("Rate limit reached but the number of seconds "
                               "to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                Q_EMIT notifyError(errorDescription);
                return;
            }

            m_authenticateToLinkedNotebooksPostponeTimerId =
                startTimer(secondsToMilliseconds(rateLimitSeconds));

            Q_EMIT rateLimitExceeded(rateLimitSeconds);

            ++it;
            continue;
        }
        else if (errorCode != 0) {
            QNWARNING(
                "synchronization",
                "Failed to authenticate to shared "
                    << "notebook: " << errorDescription
                    << " (error code = " << errorCode << ")");
            Q_EMIT notifyError(errorDescription);
            return;
        }

        QNDEBUG(
            "synchronization",
            "Retrieved authentication: server-side "
                << "result generation time (currentTime) = "
                << printableDateTimeFromTimestamp(authResult.currentTime)
                << ", expiration time for the authentication result "
                << "(expiration): "
                << printableDateTimeFromTimestamp(authResult.expiration)
                << ", user: "
                << (authResult.user.isSet() ? ToString(authResult.user.ref())
                                            : QStringLiteral("<empty>")));

        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[guid] =
            std::make_pair(authResult.authenticationToken, shardId);

        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid[guid] =
            authResult.expiration;

        auto & authTokenAndShardId = authTokensAndShardIdsToCacheByGuid[guid];
        authTokenAndShardId.first = authResult.authenticationToken;
        authTokenAndShardId.second = shardId;

        authTokenExpirationTimestampsToCacheByGuid[guid] =
            authResult.expiration;

        it = m_linkedNotebookAuthDataPendingAuthentication.erase(it);
    }

    if (m_linkedNotebookAuthDataPendingAuthentication.isEmpty()) {
        QNDEBUG(
            "synchronization",
            "Retrieved authentication data for all "
                << "requested linked notebooks, sending the answer now");

        Q_EMIT sendAuthenticationTokensForLinkedNotebooks(
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
            m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
    }

    if (!linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain.isEmpty())
    {
        for (const auto & guid:
             linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain)
        {
            // 1) Set up the job of reading the authentication token
            auto readAuthTokenJobIt =
                m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left
                    .find(guid);

            if (readAuthTokenJobIt ==
                m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left
                    .end())
            {
                QUuid jobId = m_pKeychainService->startReadPasswordJob(
                    READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB,
                    keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid);

                m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids
                    .insert(
                        KeychainJobIdWithGuidBimap::value_type(guid, jobId));
            }

            // 2) Set up the job reading the shard id
            auto readShardIdJobIt =
                m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left
                    .find(guid);

            if (readShardIdJobIt ==
                m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left
                    .end())
            {
                QUuid jobId = m_pKeychainService->startReadPasswordJob(
                    READ_LINKED_NOTEBOOK_SHARD_ID_JOB,
                    keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid);

                m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.insert(
                    KeychainJobIdWithGuidBimap::value_type(guid, jobId));
            }
        }

        QNDEBUG(
            "synchronization",
            "Pending read auth tokens and shard ids "
                << "from keychain for "
                << linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain
                       .size()
                << " linked notebooks");
        return;
    }

    // Caching linked notebook's authentication token's expiration time in app
    // settings
    for (auto it: qevercloud::toRange(
             qAsConst(authTokenExpirationTimestampsToCacheByGuid)))
    {
        QString key =
            LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX + it.key();
        appSettings.setValue(keyGroup + key, QVariant(it.value()));
    }

    // Caching linked notebook's authentication tokens and shard ids in the
    // keychain
    for (auto it: qevercloud::toRange(
             authTokensAndShardIdsToCacheByGuid)) // clazy:exclude=range-loop
    {
        const QString & guid = it.key();
        const QString & token = it.value().first;
        const QString & shardId = it.value().second;

        // 1) Set up the job writing the auth token to the keychain
        auto jobIt = // clazy:exclude=rule-of-two-soft
            m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left
                .find(guid); // clazy:exclude=rule-of-two-soft

        if (jobIt ==
            m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left
                .end())
        {
            QString key =
                keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid;
            QUuid jobId = m_pKeychainService->startWritePasswordJob(
                WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB, key, token);

            m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.insert(
                KeychainJobIdWithGuidBimap::value_type(guid, jobId));
        }
        else {
            m_linkedNotebookAuthTokensPendingWritingByGuid[guid] = token;
        }

        // 2) Set up the job writing the shard id to the keychain
        jobIt = // clazy:exclude=rule-of-two-soft
            m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left.find(
                guid); // clazy:exclude=rule-of-two-soft

        if (jobIt ==
            m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left
                .end())
        {
            QString key = keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid;

            QUuid jobId = m_pKeychainService->startWritePasswordJob(
                WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB, key, shardId);

            m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.insert(
                KeychainJobIdWithGuidBimap::value_type(guid, jobId));
        }
        else {
            m_linkedNotebookShardIdsPendingWritingByGuid[guid] = shardId;
        }
    }
}

void SynchronizationManagerPrivate::onReadAuthTokenFinished(
    const IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription, const QString & password)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onReadAuthTokenFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
        QNWARNING(
            "synchronization",
            "Unexpectedly missing OAuth token in "
                << "the keychain: " << errorDescription
                << "; fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        QNWARNING(
            "synchronization",
            "Attempt to read the auth token returned "
                << "with error: error code " << errorCode << ", "
                << errorDescription << ". Fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    QNDEBUG(
        "synchronization",
        "Successfully restored the authentication "
            << "token");
    m_OAuthResult.m_authToken = password;

    if (!m_authenticationInProgress &&
        !isReadingShardId(m_OAuthResult.m_userId) &&
        !isWritingShardId(m_OAuthResult.m_userId))
    {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onReadShardIdFinished(
    const IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription, const QString & password)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onReadShardIdFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
        QNWARNING(
            "synchronization",
            "Unexpectedly missing OAuth shard id in "
                << "the keychain: " << errorDescription
                << "; fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        QNWARNING(
            "synchronization",
            "Attempt to read the shard id returned "
                << "with error: error code " << errorCode << ", "
                << errorDescription << ". Fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    QNDEBUG("synchronization", "Successfully restored the shard id");
    m_OAuthResult.m_shardId = password;

    if (!m_authenticationInProgress &&
        !isReadingAuthToken(m_OAuthResult.m_userId) &&
        !isWritingAuthToken(m_OAuthResult.m_userId))
    {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onWriteAuthTokenFinished(
    const IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onWriteAuthTokenFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        ErrorString error(
            QT_TR_NOOP("Failed to write the OAuth token to the keychain"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    QNDEBUG(
        "synchronization",
        "Successfully stored the authentication token "
            << "in the keychain");

    if (!m_authenticationInProgress &&
        !isReadingShardId(m_OAuthResult.m_userId) &&
        !isWritingShardId(m_OAuthResult.m_userId))
    {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onWriteShardIdFinished(
    const IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onWriteShardIdFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        ErrorString error(
            QT_TR_NOOP("Failed to write the shard id to the keychain"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    QNDEBUG(
        "synchronization",
        "Successfully stored the shard id in "
            << "the keychain");

    if (!m_authenticationInProgress &&
        !isReadingAuthToken(m_OAuthResult.m_userId) &&
        !isWritingAuthToken(m_OAuthResult.m_userId))
    {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onDeleteAuthTokenFinished(
    const IKeychainService::ErrorCode errorCode,
    const qevercloud::UserID userId, const ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onDeleteAuthTokenFinished: user id = "
            << userId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    if ((errorCode != IKeychainService::ErrorCode::NoError) &&
        (errorCode != IKeychainService::ErrorCode::EntryNotFound))
    {
        auto it = m_deleteShardIdJobIdsWithUserIds.left.find(userId);
        if (it != m_deleteShardIdJobIdsWithUserIds.left.end()) {
            m_deleteShardIdJobIdsWithUserIds.left.erase(it);
        }

        QNWARNING(
            "synchronization",
            "Attempt to delete the auth token "
                << "returned with error: " << errorDescription);

        ErrorString error(
            QT_TR_NOOP("Failed to delete authentication token "
                       "from the keychain"));

        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();

        Q_EMIT authenticationRevoked(
            /* success = */ false, error, userId);
        return;
    }

    if (!isDeletingShardId(userId)) {
        finalizeRevokeAuthentication(userId);
    }
}

void SynchronizationManagerPrivate::onDeleteShardIdFinished(
    const IKeychainService::ErrorCode errorCode,
    const qevercloud::UserID userId, const ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onDeleteShardIdFinished: user id = "
            << userId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    if ((errorCode != IKeychainService::ErrorCode::NoError) &&
        (errorCode != IKeychainService::ErrorCode::EntryNotFound))
    {
        auto it = m_deleteAuthTokenJobIdsWithUserIds.left.find(userId);
        if (it != m_deleteAuthTokenJobIdsWithUserIds.left.end()) {
            m_deleteAuthTokenJobIdsWithUserIds.left.erase(it);
        }

        QNWARNING(
            "synchronization",
            "Attempt to delete the shard id returned "
                << "with error: " << errorDescription);

        ErrorString error(
            QT_TR_NOOP("Failed to delete shard id from the keychain"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();

        Q_EMIT authenticationRevoked(
            /* success = */ false, error, userId);
        return;
    }

    if (!isDeletingAuthToken(userId)) {
        finalizeRevokeAuthentication(userId);
    }
}

bool SynchronizationManagerPrivate::isReadingAuthToken(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return (
        m_readAuthTokenJobIdsWithUserIds.left.find(userId) !=
        m_readAuthTokenJobIdsWithUserIds.left.end());
}

bool SynchronizationManagerPrivate::isReadingShardId(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return (
        m_readShardIdJobIdsWithUserIds.left.find(userId) !=
        m_readShardIdJobIdsWithUserIds.left.end());
}

bool SynchronizationManagerPrivate::isWritingAuthToken(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return (
        m_writeAuthTokenJobIdsWithUserIds.left.find(userId) !=
        m_writeAuthTokenJobIdsWithUserIds.left.end());
}

bool SynchronizationManagerPrivate::isWritingShardId(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return (
        m_writeShardIdJobIdsWithUserIds.left.find(userId) !=
        m_writeShardIdJobIdsWithUserIds.left.end());
}

bool SynchronizationManagerPrivate::isDeletingAuthToken(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return (
        m_deleteAuthTokenJobIdsWithUserIds.left.find(userId) !=
        m_deleteAuthTokenJobIdsWithUserIds.left.end());
}

bool SynchronizationManagerPrivate::isDeletingShardId(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return (
        m_deleteShardIdJobIdsWithUserIds.left.find(userId) !=
        m_deleteShardIdJobIdsWithUserIds.left.end());
}

void SynchronizationManagerPrivate::tryUpdateLastSyncStatus()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::tryUpdateLastSyncStatus");

    qint32 updateCount = -1;
    QHash<QString, qint32> updateCountsByLinkedNotebookGuid;
    m_pRemoteToLocalSyncManager->collectNonProcessedItemsSmallestUsns(
        updateCount, updateCountsByLinkedNotebookGuid);

    if ((updateCount < 0) && updateCountsByLinkedNotebookGuid.isEmpty()) {
        QNDEBUG(
            "synchronization",
            "Found no USNs for neither user's own "
                << "account nor linked notebooks");
        return;
    }

    qevercloud::Timestamp lastSyncTime = QDateTime::currentMSecsSinceEpoch();

    bool shouldUpdatePersistentSyncSettings = false;

    if ((updateCount > 0) &&
        m_pRemoteToLocalSyncManager->downloadedSyncChunks()) {
        m_lastUpdateCount = updateCount;
        m_lastSyncTime = lastSyncTime;

        QNDEBUG(
            "synchronization",
            "Got updated sync state for user's own "
                << "account: update count = " << m_lastUpdateCount
                << ", last sync time = "
                << printableDateTimeFromTimestamp(m_lastSyncTime));
        shouldUpdatePersistentSyncSettings = true;
    }
    else if (
        !updateCountsByLinkedNotebookGuid.isEmpty() &&
        m_pRemoteToLocalSyncManager->downloadedLinkedNotebooksSyncChunks())
    {
        for (auto it:
             qevercloud::toRange(qAsConst(updateCountsByLinkedNotebookGuid))) {
            m_cachedLinkedNotebookLastUpdateCountByGuid[it.key()] = it.value();
            m_cachedLinkedNotebookLastSyncTimeByGuid[it.key()] = lastSyncTime;
            QNDEBUG(
                "synchronization",
                "Got updated sync state for linked "
                    << "notebook with guid " << it.key() << ", update count = "
                    << it.value() << ", last sync time = " << lastSyncTime);
            shouldUpdatePersistentSyncSettings = true;
        }
    }

    if (shouldUpdatePersistentSyncSettings) {
        updatePersistentSyncSettings();
    }
}

void SynchronizationManagerPrivate::updatePersistentSyncSettings()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::updatePersistentSyncSettings");

    auto syncState = std::make_shared<SyncStateStorage::SyncState>();

    syncState->m_userDataUpdateCount = m_lastUpdateCount;
    syncState->m_userDataLastSyncTime = m_lastSyncTime;

    syncState->m_updateCountsByLinkedNotebookGuid =
        m_cachedLinkedNotebookLastUpdateCountByGuid;

    syncState->m_lastSyncTimesByLinkedNotebookGuid =
        m_cachedLinkedNotebookLastSyncTimeByGuid;

    m_pSyncStateStorage->setSyncState(
        m_pRemoteToLocalSyncManager->account(), syncState);
}

INoteStore * SynchronizationManagerPrivate::noteStoreForLinkedNotebook(
    const LinkedNotebook & linkedNotebook)
{
    QNTRACE(
        "synchronization",
        "SynchronizationManagerPrivate::noteStoreForLinkedNotebook: "
            << linkedNotebook);

    if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
        QNTRACE(
            "synchronization",
            "Linked notebook has no guid, can't find or "
                << "create note store for it");
        return nullptr;
    }

    auto * pNoteStore = noteStoreForLinkedNotebookGuid(linkedNotebook.guid());
    if (Q_UNLIKELY(!pNoteStore)) {
        return nullptr;
    }

    if (linkedNotebook.hasNoteStoreUrl()) {
        QNTRACE(
            "synchronization",
            "Setting note store URL to the created "
                << "and/or found note store: "
                << linkedNotebook.noteStoreUrl());
        pNoteStore->setNoteStoreUrl(linkedNotebook.noteStoreUrl());
    }

    return pNoteStore;
}

INoteStore * SynchronizationManagerPrivate::noteStoreForLinkedNotebookGuid(
    const QString & guid)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::noteStoreForLinkedNotebookGuid: guid = "
            << guid);

    if (Q_UNLIKELY(guid.isEmpty())) {
        QNWARNING(
            "synchronization",
            "Can't find or create the note store for "
                << "empty linked notebook guid");
        return nullptr;
    }

    auto it = m_noteStoresByLinkedNotebookGuids.find(guid);
    if (it != m_noteStoresByLinkedNotebookGuids.end()) {
        QNDEBUG(
            "synchronization",
            "Found existing note store for linked "
                << "notebook guid " << guid);
        return it.value();
    }

    QNDEBUG(
        "synchronization",
        "Found no existing note store corresponding to "
            << "linked notebook guid " << guid);

    if (m_authenticationInProgress) {
        QNWARNING(
            "synchronization",
            "Can't create the note store for a linked "
                << "notebook: the authentication is in progress");
        return nullptr;
    }

    auto * pNoteStore = m_pNoteStore->create();
    pNoteStore->setParent(this);

    pNoteStore->setAuthData(m_OAuthResult.m_authToken, m_OAuthResult.m_cookies);

    m_noteStoresByLinkedNotebookGuids[guid] = pNoteStore;
    return pNoteStore;
}

SynchronizationManagerPrivate::RemoteToLocalSynchronizationManagerController::
    RemoteToLocalSynchronizationManagerController(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManagerPrivate & syncManager) :
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_syncManager(syncManager)
{}

LocalStorageManagerAsync & SynchronizationManagerPrivate::
    RemoteToLocalSynchronizationManagerController::localStorageManagerAsync()
{
    return m_localStorageManagerAsync;
}

INoteStore & SynchronizationManagerPrivate::
    RemoteToLocalSynchronizationManagerController::noteStore()
{
    return *m_syncManager.m_pNoteStore;
}

IUserStore & SynchronizationManagerPrivate::
    RemoteToLocalSynchronizationManagerController::userStore()
{
    return *m_syncManager.m_pUserStore;
}

INoteStore * SynchronizationManagerPrivate::
    RemoteToLocalSynchronizationManagerController::noteStoreForLinkedNotebook(
        const LinkedNotebook & linkedNotebook)
{
    return m_syncManager.noteStoreForLinkedNotebook(linkedNotebook);
}

SynchronizationManagerPrivate::SendLocalChangesManagerController::
    SendLocalChangesManagerController(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManagerPrivate & syncManager) :
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_syncManager(syncManager)
{}

LocalStorageManagerAsync & SynchronizationManagerPrivate::
    SendLocalChangesManagerController::localStorageManagerAsync()
{
    return m_localStorageManagerAsync;
}

INoteStore &
SynchronizationManagerPrivate::SendLocalChangesManagerController::noteStore()
{
    return *m_syncManager.m_pNoteStore;
}

INoteStore * SynchronizationManagerPrivate::SendLocalChangesManagerController::
    noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook)
{
    return m_syncManager.noteStoreForLinkedNotebook(linkedNotebook);
}

QTextStream & SynchronizationManagerPrivate::AuthData::print(
    QTextStream & strm) const
{
    strm << "AuthData: {\n"
         << "    user id = " << m_userId << ";\n"
         << "    auth token expiration time = "
         << printableDateTimeFromTimestamp(m_expirationTime) << ";\n"
         << "    authentication time = "
         << printableDateTimeFromTimestamp(m_authenticationTime) << ";\n"
         << "    shard id = " << m_shardId << ";\n"
         << "    note store url = " << m_noteStoreUrl << ";\n"
         << "    web API url prefix = " << m_webApiUrlPrefix << ";\n"
         << "    cookies count: " << m_cookies.size() << ";\n"
         << "};\n";
    return strm;
}

template <typename T>
void printAuthContext(
    T & t, const SynchronizationManagerPrivate::AuthContext ctx)
{
    using AuthContext = SynchronizationManagerPrivate::AuthContext;
    switch (ctx) {
    case AuthContext::Blank:
        t << "Blank";
        break;
    case AuthContext::SyncLaunch:
        t << "Sync launch";
        break;
    case AuthContext::NewUserRequest:
        t << "New user request";
        break;
    case AuthContext::CurrentUserRequest:
        t << "Current user request";
        break;
    case AuthContext::AuthToLinkedNotebooks:
        t << "Auth to linked notebooks";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(ctx) << ")";
        break;
    }
}

QTextStream & operator<<(
    QTextStream & strm, const SynchronizationManagerPrivate::AuthContext ctx)
{
    printAuthContext(strm, ctx);
    return strm;
}

QDebug & operator<<(
    QDebug & dbg, const SynchronizationManagerPrivate::AuthContext ctx)
{
    printAuthContext(dbg, ctx);
    return dbg;
}

} // namespace quentier
