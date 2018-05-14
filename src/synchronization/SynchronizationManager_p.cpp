/*
 * Copyright 2016 Dmitry Ivanov
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
#include "SynchronizationShared.h"
#include "NoteStore.h"
#include "UserStore.h"
#include "../utility/KeychainService.h"
#include <quentier/utility/Utility.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/QuentierCheckPtr.h>
#include <quentier/utility/Printable.h>
#include <quentier_private/synchronization/SynchronizationManagerDependencyInjector.h>
#include <QCoreApplication>
#include <limits>

namespace quentier {

class SynchronizationManagerPrivate::RemoteToLocalSynchronizationManagerController: public RemoteToLocalSynchronizationManager::IManager
{
public:
    RemoteToLocalSynchronizationManagerController(LocalStorageManagerAsync & localStorageManagerAsync,
                                                  SynchronizationManagerPrivate & syncManager);

    virtual LocalStorageManagerAsync & localStorageManagerAsync() Q_DECL_OVERRIDE;
    virtual INoteStore & noteStore() Q_DECL_OVERRIDE;
    virtual IUserStore & userStore() Q_DECL_OVERRIDE;
    virtual INoteStore * noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook) Q_DECL_OVERRIDE;

private:
    LocalStorageManagerAsync &          m_localStorageManagerAsync;
    SynchronizationManagerPrivate &     m_syncManager;
};

class SynchronizationManagerPrivate::SendLocalChangesManagerController: public SendLocalChangesManager::IManager
{
public:
    SendLocalChangesManagerController(LocalStorageManagerAsync & localStorageManagerAsync,
                                      SynchronizationManagerPrivate & syncManager);

    virtual LocalStorageManagerAsync & localStorageManagerAsync() Q_DECL_OVERRIDE;
    virtual INoteStore & noteStore() Q_DECL_OVERRIDE;
    virtual INoteStore * noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook) Q_DECL_OVERRIDE;

private:
    LocalStorageManagerAsync &          m_localStorageManagerAsync;
    SynchronizationManagerPrivate &     m_syncManager;
};

SynchronizationManagerPrivate::SynchronizationManagerPrivate(const QString & host, LocalStorageManagerAsync & localStorageManagerAsync,
                                                             IAuthenticationManager & authenticationManager,
                                                             SynchronizationManagerDependencyInjector * pInjector) :
    m_host(host),
    m_maxSyncChunkEntries(50),
    m_previousUpdateCount(-1),
    m_lastUpdateCount(-1),
    m_lastSyncTime(-1),
    m_cachedLinkedNotebookLastUpdateCountByGuid(),
    m_cachedLinkedNotebookLastSyncTimeByGuid(),
    m_onceReadLastSyncParams(false),
    m_pNoteStore((pInjector && pInjector->m_pNoteStore)
                 ? pInjector->m_pNoteStore
                 : (new NoteStore(QSharedPointer<qevercloud::NoteStore>(new qevercloud::NoteStore), this))),
    m_pUserStore((pInjector && pInjector->m_pUserStore)
                 ? pInjector->m_pUserStore
                 : (new UserStore(QSharedPointer<qevercloud::UserStore>(new qevercloud::UserStore(m_host))))),
    m_authContext(AuthContext::Blank),
    m_launchSyncPostponeTimerId(-1),
    m_OAuthResult(),
    m_authenticationInProgress(false),
    m_pRemoteToLocalSyncManagerController(new RemoteToLocalSynchronizationManagerController(localStorageManagerAsync, *this)),
    m_remoteToLocalSyncManager(*m_pRemoteToLocalSyncManagerController, m_host),
    m_pSendLocalChangesManagerController(new SendLocalChangesManagerController(localStorageManagerAsync, *this)),
    m_sendLocalChangesManager(*m_pSendLocalChangesManagerController),
    m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid(),
    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid(),
    m_linkedNotebookAuthDataPendingAuthentication(),
    m_noteStoresByLinkedNotebookGuids(),
    m_authenticateToLinkedNotebooksPostponeTimerId(-1),
    m_pKeychainService((pInjector && pInjector->m_pKeychainService)
                       ? pInjector->m_pKeychainService
                       : (new KeychainService(this))),
    m_readingAuthToken(false),
    m_readingShardId(false),
    m_writingAuthToken(false),
    m_writingShardId(false),
    m_deletingAuthToken(false),
    m_deletingShardId(false),
    m_lastRevokedAuthenticationUserId(-1),
    m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids(),
    m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids(),
    m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids(),
    m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids(),
    m_linkedNotebookAuthTokensPendingWritingByGuid(),
    m_linkedNotebookShardIdsPendingWritingByGuid(),
    m_linkedNotebookGuidsWithoutLocalAuthData(),
    m_shouldRepeatIncrementalSyncAfterSendingChanges(false)
{
    m_OAuthResult.m_userId = -1;

    if (pInjector)
    {
        if (pInjector->m_pNoteStore) {
            m_pNoteStore->setParent(this);
        }

        if (pInjector->m_pKeychainService) {
            m_pKeychainService->setParent(this);
        }
    }

    createConnections(authenticationManager);
}

SynchronizationManagerPrivate::~SynchronizationManagerPrivate()
{}

bool SynchronizationManagerPrivate::active() const
{
    return m_remoteToLocalSyncManager.active() || m_sendLocalChangesManager.active();
}

bool SynchronizationManagerPrivate::downloadNoteThumbnailsOption() const
{
    return m_remoteToLocalSyncManager.shouldDownloadThumbnailsForNotes();
}

void SynchronizationManagerPrivate::setAccount(const Account & account)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::setAccount: ") << account);

    Account currentAccount = m_remoteToLocalSyncManager.account();
    if (currentAccount == account) {
        QNDEBUG(QStringLiteral("The same account is already set, nothing to do"));
        return;
    }

    clear();

    m_OAuthResult = AuthData();
    m_OAuthResult.m_userId = -1;

    if (account.type() == Account::Type::Local) {
        return;
    }

    m_OAuthResult.m_userId = account.id();
    m_remoteToLocalSyncManager.setAccount(account);
    // NOTE: send local changes manager doesn't have any use for the account
}

void SynchronizationManagerPrivate::synchronize()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::synchronize"));

    if (m_authenticationInProgress || m_writingAuthToken || m_writingShardId) {
        ErrorString error(QT_TR_NOOP("Authentication is not finished yet, please wait"));
        QNDEBUG(error << QStringLiteral(", authentication in progress = ")
                << (m_authenticationInProgress ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing OAuth token = ") << (m_writingAuthToken ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing shard id = ") << (m_writingShardId ? QStringLiteral("true") : QStringLiteral("false")));
        Q_EMIT notifyError(error);
        return;
    }

    clear();
    authenticateImpl(AuthContext::SyncLaunch);
}

void SynchronizationManagerPrivate::authenticate()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::authenticate"));

    if (m_authenticationInProgress || m_writingAuthToken || m_writingShardId) {
        ErrorString error(QT_TR_NOOP("Previous authentication is not finished yet, please wait"));
        QNDEBUG(error << QStringLiteral(", authentication in progress = ")
                << (m_authenticationInProgress ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing OAuth token = ") << (m_writingAuthToken ? QStringLiteral("true") : QStringLiteral("false"))
                << QStringLiteral(", writing shard id = ") << (m_writingShardId ? QStringLiteral("true") : QStringLiteral("false")));
        Q_EMIT authenticationFinished(/* success = */ false, error, Account());
        return;
    }

    authenticateImpl(AuthContext::Request);
}

void SynchronizationManagerPrivate::stop()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::stop"));

    tryUpdateLastSyncStatus();

    Q_EMIT stopRemoteToLocalSync();
    Q_EMIT stopSendingLocalChanges();
}

void SynchronizationManagerPrivate::revokeAuthentication(const qevercloud::UserID userId)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::revokeAuthentication: user id = ")
            << userId);

    m_lastRevokedAuthenticationUserId = userId;

    m_deletingAuthToken = true;
    QString deleteAuthTokenService = QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;
    QString deleteAuthTokenKey = QCoreApplication::applicationName() + QStringLiteral("_") +
                                 m_host + QStringLiteral("_") + QString::number(m_lastRevokedAuthenticationUserId);
    m_deleteAuthTokenJobId = m_pKeychainService->startDeletePasswordJob(deleteAuthTokenService, deleteAuthTokenKey);

    m_deletingShardId = true;
    QString deleteShardIdService = QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;
    QString deleteShardIdKey = QCoreApplication::applicationName() + QStringLiteral("_") +
                               m_host + QStringLiteral("_") + QString::number(m_lastRevokedAuthenticationUserId);
    m_deleteShardIdJobId = m_pKeychainService->startDeletePasswordJob(deleteShardIdService, deleteShardIdKey);
}

void SynchronizationManagerPrivate::setDownloadNoteThumbnails(const bool flag)
{
    m_remoteToLocalSyncManager.setDownloadNoteThumbnails(flag);
}

void SynchronizationManagerPrivate::setDownloadInkNoteImages(const bool flag)
{
    m_remoteToLocalSyncManager.setDownloadInkNoteImages(flag);
}

void SynchronizationManagerPrivate::setInkNoteImagesStoragePath(const QString & path)
{
    m_remoteToLocalSyncManager.setInkNoteImagesStoragePath(path);
}

void SynchronizationManagerPrivate::onOAuthResult(bool success, qevercloud::UserID userId, QString authToken,
                                                  qevercloud::Timestamp authTokenExpirationTime, QString shardId,
                                                  QString noteStoreUrl, QString webApiUrlPrefix, ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onOAuthResult: ") << (success ? QStringLiteral("success") : QStringLiteral("failure"))
            << QStringLiteral(", user id = ") << userId << QStringLiteral(", auth token expiration time = ")
            << printableDateTimeFromTimestamp(authTokenExpirationTime) << QStringLiteral(", error: ") << errorDescription);

    m_authenticationInProgress = false;

    if (success)
    {
        AuthData authData;
        authData.m_userId = userId;
        authData.m_authToken = authToken;
        authData.m_expirationTime = authTokenExpirationTime;
        authData.m_shardId = shardId;
        authData.m_noteStoreUrl = noteStoreUrl;
        authData.m_webApiUrlPrefix = webApiUrlPrefix;

        m_OAuthResult = authData;
        QNDEBUG(QStringLiteral("OAuth result = ") << m_OAuthResult);

        Account previousAccount = m_remoteToLocalSyncManager.account();

        Account newAccount(QString(), Account::Type::Evernote, userId, Account::EvernoteAccountType::Free, m_host);
        m_remoteToLocalSyncManager.setAccount(newAccount);

        m_pUserStore->setAuthenticationToken(authToken);

        ErrorString error;
        bool res = m_remoteToLocalSyncManager.syncUser(userId, error, /* write user data to local storage = */ false);
        if (Q_UNLIKELY(!res))
        {
            errorDescription.setBase(QT_TR_NOOP("Can't switch to new Evernote account: failed to sync user data"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(errorDescription);
            Q_EMIT notifyError(errorDescription);

            m_remoteToLocalSyncManager.setAccount(previousAccount);

            return;
        }

        const User & user = m_remoteToLocalSyncManager.user();
        if (Q_UNLIKELY(!user.hasUsername()))
        {
            errorDescription.setBase(QT_TR_NOOP("Can't switch to new Evernote account: the synched user data lacks username"));
            errorDescription.appendBase(error.base());
            errorDescription.appendBase(error.additionalBases());
            errorDescription.details() = error.details();
            QNWARNING(errorDescription);
            Q_EMIT notifyError(errorDescription);

            m_remoteToLocalSyncManager.setAccount(previousAccount);

            return;
        }

        launchStoreOAuthResult(authData);
    }
    else
    {
        if (m_authContext == AuthContext::Request) {
            Q_EMIT authenticationFinished(/* success = */ false, errorDescription, Account());
        }
        else {
            Q_EMIT notifyError(errorDescription);
        }
    }
}

void SynchronizationManagerPrivate::onWritePasswordJobFinished(QUuid jobId, IKeychainService::ErrorCode::type errorCode,
                                                               ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onWritePasswordJobFinished: job id = ") << jobId
            << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    if (jobId == m_writeAuthTokenJobId) {
        onWriteAuthTokenFinished(errorCode, errorDescription);
        return;
    }

    if (jobId == m_writeShardIdJobId) {
        onWriteShardIdFinished(errorCode, errorDescription);
        return;
    }

    auto writeAuthTokenIt = m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.find(jobId);
    if (writeAuthTokenIt != m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(QStringLiteral("Write linked notebook auth token job finished: linked notebook guid = ") << writeAuthTokenIt->second);

        QString guid = writeAuthTokenIt->second;
        Q_UNUSED(m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.erase(writeAuthTokenIt))

        auto pendingItemIt = m_linkedNotebookAuthTokensPendingWritingByGuid.find(guid);
        if (pendingItemIt != m_linkedNotebookAuthTokensPendingWritingByGuid.end())
        {
            // NOTE: ignore the status of previous write job for this key, it doesn't matter
            // if we need to write another token
            QString token = pendingItemIt.value();
            Q_UNUSED(m_linkedNotebookAuthTokensPendingWritingByGuid.erase(pendingItemIt))
            QNDEBUG(QStringLiteral("Writing postponed auth token for linked notebook guid ") << guid);
            QString keyPrefix = QCoreApplication::applicationName() + QStringLiteral("_") + m_host +
                                QStringLiteral("_") + QString::number(m_OAuthResult.m_userId);
            QUuid jobId = m_pKeychainService->startWritePasswordJob(WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB,
                                                                    keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid,
                                                                    token);
            m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.insert(JobIdWithGuidBimap::value_type(guid, jobId));
        }
        else if (errorCode != IKeychainService::ErrorCode::NoError)
        {
            ErrorString error(QT_TR_NOOP("Error saving linked notebook's authentication token to the keychain"));
            error.appendBase(errorDescription.base());
            error.appendBase(errorDescription.additionalBases());
            error.details() = QStringLiteral("error code = ");
            error.details() += ToString(errorCode);
            const QString & errorDetails = errorDescription.details();
            if (!errorDetails.isEmpty()) {
                error.details() += QStringLiteral(": ");
                error.details() += errorDetails;
            }
            QNWARNING(error);
            Q_EMIT notifyError(error);
        }

        return;
    }

    auto writeShardIdIt = m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.find(jobId);
    if (writeShardIdIt != m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(QStringLiteral("Write linked notebook shard id job finished: linked notebook guid = ") << writeShardIdIt->second);

        QString guid = writeShardIdIt->second;
        Q_UNUSED(m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.erase(writeShardIdIt))

        auto pendingItemIt = m_linkedNotebookShardIdsPendingWritingByGuid.find(guid);
        if (pendingItemIt != m_linkedNotebookShardIdsPendingWritingByGuid.end())
        {
            // NOTE: ignore the status of previous write job for this key, it doesn't matter
            // if we need to write another shard id
            QString shardId = pendingItemIt.value();
            Q_UNUSED(m_linkedNotebookShardIdsPendingWritingByGuid.erase(pendingItemIt))
            QNDEBUG(QStringLiteral("Writing postponed shard id ") << shardId << QStringLiteral(" for linked notebook guid ") << guid);
            QString keyPrefix = QCoreApplication::applicationName() + QStringLiteral("_") + m_host +
                                QStringLiteral("_") + QString::number(m_OAuthResult.m_userId);
            QUuid jobId = m_pKeychainService->startWritePasswordJob(WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB,
                                                                    keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid,
                                                                    shardId);
            m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.insert(JobIdWithGuidBimap::value_type(guid, jobId));
        }
        else if (errorCode != IKeychainService::ErrorCode::NoError)
        {
            ErrorString error(QT_TR_NOOP("Error saving linked notebook's shard id to the keychain"));
            error.appendBase(errorDescription.base());
            error.appendBase(errorDescription.additionalBases());
            error.details() = QStringLiteral("error code = ");
            error.details() += ToString(errorCode);
            const QString & errorDetails = errorDescription.details();
            if (!errorDetails.isEmpty()) {
                error.details() += QStringLiteral(": ");
                error.details() += errorDetails;
            }
            QNWARNING(error);
            Q_EMIT notifyError(error);
        }

        return;
    }

    QNDEBUG(QStringLiteral("Couldn't identify the write password from keychain job"));
}

void SynchronizationManagerPrivate::onReadPasswordJobFinished(QUuid jobId, IKeychainService::ErrorCode::type errorCode,
                                                              ErrorString errorDescription, QString password)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onReadPasswordJobFinished: job id = ") << jobId
            << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    if (jobId == m_readAuthTokenJobId) {
        onReadAuthTokenFinished(errorCode, errorDescription, password);
        return;
    }

    if (jobId == m_readShardIdJobId) {
        onReadShardIdFinished(errorCode, errorDescription, password);
        return;
    }

    auto readAuthTokenIt = m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.find(jobId);
    if (readAuthTokenIt != m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(QStringLiteral("Read linked notebook auth token job finished: linked notebook guid = ") << readAuthTokenIt->second);

        if (errorCode == IKeychainService::ErrorCode::NoError)
        {
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[readAuthTokenIt->second].first = password;
        }
        else if (errorCode == IKeychainService::ErrorCode::EntryNotFound)
        {
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(readAuthTokenIt->second))
        }
        else
        {
            ErrorString error(QT_TR_NOOP("Error reading linked notebook's authentication token from the keychain"));
            error.appendBase(errorDescription.base());
            error.appendBase(errorDescription.additionalBases());
            error.details() = QStringLiteral("error code = ");
            error.details() += ToString(errorCode);
            const QString & errorDetails = errorDescription.details();
            if (!errorDetails.isEmpty()) {
                error.details() += QStringLiteral(": ");
                error.details() += errorDetails;
            }
            QNWARNING(error);
            Q_EMIT notifyError(error);

            // Try to recover by making user to authenticate again in the blind hope that
            // the next time the persistence of auth settings in the keychain would work
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(readAuthTokenIt->second))
        }

        authenticateToLinkedNotebooks();
        Q_UNUSED(m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.right.erase(readAuthTokenIt))
        return;
    }

    auto readShardIdIt = m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.find(jobId);
    if (readShardIdIt != m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.end())
    {
        QNDEBUG(QStringLiteral("Read linked notebook shard id job finished: linked notebook guid = ") << readShardIdIt->second);

        if (errorCode == IKeychainService::ErrorCode::NoError)
        {
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[readAuthTokenIt->second].second = password;
        }
        else if (errorCode == IKeychainService::ErrorCode::EntryNotFound)
        {
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(readShardIdIt->second))
        }
        else
        {
            ErrorString error(QT_TR_NOOP("Error reading linked notebook's shard id from the keychain"));
            error.appendBase(errorDescription.base());
            error.appendBase(errorDescription.additionalBases());
            error.details() = QStringLiteral("error code = ");
            error.details() += ToString(errorCode);
            const QString & errorDetails = errorDescription.details();
            if (!errorDetails.isEmpty()) {
                error.details() += QStringLiteral(": ");
                error.details() += errorDetails;
            }
            QNWARNING(error);
            Q_EMIT notifyError(error);

            // Try to recover by making user to authenticate again in the blind hope that
            // the next time the persistence of auth settings in the keychain would work
            Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(readShardIdIt->second))
        }

        authenticateToLinkedNotebooks();
        Q_UNUSED(m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.right.erase(readShardIdIt))
        return;
    }

    QNDEBUG(QStringLiteral("Couldn't identify the read password from keychain job"));
}

void SynchronizationManagerPrivate::onDeletePasswordJobFinished(QUuid jobId, IKeychainService::ErrorCode::type errorCode,
                                                                ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onDeletePasswordJobFinished: job id = ") << jobId
            << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    if (jobId == m_deleteAuthTokenJobId) {
        onDeleteAuthTokenFinished(errorCode, errorDescription);
        return;
    }

    if (jobId == m_deleteShardIdJobId) {
        onDeleteShardIdFinished(errorCode, errorDescription);
        return;
    }

    QNDEBUG(QStringLiteral("Couldn't identify the delete password from keychain job"));
}

void SynchronizationManagerPrivate::onRequestAuthenticationToken()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRequestAuthenticationToken"));

    if (validAuthentication()) {
        QNDEBUG(QStringLiteral("Found valid auth token and shard id, returning them"));
        Q_EMIT sendAuthenticationTokenAndShardId(m_OAuthResult.m_authToken, m_OAuthResult.m_shardId, m_OAuthResult.m_expirationTime);
        return;
    }

    authenticateImpl(AuthContext::SyncLaunch);
}

void SynchronizationManagerPrivate::onRequestAuthenticationTokensForLinkedNotebooks(QVector<LinkedNotebookAuthData> linkedNotebookAuthData)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRequestAuthenticationTokensForLinkedNotebooks"));
    m_linkedNotebookAuthDataPendingAuthentication = linkedNotebookAuthData;
    authenticateToLinkedNotebooks();
}

void SynchronizationManagerPrivate::onRequestLastSyncParameters()
{
    if (m_onceReadLastSyncParams) {
        Q_EMIT sendLastSyncParameters(m_lastUpdateCount, m_lastSyncTime, m_cachedLinkedNotebookLastUpdateCountByGuid,
                                      m_cachedLinkedNotebookLastSyncTimeByGuid);
        return;
    }

    readLastSyncParameters();

    Q_EMIT sendLastSyncParameters(m_lastUpdateCount, m_lastSyncTime, m_cachedLinkedNotebookLastUpdateCountByGuid,
                                  m_cachedLinkedNotebookLastSyncTimeByGuid);
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncFinished(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
                                                                QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid,
                                                                QHash<QString,qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRemoteToLocalSyncFinished: lastUpdateCount = ")
            << lastUpdateCount << QStringLiteral(", lastSyncTime = ") << printableDateTimeFromTimestamp(lastSyncTime));

    bool somethingDownloaded = (m_lastUpdateCount != lastUpdateCount) ||
                               (m_lastUpdateCount != m_previousUpdateCount) ||
                               (m_cachedLinkedNotebookLastUpdateCountByGuid != lastUpdateCountByLinkedNotebookGuid);
    QNTRACE(QStringLiteral("Something downloaded = ") << (somethingDownloaded ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", m_lastUpdateCount = ") << m_lastUpdateCount << QStringLiteral(", m_previousUpdateCount = ")
            << m_previousUpdateCount << QStringLiteral(", m_cachedLinkedNotebookLastUpdateCountByGuid = ")
            << m_cachedLinkedNotebookLastUpdateCountByGuid);

    m_lastUpdateCount = lastUpdateCount;
    m_previousUpdateCount = lastUpdateCount;
    m_lastSyncTime = lastSyncTime;
    m_cachedLinkedNotebookLastUpdateCountByGuid = lastUpdateCountByLinkedNotebookGuid;
    m_cachedLinkedNotebookLastSyncTimeByGuid = lastSyncTimeByLinkedNotebookGuid;

    updatePersistentSyncSettings();

    m_onceReadLastSyncParams = true;
    m_somethingDownloaded = somethingDownloaded;
    Q_EMIT notifyRemoteToLocalSyncDone(m_somethingDownloaded);

    sendChanges();
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncStopped()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRemoteToLocalSyncStopped"));
    Q_EMIT remoteToLocalSyncStopped();

    if (!m_sendLocalChangesManager.active()) {
        Q_EMIT notifyStop();
    }
}

void SynchronizationManagerPrivate::onRemoteToLocalSyncFailure(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRemoteToLocalSyncFailure: ") << errorDescription);

    Q_EMIT stopRemoteToLocalSync();
    Q_EMIT stopSendingLocalChanges();
    Q_EMIT notifyError(errorDescription);
}

void SynchronizationManagerPrivate::onRemoteToLocalSynchronizedContentFromUsersOwnAccount(qint32 lastUpdateCount,
                                                                                          qevercloud::Timestamp lastSyncTime)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRemoteToLocalSynchronizedContentFromUsersOwnAccount: last update count = ")
            << lastUpdateCount << QStringLiteral(", last sync time = ") << printableDateTimeFromTimestamp(lastSyncTime));

    m_lastUpdateCount = lastUpdateCount;
    m_lastSyncTime = lastSyncTime;

    updatePersistentSyncSettings();
}

void SynchronizationManagerPrivate::onShouldRepeatIncrementalSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onShouldRepeatIncrementalSync"));

    m_shouldRepeatIncrementalSyncAfterSendingChanges = true;
    Q_EMIT willRepeatRemoteToLocalSyncAfterSendingChanges();
}

void SynchronizationManagerPrivate::onConflictDetectedDuringLocalChangesSending()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onConflictDetectedDuringLocalChangesSending"));

    Q_EMIT detectedConflictDuringLocalChangesSending();

    m_sendLocalChangesManager.stop();

    // NOTE: the detection of non-synchronized state with respect to remote service often precedes the actual conflict detection;
    // need to drop this flag to prevent launching the incremental sync after sending the local changes after the incremental sync
    // which we'd launch now
    m_shouldRepeatIncrementalSyncAfterSendingChanges = false;

    launchIncrementalSync();
}

void SynchronizationManagerPrivate::onLocalChangesSent(qint32 lastUpdateCount, QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onLocalChangesSent: last update count = ") << lastUpdateCount
            << QStringLiteral(", last update count per linked notebook guid: ") << lastUpdateCountByLinkedNotebookGuid);

    bool somethingSent = (m_lastUpdateCount != lastUpdateCount) ||
                         (m_cachedLinkedNotebookLastUpdateCountByGuid != lastUpdateCountByLinkedNotebookGuid);

    m_lastUpdateCount = lastUpdateCount;
    m_cachedLinkedNotebookLastUpdateCountByGuid = lastUpdateCountByLinkedNotebookGuid;

    updatePersistentSyncSettings();

    if (m_shouldRepeatIncrementalSyncAfterSendingChanges) {
        QNDEBUG(QStringLiteral("Repeating the incremental sync after sending the changes"));
        m_shouldRepeatIncrementalSyncAfterSendingChanges = false;
        launchIncrementalSync();
        return;
    }

    QNINFO(QStringLiteral("Finished the whole synchronization procedure!"));

    bool somethingDownloaded = m_somethingDownloaded;
    m_somethingDownloaded = false;

    Q_EMIT notifyFinish(m_remoteToLocalSyncManager.account(), somethingDownloaded, somethingSent);
}

void SynchronizationManagerPrivate::onSendLocalChangesStopped()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onSendLocalChangesStopped"));
    Q_EMIT sendLocalChangesStopped();

    if (!m_remoteToLocalSyncManager.active()) {
        Q_EMIT notifyStop();
    }
}

void SynchronizationManagerPrivate::onSendLocalChangesFailure(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onSendLocalChangesFailure: ") << errorDescription);

    stop();
    Q_EMIT notifyError(errorDescription);
}

void SynchronizationManagerPrivate::onRateLimitExceeded(qint32 secondsToWait)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onRateLimitExceeded"));

    // Before re-sending this signal to the outside world will attempt to collect the update sequence numbers
    // for the next sync, either for user's own account or for each linked notebook - depending on what has been
    // synced right before the Evernote API rate limit was exceeded. The collected update sequence numbers would
    // be used to update the persistent sync settings. So that if the sync ends now before it's automatically restarted
    // after the required waiting time (for example, user quits the app now), the next time we'll request the sync chunks
    // after the last properly processed USN, either for user's own account or for linked notebooks, so that we won't
    // re-download the same stuff over and over again and hit the rate limit at the very same sync stage

    tryUpdateLastSyncStatus();
    Q_EMIT rateLimitExceeded(secondsToWait);
}

void SynchronizationManagerPrivate::createConnections(IAuthenticationManager & authenticationManager)
{
    // Connections with authentication manager
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,requestAuthentication),
                     &authenticationManager, QNSLOT(IAuthenticationManager,onAuthenticationRequest));
    QObject::connect(&authenticationManager, QNSIGNAL(IAuthenticationManager,sendAuthenticationResult,bool,qevercloud::UserID,
                                                      QString,qevercloud::Timestamp,QString,QString,QString,ErrorString),
                     this, QNSLOT(SynchronizationManagerPrivate,onOAuthResult,bool,qevercloud::UserID,
                                  QString,qevercloud::Timestamp,QString,QString,QString,ErrorString));

    // Connections with keychain service
    QObject::connect(m_pKeychainService, QNSIGNAL(IKeychainService,writePasswordJobFinished,QUuid,ErrorCode::type,ErrorString),
                     this, QNSLOT(SynchronizationManagerPrivate,onWritePasswordJobFinished,QUuid,ErrorCode::type,ErrorString));
    QObject::connect(m_pKeychainService, QNSIGNAL(IKeychainService,readPasswordJobFinished,QUuid,ErrorCode::type,ErrorString,QString),
                     this, QNSLOT(SynchronizationManagerPrivate,onReadPasswordJobFinished,QUuid,ErrorCode::type,ErrorString,QString));
    QObject::connect(m_pKeychainService, QNSIGNAL(IKeychainService,deletePasswordJobFinished,QUuid,ErrorCode::type,ErrorString),
                     this, QNSLOT(SynchronizationManagerPrivate,onDeletePasswordJobFinished,QUuid,ErrorCode::type,ErrorString));

    // Connections with remote to local synchronization manager
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,finished,qint32,qevercloud::Timestamp,QHash<QString,qint32>,
                                                           QHash<QString,qevercloud::Timestamp>),
                     this, QNSLOT(SynchronizationManagerPrivate,onRemoteToLocalSyncFinished,qint32,qevercloud::Timestamp,QHash<QString,qint32>,
                                  QHash<QString,qevercloud::Timestamp>));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,rateLimitExceeded,qint32),
                     this, QNSLOT(SynchronizationManagerPrivate,onRateLimitExceeded,qint32));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,requestAuthenticationToken),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationToken));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,requestAuthenticationTokensForLinkedNotebooks,QVector<LinkedNotebookAuthData>),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationTokensForLinkedNotebooks,QVector<LinkedNotebookAuthData>));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,stopped),
                     this, QNSLOT(SynchronizationManagerPrivate,onRemoteToLocalSyncStopped));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,failure,ErrorString),
                     this, QNSLOT(SynchronizationManagerPrivate,onRemoteToLocalSyncFailure,ErrorString));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,synchronizedContentFromUsersOwnAccount,qint32,qevercloud::Timestamp),
                     this, QNSLOT(SynchronizationManagerPrivate,onRemoteToLocalSynchronizedContentFromUsersOwnAccount,qint32,qevercloud::Timestamp));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,requestLastSyncParameters),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestLastSyncParameters));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,syncChunksDownloadProgress,qint32,qint32,qint32),
                     this, QNSIGNAL(SynchronizationManagerPrivate,syncChunksDownloadProgress,qint32,qint32,qint32));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,syncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManagerPrivate,syncChunksDownloaded));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,notesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManagerPrivate,notesDownloadProgress,quint32,quint32));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,linkedNotebookSyncChunksDownloadProgress,qint32,qint32,qint32,LinkedNotebook),
                     this, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebookSyncChunksDownloadProgress,qint32,qint32,qint32,LinkedNotebook));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,linkedNotebooksSyncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksSyncChunksDownloaded));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,resourcesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManagerPrivate,resourcesDownloadProgress,quint32,quint32));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,linkedNotebooksResourcesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksResourcesDownloadProgress,quint32,quint32));
    QObject::connect(&m_remoteToLocalSyncManager, QNSIGNAL(RemoteToLocalSynchronizationManager,linkedNotebooksNotesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksNotesDownloadProgress,quint32,quint32));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,stopRemoteToLocalSync),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,stop));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendAuthenticationTokenAndShardId,QString,QString,qevercloud::Timestamp),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,onAuthenticationInfoReceived,QString,QString,qevercloud::Timestamp));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendAuthenticationTokensForLinkedNotebooks,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,onAuthenticationTokensForLinkedNotebooksReceived,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendLastSyncParameters,qint32,qevercloud::Timestamp,QHash<QString,qint32>,QHash<QString,qevercloud::Timestamp>),
                     &m_remoteToLocalSyncManager, QNSLOT(RemoteToLocalSynchronizationManager,onLastSyncParametersReceived,qint32,qevercloud::Timestamp,QHash<QString,qint32>,QHash<QString,qevercloud::Timestamp>));

    // Connections with send local changes manager
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,finished,qint32,QHash<QString,qint32>),
                     this, QNSLOT(SynchronizationManagerPrivate,onLocalChangesSent,qint32,QHash<QString,qint32>));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,rateLimitExceeded,qint32),
                     this, QNSLOT(SynchronizationManagerPrivate,onRateLimitExceeded,qint32));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,requestAuthenticationToken),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationToken));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,requestAuthenticationTokensForLinkedNotebooks,QVector<LinkedNotebookAuthData>),
                     this, QNSLOT(SynchronizationManagerPrivate,onRequestAuthenticationTokensForLinkedNotebooks,QVector<LinkedNotebookAuthData>));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,shouldRepeatIncrementalSync),
                     this, QNSLOT(SynchronizationManagerPrivate,onShouldRepeatIncrementalSync));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,conflictDetected),
                     this, QNSLOT(SynchronizationManagerPrivate,onConflictDetectedDuringLocalChangesSending));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,stopped),
                     this, QNSLOT(SynchronizationManagerPrivate,onSendLocalChangesStopped));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,failure,ErrorString),
                     this, QNSLOT(SynchronizationManagerPrivate,onSendLocalChangesFailure,ErrorString));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,receivedUserAccountDirtyObjects),
                     this, QNSIGNAL(SynchronizationManagerPrivate,preparedDirtyObjectsForSending));
    QObject::connect(&m_sendLocalChangesManager, QNSIGNAL(SendLocalChangesManager,receivedDirtyObjectsFromLinkedNotebooks),
                     this, QNSIGNAL(SynchronizationManagerPrivate,preparedLinkedNotebooksDirtyObjectsForSending));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,sendAuthenticationTokensForLinkedNotebooks,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>),
                     &m_sendLocalChangesManager, QNSLOT(SendLocalChangesManager,onAuthenticationTokensForLinkedNotebooksReceived,QHash<QString,QPair<QString,QString> >,QHash<QString,qevercloud::Timestamp>));
    QObject::connect(this, QNSIGNAL(SynchronizationManagerPrivate,stopSendingLocalChanges),
                     &m_sendLocalChangesManager, QNSLOT(SendLocalChangesManager,stop));
}

void SynchronizationManagerPrivate::readLastSyncParameters()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::readLastSyncParameters"));

    m_lastSyncTime = 0;
    m_lastUpdateCount = 0;
    m_previousUpdateCount = 0;
    m_cachedLinkedNotebookLastUpdateCountByGuid.clear();
    m_cachedLinkedNotebookLastSyncTimeByGuid.clear();

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    const QString keyGroup = QStringLiteral("Synchronization/") + m_host + QStringLiteral("/") +
                             QString::number(m_OAuthResult.m_userId) + QStringLiteral("/") +
                             LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");

    QVariant lastUpdateCountVar = appSettings.value(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY);
    if (!lastUpdateCountVar.isNull())
    {
        bool conversionResult = false;
        m_lastUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read last update count from persistent application settings"));
            m_lastUpdateCount = 0;
        }
        m_previousUpdateCount = m_lastUpdateCount;
    }

    QVariant lastSyncTimeVar = appSettings.value(keyGroup + LAST_SYNC_TIME_KEY);
    if (!lastUpdateCountVar.isNull())
    {
        bool conversionResult = false;
        m_lastSyncTime = lastSyncTimeVar.toLongLong(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read last sync time from persistent application settings"));
            m_lastSyncTime = 0;
        }
    }

    int numLinkedNotebooksSyncParams = appSettings.beginReadArray(keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS);
    for(int i = 0; i < numLinkedNotebooksSyncParams; ++i)
    {
        appSettings.setArrayIndex(i);

        QString guid = appSettings.value(LINKED_NOTEBOOK_GUID_KEY).toString();
        if (guid.isEmpty()) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's guid from persistent application settings"));
            continue;
        }

        QVariant lastUpdateCountVar = appSettings.value(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY);
        bool conversionResult = false;
        qint32 lastUpdateCount = lastUpdateCountVar.toInt(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's last update count from persistent application settings"));
            continue;
        }

        QVariant lastSyncTimeVar = appSettings.value(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY);
        conversionResult = false;
        qevercloud::Timestamp lastSyncTime = lastSyncTimeVar.toLongLong(&conversionResult);
        if (!conversionResult) {
            QNWARNING(QStringLiteral("Couldn't read linked notebook's last sync time from persistent application settings"));
            continue;
        }

        m_cachedLinkedNotebookLastUpdateCountByGuid[guid] = lastUpdateCount;
        m_cachedLinkedNotebookLastSyncTimeByGuid[guid] = lastSyncTime;
    }
    appSettings.endArray();

    m_onceReadLastSyncParams = true;
}

void SynchronizationManagerPrivate::authenticateImpl(const AuthContext::type authContext)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::authenticateImpl: auth context = ") << authContext);

    m_authContext = authContext;

    if (m_authContext == AuthContext::Request) {
        QNDEBUG(QStringLiteral("Authentication of the new user is requested, proceeding to OAuth"));
        launchOAuth();
        return;
    }

    if (m_OAuthResult.m_userId < 0) {
        QNDEBUG(QStringLiteral("No current user id, launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    if (validAuthentication()) {
        QNDEBUG(QStringLiteral("Found already valid authentication info"));
        finalizeAuthentication();
        return;
    }

    QNTRACE(QStringLiteral("Trying to restore persistent authentication settings..."));

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_OAuthResult.m_userId) + QStringLiteral("/");

    QVariant tokenExpirationValue = appSettings.value(keyGroup + EXPIRATION_TIMESTAMP_KEY);
    if (tokenExpirationValue.isNull()) {
        QNINFO(QStringLiteral("Authentication token expiration timestamp was not found within application settings, "
                              "assuming it has never been written & launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    bool conversionResult = false;
    qevercloud::Timestamp tokenExpirationTimestamp = tokenExpirationValue.toLongLong(&conversionResult);
    if (!conversionResult) {
        ErrorString error(QT_TR_NOOP("Internal error: failed to convert QVariant with authentication token "
                                     "expiration timestamp to the actual timestamp"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    if (checkIfTimestampIsAboutToExpireSoon(tokenExpirationTimestamp)) {
        QNINFO(QStringLiteral("Authentication token stored in persistent application settings is about to expire soon enough, "
                              "launching the OAuth procedure"));
        launchOAuth();
        return;
    }

    m_OAuthResult.m_expirationTime = tokenExpirationTimestamp;

    QNTRACE(QStringLiteral("Restoring persistent note store url"));

    QVariant noteStoreUrlValue = appSettings.value(keyGroup + NOTE_STORE_URL_KEY);
    if (noteStoreUrlValue.isNull()) {
        ErrorString error(QT_TR_NOOP("Failed to find the note store url within persistent application settings"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    QString noteStoreUrl = noteStoreUrlValue.toString();
    if (noteStoreUrl.isEmpty()) {
        ErrorString error(QT_TR_NOOP("Internal error: failed to convert the note store url from QVariant to QString"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    m_OAuthResult.m_noteStoreUrl = noteStoreUrl;

    QNDEBUG(QStringLiteral("Restoring persistent web api url prefix"));

    QVariant webApiUrlPrefixValue = appSettings.value(keyGroup + WEB_API_URL_PREFIX_KEY);
    if (webApiUrlPrefixValue.isNull()) {
        ErrorString error(QT_TR_NOOP("Failed to find the web API url prefix within persistent application settings"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    QString webApiUrlPrefix = webApiUrlPrefixValue.toString();
    if (webApiUrlPrefix.isEmpty()) {
        ErrorString error(QT_TR_NOOP("Failed to convert the web api url prefix from QVariant to QString"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    m_OAuthResult.m_webApiUrlPrefix = webApiUrlPrefix;

    QNDEBUG(QStringLiteral("Trying to restore the authentication token and the shard id from the keychain"));

    m_readingAuthToken = true;
    QString readAuthTokenService = QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;
    QString readAuthTokenKey = QCoreApplication::applicationName() + QStringLiteral("_auth_token_") +
                               m_host + QStringLiteral("_") + QString::number(m_OAuthResult.m_userId);
    m_readAuthTokenJobId = m_pKeychainService->startReadPasswordJob(readAuthTokenService, readAuthTokenKey);

    m_readingShardId = true;
    QString readShardIdService = QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;
    QString readShardIdKey = QCoreApplication::applicationName() + QStringLiteral("_shard_id_") +
                             m_host + QStringLiteral("_") + QString::number(m_OAuthResult.m_userId);
    m_readShardIdJobId = m_pKeychainService->startReadPasswordJob(readShardIdService, readShardIdKey);
}

void SynchronizationManagerPrivate::launchOAuth()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchOAuth"));

    m_authenticationInProgress = true;
    Q_EMIT requestAuthentication();
}

void SynchronizationManagerPrivate::launchSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchSync"));

    if (!m_onceReadLastSyncParams) {
        readLastSyncParameters();
    }

    Q_EMIT notifyStart();

    m_pNoteStore->setNoteStoreUrl(m_OAuthResult.m_noteStoreUrl);
    m_pNoteStore->setAuthenticationToken(m_OAuthResult.m_authToken);
    m_pUserStore->setAuthenticationToken(m_OAuthResult.m_authToken);

    if (m_lastUpdateCount <= 0) {
        QNDEBUG(QStringLiteral("The client has never synchronized with the remote service, "
                               "performing the full sync"));
        launchFullSync();
        return;
    }

    QNDEBUG(QStringLiteral("Performing incremental sync"));
    launchIncrementalSync();
}

void SynchronizationManagerPrivate::launchFullSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchFullSync"));

    m_somethingDownloaded = false;
    m_remoteToLocalSyncManager.start();
}

void SynchronizationManagerPrivate::launchIncrementalSync()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::launchIncrementalSync: m_lastUpdateCount = ")
            << m_lastUpdateCount);

    m_somethingDownloaded = false;
    m_remoteToLocalSyncManager.start(m_lastUpdateCount);
}

void SynchronizationManagerPrivate::sendChanges()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::sendChanges"));
    m_sendLocalChangesManager.start(m_lastUpdateCount, m_cachedLinkedNotebookLastUpdateCountByGuid);
}

void SynchronizationManagerPrivate::launchStoreOAuthResult(const AuthData & result)
{
    m_writtenOAuthResult = result;

    m_writingAuthToken = true;
    QString writeAuthTokenService = QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;
    QString writeAuthTokenKey = QCoreApplication::applicationName() + QStringLiteral("_auth_token_") +
                                m_host + QStringLiteral("_") + QString::number(result.m_userId);
    m_writeAuthTokenJobId = m_pKeychainService->startWritePasswordJob(writeAuthTokenService, writeAuthTokenKey, result.m_authToken);

    m_writingShardId = true;
    QString writeShardIdService = QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;
    QString writeShardIdKey = QCoreApplication::applicationName() + QStringLiteral("_shard_id_") +
                              m_host + QStringLiteral("_") + QString::number(result.m_userId);
    m_writeShardIdJobId = m_pKeychainService->startWritePasswordJob(writeShardIdService, writeShardIdKey, result.m_shardId);
}

void SynchronizationManagerPrivate::finalizeStoreOAuthResult()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::finalizeStoreOAuthResult"));

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_writtenOAuthResult.m_userId) + QStringLiteral("/");

    appSettings.setValue(keyGroup + NOTE_STORE_URL_KEY, m_writtenOAuthResult.m_noteStoreUrl);
    appSettings.setValue(keyGroup + EXPIRATION_TIMESTAMP_KEY, m_writtenOAuthResult.m_expirationTime);
    appSettings.setValue(keyGroup + WEB_API_URL_PREFIX_KEY, m_writtenOAuthResult.m_webApiUrlPrefix);

    QNDEBUG(QStringLiteral("Successfully wrote the authentication result info to the application settings for host ")
            << m_host << QStringLiteral(", user id ") << m_writtenOAuthResult.m_userId << QStringLiteral(": ")
            << QStringLiteral(": auth token expiration timestamp = ") << printableDateTimeFromTimestamp(m_writtenOAuthResult.m_expirationTime)
            << QStringLiteral(", web API url prefix = ") << m_writtenOAuthResult.m_webApiUrlPrefix);

    finalizeAuthentication();
}

void SynchronizationManagerPrivate::finalizeAuthentication()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::finalizeAuthentication: result = ") << m_OAuthResult);

    switch(m_authContext)
    {
    case AuthContext::Blank:
    {
        ErrorString error(QT_TR_NOOP("Internal error: incorrect authentication context: blank"));
        Q_EMIT notifyError(error);
        break;
    }
    case AuthContext::SyncLaunch:
    {
        launchSync();
        break;
    }
    case AuthContext::Request:
    {
        Account account = m_remoteToLocalSyncManager.account();
        QNDEBUG(QStringLiteral("Emitting the authenticationFinished signal: ") << account);
        Q_EMIT authenticationFinished(/* success = */ true, ErrorString(), account);

        m_writtenOAuthResult = AuthData();
        m_writtenOAuthResult.m_userId = -1;
        break;
    }
    case AuthContext::AuthToLinkedNotebooks:
        authenticateToLinkedNotebooks();
        break;
    default:
    {
        ErrorString error(QT_TR_NOOP("Internal error: unknown authentication context"));
        error.details() = ToString(m_authContext);
        Q_EMIT notifyError(error);
        break;
    }
    }

    m_authContext = AuthContext::Blank;
}

void SynchronizationManagerPrivate::timerEvent(QTimerEvent * pTimerEvent)
{
    if (Q_UNLIKELY(!pTimerEvent)) {
        ErrorString errorDescription(QT_TR_NOOP("Qt error: detected null pointer to QTimerEvent"));
        QNWARNING(errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    int timerId = pTimerEvent->timerId();
    killTimer(timerId);

    QNDEBUG(QStringLiteral("Timer event for timer id ") << timerId);

    if (timerId == m_launchSyncPostponeTimerId)  {
        QNDEBUG(QStringLiteral("Re-launching the sync procedure due to RATE_LIMIT_REACHED exception "
                               "when trying to get the sync state the last time"));
        launchSync();
        return;
    }

    if (timerId == m_authenticateToLinkedNotebooksPostponeTimerId)  {
        QNDEBUG(QStringLiteral("Re-attempting to authenticate to remaining linked (shared) notebooks"));
        onRequestAuthenticationTokensForLinkedNotebooks(m_linkedNotebookAuthDataPendingAuthentication);
        return;
    }
}

void SynchronizationManagerPrivate::clear()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::clear"));

    m_lastUpdateCount = -1;
    m_previousUpdateCount = -1;
    m_lastSyncTime = -1;
    m_cachedLinkedNotebookLastUpdateCountByGuid.clear();
    m_cachedLinkedNotebookLastSyncTimeByGuid.clear();
    m_onceReadLastSyncParams = false;

    m_authContext = AuthContext::Blank;

    m_launchSyncPostponeTimerId = -1;

    m_pNoteStore->stop();

    for(auto it = m_noteStoresByLinkedNotebookGuids.begin(),
        end = m_noteStoresByLinkedNotebookGuids.end(); it != end; ++it)
    {
        INoteStore * pNoteStore = it.value();
        pNoteStore->stop();
        pNoteStore->setParent(Q_NULLPTR);
        pNoteStore->deleteLater();
    }

    m_noteStoresByLinkedNotebookGuids.clear();

    m_remoteToLocalSyncManager.stop();
    m_somethingDownloaded = false;

    m_sendLocalChangesManager.stop();

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

    return !checkIfTimestampIsAboutToExpireSoon(m_OAuthResult.m_expirationTime);
}

bool SynchronizationManagerPrivate::checkIfTimestampIsAboutToExpireSoon(const qevercloud::Timestamp timestamp) const
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::checkIfTimestampIsAboutToExpireSoon: ")
            << printableDateTimeFromTimestamp(timestamp));

    qevercloud::Timestamp currentTimestamp = QDateTime::currentMSecsSinceEpoch();
    QNTRACE(QStringLiteral("Current datetime: ") << printableDateTimeFromTimestamp(currentTimestamp));

    if ((timestamp - currentTimestamp) < HALF_AN_HOUR_IN_MSEC) {
        return true;
    }

    return false;
}

void SynchronizationManagerPrivate::authenticateToLinkedNotebooks()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::authenticateToLinkedNotebooks"));

    if (Q_UNLIKELY(m_OAuthResult.m_userId < 0)) {
        ErrorString error(QT_TR_NOOP("Detected attempt to authenticate to linked notebooks while there is no user id set to the synchronization manager"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    const int numLinkedNotebooks = m_linkedNotebookAuthDataPendingAuthentication.size();
    if (numLinkedNotebooks == 0) {
        QNDEBUG(QStringLiteral("No linked notebooks waiting for authentication, sending the cached auth tokens, shard ids and expiration times"));
        Q_EMIT sendAuthenticationTokensForLinkedNotebooks(m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
                                                          m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
        return;
    }

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);
    QString keyGroup = QStringLiteral("Authentication/") + m_host + QStringLiteral("/") +
                       QString::number(m_OAuthResult.m_userId) + QStringLiteral("/");

    QHash<QString,QPair<QString,QString> >  authTokensAndShardIdsToCacheByGuid;
    QHash<QString,qevercloud::Timestamp>    authTokenExpirationTimestampsToCacheByGuid;

    QString keyPrefix = QCoreApplication::applicationName() + QStringLiteral("_") + m_host +
                        QStringLiteral("_") + QString::number(m_OAuthResult.m_userId);

    for(auto it = m_linkedNotebookAuthDataPendingAuthentication.begin();
        it != m_linkedNotebookAuthDataPendingAuthentication.end(); )
    {
        const LinkedNotebookAuthData & authData = *it;

        const QString & guid = authData.m_guid;
        const QString & shardId = authData.m_shardId;
        const QString & sharedNotebookGlobalId = authData.m_sharedNotebookGlobalId;
        const QString & uri = authData.m_uri;
        const QString & noteStoreUrl = authData.m_noteStoreUrl;
        QNDEBUG(QStringLiteral("Processing linked notebook guid = ") << guid
                << QStringLiteral(", shard id = ") << shardId
                << QStringLiteral(", shared notebook global id = ") << sharedNotebookGlobalId
                << QStringLiteral(", uri = ") << uri
                << QStringLiteral(", note store URL = ") << noteStoreUrl);

        if (sharedNotebookGlobalId.isEmpty() && !uri.isEmpty()) {
            // This appears to be a public notebook and per the official
            // documentation from Evernote (dev.evernote.com/media/pdf/edam-sync.pdf)
            // it doesn't need the authentication token at all so will use
            // empty string for its authentication token
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[guid] = QPair<QString, QString>(QString(), shardId);
            m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid[guid] = std::numeric_limits<qint64>::max();

            it = m_linkedNotebookAuthDataPendingAuthentication.erase(it);
            continue;
        }

        bool forceRemoteAuth = false;
        auto linkedNotebookAuthTokenIt = m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.find(guid);
        if (linkedNotebookAuthTokenIt == m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.end())
        {
            auto noAuthDataIt = m_linkedNotebookGuidsWithoutLocalAuthData.find(guid);
            if (noAuthDataIt != m_linkedNotebookGuidsWithoutLocalAuthData.end())
            {
                forceRemoteAuth = true;
                Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.erase(noAuthDataIt))
            }
            else
            {
                QNDEBUG(QStringLiteral("Haven't found the authentication token and shard id for linked notebook guid ") << guid
                        << QStringLiteral(" in the local cache, will try to read them from the keychain"));

                // 1) Set up the job of reading the authentication token
                auto readAuthTokenJobIt = m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left.find(guid);
                if (readAuthTokenJobIt == m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left.end()) {
                    QUuid jobId = m_pKeychainService->startReadPasswordJob(READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB,
                                                                           keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid);
                    m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.insert(JobIdWithGuidBimap::value_type(guid, jobId));
                }

                // 2) Set up the job reading the shard id
                auto readShardIdJobIt = m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left.find(guid);
                if (readShardIdJobIt == m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left.end()) {
                    QUuid jobId = m_pKeychainService->startReadPasswordJob(READ_LINKED_NOTEBOOK_SHARD_ID_JOB,
                                                                           keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid);
                    m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.insert(JobIdWithGuidBimap::value_type(guid, jobId));
                }

                ++it;
                continue;
            }
        }

        if (!forceRemoteAuth)
        {
            auto linkedNotebookAuthTokenExpirationIt = m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.find(guid);
            if (linkedNotebookAuthTokenExpirationIt == m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end())
            {
                QVariant expirationTimeVariant = appSettings.value(keyGroup + LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX + guid);
                if (!expirationTimeVariant.isNull())
                {
                    bool conversionResult = false;
                    qevercloud::Timestamp expirationTime = expirationTimeVariant.toLongLong(&conversionResult);
                    if (conversionResult) {
                        linkedNotebookAuthTokenExpirationIt = m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.insert(guid, expirationTime);
                    }
                    else {
                        QNWARNING(QStringLiteral("Can't convert linked notebook's authentication token's expiration time from QVariant retrieved from ")
                                  << QStringLiteral("app settings into timestamp: linked notebook guid = ") << guid
                                  << QStringLiteral(", variant = ") << expirationTimeVariant);
                    }
                }
            }

            if ( (linkedNotebookAuthTokenExpirationIt != m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid.end()) &&
                 !checkIfTimestampIsAboutToExpireSoon(linkedNotebookAuthTokenExpirationIt.value()) )
            {
                QNDEBUG(QStringLiteral("Found authentication data for linked notebook guid ") << guid
                        << QStringLiteral(" + verified its expiration timestamp"));
                it = m_linkedNotebookAuthDataPendingAuthentication.erase(it);
                continue;
            }
        }

        QNDEBUG(QStringLiteral("Authentication data for linked notebook guid ") << guid
                << QStringLiteral(" was either not found in local cache (and/or app settings / keychain) ")
                << QStringLiteral("or has expired, need to receive that from remote Evernote service"));

        if (m_authenticateToLinkedNotebooksPostponeTimerId >= 0) {
            QNDEBUG(QStringLiteral("Authenticate to linked notebook postpone timer is active, will wait "
                                   "to preserve the breach of Evernote rate API limit"));
            ++it;
            continue;
        }

        if (m_authContext != AuthContext::Blank) {
            QNDEBUG(QStringLiteral("Authentication context variable is not set to blank which means ")
                    << QStringLiteral("that authentication must be in progress: ")
                    << m_authContext << QStringLiteral("; won't attempt to call remote Evernote API at this time"));
            ++it;
            continue;
        }

        qevercloud::AuthenticationResult authResult;
        ErrorString errorDescription;
        qint32 rateLimitSeconds = 0;

        INoteStore * pNoteStore = noteStoreForLinkedNotebookGuid(guid);
        if (Q_UNLIKELY(!pNoteStore)) {
            ErrorString error(QT_TR_NOOP("Can't sync the linked notebook contents: can't find or create the note store for the linked notebook"));
            Q_EMIT notifyError(error);
            return;
        }

        pNoteStore->setAuthenticationToken(m_OAuthResult.m_authToken);
        pNoteStore->setNoteStoreUrl(noteStoreUrl);

        qint32 errorCode = pNoteStore->authenticateToSharedNotebook(sharedNotebookGlobalId, authResult,
                                                                    errorDescription, rateLimitSeconds);
        if (errorCode == qevercloud::EDAMErrorCode::AUTH_EXPIRED)
        {
            if (validAuthentication()) {
                ErrorString error(QT_TR_NOOP("Unexpected AUTH_EXPIRED error"));
                error.additionalBases().append(errorDescription.base());
                error.additionalBases().append(errorDescription.additionalBases());
                error.details() = errorDescription.details();
                Q_EMIT notifyError(error);
            }
            else {
                authenticateImpl(AuthContext::AuthToLinkedNotebooks);
            }

            ++it;
            continue;
        }
        else if (errorCode == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
        {
            if (rateLimitSeconds <= 0) {
                errorDescription.setBase(QT_TR_NOOP("Rate limit reached but the number of seconds to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                Q_EMIT notifyError(errorDescription);
                return;
            }

            m_authenticateToLinkedNotebooksPostponeTimerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));

            ++it;
            continue;
        }
        else if (errorCode != 0)
        {
            QNWARNING(QStringLiteral("Failed to authenticate to shared notebook: ") << errorDescription
                      << QStringLiteral(" (error code = ") << errorCode << QStringLiteral(")"));
            Q_EMIT notifyError(errorDescription);
            return;
        }

        QNDEBUG(QStringLiteral("Retrieved authentication: server-side result generation time (currentTime) = ")
                << printableDateTimeFromTimestamp(authResult.currentTime)
                << QStringLiteral(", expiration time for the authentication result (expiration): ")
                << printableDateTimeFromTimestamp(authResult.expiration)
                << QStringLiteral(", user: ") << (authResult.user.isSet() ? ToString(authResult.user.ref()) : QStringLiteral("<empty>")));

        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[guid] = QPair<QString, QString>(authResult.authenticationToken, shardId);
        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid[guid] = authResult.expiration;

        QPair<QString,QString> & authTokenAndShardId = authTokensAndShardIdsToCacheByGuid[guid];
        authTokenAndShardId.first = authResult.authenticationToken;
        authTokenAndShardId.second = shardId;

        authTokenExpirationTimestampsToCacheByGuid[guid] = authResult.expiration;

        it = m_linkedNotebookAuthDataPendingAuthentication.erase(it);
    }

    if (m_linkedNotebookAuthDataPendingAuthentication.isEmpty()) {
        QNDEBUG(QStringLiteral("Retrieved authentication data for all requested linked notebooks, sending the answer now"));
        Q_EMIT sendAuthenticationTokensForLinkedNotebooks(m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
                                                          m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
    }

    // Caching linked notebook's authentication token's expiration time in app settings
    typedef QHash<QString,qevercloud::Timestamp>::const_iterator ExpirationTimeCIter;
    ExpirationTimeCIter authTokenExpirationTimesToCacheEnd = authTokenExpirationTimestampsToCacheByGuid.end();
    for(ExpirationTimeCIter it = authTokenExpirationTimestampsToCacheByGuid.begin();
        it != authTokenExpirationTimesToCacheEnd; ++it)
    {
        QString key = LINKED_NOTEBOOK_EXPIRATION_TIMESTAMP_KEY_PREFIX + it.key();
        appSettings.setValue(keyGroup + key, QVariant(it.value()));
    }

    // Caching linked notebook's authentication tokens and shard ids in the keychain

    for(auto it = authTokensAndShardIdsToCacheByGuid.begin(), end = authTokensAndShardIdsToCacheByGuid.end();
        it != end; ++it)
    {
        const QString & guid = it.key();
        const QString & token = it.value().first;
        const QString & shardId = it.value().second;

        // 1) Set up the job writing the auth token to the keychain
        auto jobIt = m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left.find(guid);
        if (jobIt == m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.left.end()) {
            QString key = keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid;
            QUuid jobId = m_pKeychainService->startWritePasswordJob(WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB, key, token);
            m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids.insert(JobIdWithGuidBimap::value_type(guid, jobId));
        }
        else {
            m_linkedNotebookAuthTokensPendingWritingByGuid[guid] = token;
        }

        // 2) Set up the job writing the shard id to the keychain
        jobIt = m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left.find(guid);
        if (jobIt == m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.left.end()) {
            QString key = keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid;
            QUuid jobId = m_pKeychainService->startWritePasswordJob(WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB, key, shardId);
            m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids.insert(JobIdWithGuidBimap::value_type(guid, jobId));
        }
        else {
            m_linkedNotebookShardIdsPendingWritingByGuid[guid] = shardId;
        }
    }
}

void SynchronizationManagerPrivate::onReadAuthTokenFinished(const IKeychainService::ErrorCode::type errorCode,
                                                            const ErrorString & errorDescription, const QString & password)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onReadAuthTokenFinished: error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    m_readingAuthToken = false;

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        QNWARNING(errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully restored the authentication token"));
    m_OAuthResult.m_authToken = password;

    if (!m_readingShardId) {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onReadShardIdFinished(const IKeychainService::ErrorCode::type errorCode, const ErrorString & errorDescription,
                                                          const QString & password)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onReadShardIdFinished: error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    m_readingShardId = false;

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        QNWARNING(errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully restored the shard id"));
    m_OAuthResult.m_shardId = password;

    if (!m_readingAuthToken) {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onWriteAuthTokenFinished(const IKeychainService::ErrorCode::type errorCode,
                                                             const ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onWriteAuthTokenFinished: error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    m_writingAuthToken = false;

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        ErrorString error(QT_TR_NOOP("Failed to write the OAuth token to the keychain"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully stored the authentication token in the keychain"));

    if (!m_writingShardId) {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onWriteShardIdFinished(const IKeychainService::ErrorCode::type errorCode,
                                                           const ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onWriteShardIdFinished: error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    m_writingShardId = false;

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        ErrorString error(QT_TR_NOOP("Failed to write the shard id to the keychain"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    QNDEBUG(QStringLiteral("Successfully stored the shard id in the keychain"));

    if (!m_writingAuthToken) {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onDeleteAuthTokenFinished(const IKeychainService::ErrorCode::type errorCode,
                                                              const ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onDeleteAuthTokenFinished: user id = ")
            << m_lastRevokedAuthenticationUserId << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    m_deletingAuthToken = false;

    if ( (errorCode != IKeychainService::ErrorCode::NoError) &&
         (errorCode != IKeychainService::ErrorCode::EntryNotFound) )
    {
        m_deletingShardId = false;
        m_deleteShardIdJobId = QUuid();

        QNWARNING(QStringLiteral("Attempt to delete the auth token returned with error: ")
                  << errorDescription);
        ErrorString error(QT_TR_NOOP("Failed to delete authentication token from the keychain"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT authenticationRevoked(/* success = */ false, error, m_lastRevokedAuthenticationUserId);
        return;
    }

    if (!m_deletingShardId) {
        Q_EMIT authenticationRevoked(/* success = */ true, ErrorString(), m_lastRevokedAuthenticationUserId);
    }
}

void SynchronizationManagerPrivate::onDeleteShardIdFinished(const IKeychainService::ErrorCode::type errorCode, const ErrorString & errorDescription)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::onDeleteShardIdFinished: user id = ")
            << m_lastRevokedAuthenticationUserId << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);

    m_deletingShardId = false;

    if ( (errorCode != IKeychainService::ErrorCode::NoError) &&
         (errorCode != IKeychainService::ErrorCode::EntryNotFound) )
    {
        m_deletingAuthToken = false;
        m_deleteAuthTokenJobId = QUuid();

        QNWARNING(QStringLiteral("Attempt to delete the shard id returned with error: ")
                  << errorDescription);
        ErrorString error(QT_TR_NOOP("Failed to delete shard id from the keychain"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT authenticationRevoked(/* success = */ false, error, m_lastRevokedAuthenticationUserId);
        return;
    }

    if (!m_deletingAuthToken) {
        Q_EMIT authenticationRevoked(/* success = */ true, ErrorString(), m_lastRevokedAuthenticationUserId);
    }
}

void SynchronizationManagerPrivate::tryUpdateLastSyncStatus()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::tryUpdateLastSyncStatus"));

    qint32 updateCount = -1;
    QHash<QString,qint32> updateCountsByLinkedNotebookGuid;
    m_remoteToLocalSyncManager.collectNonProcessedItemsSmallestUsns(updateCount, updateCountsByLinkedNotebookGuid);

    if ((updateCount < 0) && updateCountsByLinkedNotebookGuid.isEmpty()) {
        QNDEBUG(QStringLiteral("Found no USNs for neither user's own account nor linked notebooks"));
        return;
    }

    qevercloud::Timestamp lastSyncTime = QDateTime::currentMSecsSinceEpoch();

    bool shouldUpdatePersistentSyncSettings = false;

    if ((updateCount > 0) && m_remoteToLocalSyncManager.downloadedSyncChunks())
    {
        m_lastUpdateCount = updateCount;
        m_lastSyncTime = lastSyncTime;
        QNDEBUG(QStringLiteral("Got updated sync state for user's own account: update count = ") << m_lastUpdateCount
                << QStringLiteral(", last sync time = ") << printableDateTimeFromTimestamp(m_lastSyncTime));
        shouldUpdatePersistentSyncSettings = true;
    }
    else if (!updateCountsByLinkedNotebookGuid.isEmpty() &&
             m_remoteToLocalSyncManager.downloadedLinkedNotebooksSyncChunks())
    {
        for(auto it = updateCountsByLinkedNotebookGuid.constBegin(),
            end = updateCountsByLinkedNotebookGuid.constEnd(); it != end; ++it)
        {
            m_cachedLinkedNotebookLastUpdateCountByGuid[it.key()] = it.value();
            m_cachedLinkedNotebookLastSyncTimeByGuid[it.key()] = lastSyncTime;
            QNDEBUG(QStringLiteral("Got updated sync state for linked notebook with guid ")
                    << it.key() << QStringLiteral(", update count = ") << it.value()
                    << QStringLiteral(", last sync time = ") << lastSyncTime);
            shouldUpdatePersistentSyncSettings = true;
        }
    }

    if (shouldUpdatePersistentSyncSettings) {
        updatePersistentSyncSettings();
    }
}

void SynchronizationManagerPrivate::updatePersistentSyncSettings()
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::updatePersistentSyncSettings"));

    ApplicationSettings appSettings(m_remoteToLocalSyncManager.account(), SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = QStringLiteral("Synchronization/") + m_host + QStringLiteral("/") +
                             QString::number(m_OAuthResult.m_userId) + QStringLiteral("/") +
                             LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");
    appSettings.setValue(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY, m_lastUpdateCount);
    appSettings.setValue(keyGroup + LAST_SYNC_TIME_KEY, m_lastSyncTime);

    int numLinkedNotebooksSyncParams = m_cachedLinkedNotebookLastUpdateCountByGuid.size();
    appSettings.beginWriteArray(keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS, numLinkedNotebooksSyncParams);

    int counter = 0;
    auto updateCountEnd = m_cachedLinkedNotebookLastUpdateCountByGuid.end();
    auto syncTimeEnd = m_cachedLinkedNotebookLastSyncTimeByGuid.end();
    for(auto updateCountIt = m_cachedLinkedNotebookLastUpdateCountByGuid.begin(); updateCountIt != updateCountEnd; ++updateCountIt)
    {
        const QString & guid = updateCountIt.key();
        auto syncTimeIt = m_cachedLinkedNotebookLastSyncTimeByGuid.find(guid);
        if (syncTimeIt == syncTimeEnd) {
            QNWARNING(QStringLiteral("Detected inconsistent last sync parameters for one of linked notebooks: last update count is present "
                                     "while last sync time is not, skipping writing the persistent settings entry for this linked notebook"));
            continue;
        }

        appSettings.setArrayIndex(counter);
        appSettings.setValue(LINKED_NOTEBOOK_GUID_KEY, guid);
        appSettings.setValue(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY, updateCountIt.value());
        appSettings.setValue(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY, syncTimeIt.value());
        QNTRACE(QStringLiteral("Persisted last sync parameters for a linked notebook: guid = ") << guid
                << QStringLiteral(", update count = ") << updateCountIt.value()
                << QStringLiteral(", sync time = ") << printableDateTimeFromTimestamp(syncTimeIt.value()));

        ++counter;
    }

    appSettings.endArray();

    QNTRACE(QStringLiteral("Wrote ") << counter << QStringLiteral(" last sync params entries for linked notebooks"));
}

INoteStore * SynchronizationManagerPrivate::noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook)
{
    QNTRACE(QStringLiteral("SynchronizationManagerPrivate::noteStoreForLinkedNotebook: ")
            << linkedNotebook);

    if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
        QNTRACE(QStringLiteral("Linked notebook has no guid, can't find or create note store for it"));
        return Q_NULLPTR;
    }

    INoteStore * pNoteStore = noteStoreForLinkedNotebookGuid(linkedNotebook.guid());
    if (Q_UNLIKELY(!pNoteStore)) {
        return Q_NULLPTR;
    }

    if (linkedNotebook.hasNoteStoreUrl()) {
        QNTRACE(QStringLiteral("Setting note store URL to the created and/or found note store: ")
                << linkedNotebook.noteStoreUrl());
        pNoteStore->setNoteStoreUrl(linkedNotebook.noteStoreUrl());
    }

    return pNoteStore;
}

INoteStore * SynchronizationManagerPrivate::noteStoreForLinkedNotebookGuid(const QString & guid)
{
    QNDEBUG(QStringLiteral("SynchronizationManagerPrivate::noteStoreForLinkedNotebookGuid: guid = ") << guid);

    if (Q_UNLIKELY(guid.isEmpty())) {
        QNWARNING(QStringLiteral("Can't find or create the note store for empty linked notebook guid"));
        return Q_NULLPTR;
    }

    auto it = m_noteStoresByLinkedNotebookGuids.find(guid);
    if (it != m_noteStoresByLinkedNotebookGuids.end()) {
        QNDEBUG(QStringLiteral("Found existing note store for linked notebook guid ") << guid);
        return it.value();
    }

    QNDEBUG(QStringLiteral("Found no existing note store corresponding to linked notebook guid ") << guid);

    if (m_authenticationInProgress) {
        QNWARNING(QStringLiteral("Can't create the note store for a linked notebook: the authentication is in progress"));
        return Q_NULLPTR;
    }

    INoteStore * pNoteStore = m_pNoteStore->create();
    pNoteStore->setParent(this);

    pNoteStore->setAuthenticationToken(m_OAuthResult.m_authToken);
    m_noteStoresByLinkedNotebookGuids[guid] = pNoteStore;
    return pNoteStore;
}

SynchronizationManagerPrivate::RemoteToLocalSynchronizationManagerController::RemoteToLocalSynchronizationManagerController(LocalStorageManagerAsync & localStorageManagerAsync,
                                                                                                                            SynchronizationManagerPrivate & syncManager) :
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_syncManager(syncManager)
{}

LocalStorageManagerAsync & SynchronizationManagerPrivate::RemoteToLocalSynchronizationManagerController::localStorageManagerAsync()
{
    return m_localStorageManagerAsync;
}

INoteStore & SynchronizationManagerPrivate::RemoteToLocalSynchronizationManagerController::noteStore()
{
    return *m_syncManager.m_pNoteStore;
}

IUserStore & SynchronizationManagerPrivate::RemoteToLocalSynchronizationManagerController::userStore()
{
    return *m_syncManager.m_pUserStore;
}

INoteStore * SynchronizationManagerPrivate::RemoteToLocalSynchronizationManagerController::noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook)
{
    return m_syncManager.noteStoreForLinkedNotebook(linkedNotebook);
}

SynchronizationManagerPrivate::SendLocalChangesManagerController::SendLocalChangesManagerController(LocalStorageManagerAsync & localStorageManagerAsync,
                                                                                                    SynchronizationManagerPrivate & syncManager) :
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_syncManager(syncManager)
{}

LocalStorageManagerAsync & SynchronizationManagerPrivate::SendLocalChangesManagerController::localStorageManagerAsync()
{
    return m_localStorageManagerAsync;
}

INoteStore & SynchronizationManagerPrivate::SendLocalChangesManagerController::noteStore()
{
    return *m_syncManager.m_pNoteStore;
}

INoteStore * SynchronizationManagerPrivate::SendLocalChangesManagerController::noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook)
{
    return m_syncManager.noteStoreForLinkedNotebook(linkedNotebook);
}

QTextStream & SynchronizationManagerPrivate::AuthData::print(QTextStream & strm) const
{
    strm << QStringLiteral("AuthData: {\n")
         << QStringLiteral("    user id = ") << m_userId << QStringLiteral(";\n")
         << QStringLiteral("    auth token expiration time = ") << printableDateTimeFromTimestamp(m_expirationTime) << QStringLiteral(";\n")
         << QStringLiteral("    shard id = ") << m_shardId << QStringLiteral(";\n")
         << QStringLiteral("    note store url = ") << m_noteStoreUrl << QStringLiteral(";\n")
         << QStringLiteral("    web API url prefix = ") << m_webApiUrlPrefix << QStringLiteral(";\n")
         << QStringLiteral("};\n");
    return strm;
}

} // namespace quentier
