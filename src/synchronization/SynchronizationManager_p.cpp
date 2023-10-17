/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include "types/SyncState.h"

#include "../utility/keychain/QtKeychainService.h"

#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

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

namespace {

[[nodiscard]] std::pair<IKeychainService::ErrorCode, ErrorString> toErrorInfo(
    const QException & e)
{
    try {
        e.raise();
    }
    catch (const IKeychainService::Exception & exc) {
        return std::make_pair(exc.errorCode(), exc.errorMessage());
    }
    catch (const QException & exc) {
        return std::make_pair(
            IKeychainService::ErrorCode::OtherError, ErrorString{exc.what()});
    }

    return std::make_pair(
        IKeychainService::ErrorCode::OtherError, ErrorString{});
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

class SynchronizationManagerPrivate::
    RemoteToLocalSynchronizationManagerController final :
    public RemoteToLocalSynchronizationManager::IManager
{
public:
    RemoteToLocalSynchronizationManagerController(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManagerPrivate & syncManager);

    [[nodiscard]] LocalStorageManagerAsync & localStorageManagerAsync()
        override;

    [[nodiscard]] INoteStore & noteStore() override;
    [[nodiscard]] IUserStore & userStore() override;

    [[nodiscard]] INoteStore * noteStoreForLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook) override;

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

    [[nodiscard]] LocalStorageManagerAsync & localStorageManagerAsync()
        override;

    [[nodiscard]] INoteStore & noteStore() override;

    [[nodiscard]] INoteStore * noteStoreForLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook) override;

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
        m_pKeychainService = newQtKeychainService();
    }

    if (!m_pSyncStateStorage) {
        m_pSyncStateStorage = newSyncStateStorage(this);
    }
    else {
        m_pSyncStateStorage->setParent(this);
    }

    createConnections(authenticationManager);
}

SynchronizationManagerPrivate::~SynchronizationManagerPrivate() = default;

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

    const bool writingAuthToken = isWritingAuthToken(m_OAuthResult.m_userId);
    const bool writingShardId = isWritingShardId(m_OAuthResult.m_userId);

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

    const bool writingAuthToken = isWritingAuthToken(m_OAuthResult.m_userId);
    const bool writingShardId = isWritingShardId(m_OAuthResult.m_userId);

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

    const bool writingAuthToken = isWritingAuthToken(m_OAuthResult.m_userId);
    const bool writingShardId = isWritingShardId(m_OAuthResult.m_userId);

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

    deleteAuthToken(userId);
    deleteShardId(userId);
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
    bool success, qevercloud::UserID userId, QString authToken, // NOLINT
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
    authData.m_shardId = std::move(shardId);
    authData.m_noteStoreUrl = std::move(noteStoreUrl);
    authData.m_webApiUrlPrefix = std::move(webApiUrlPrefix);
    authData.m_cookies = std::move(cookies);

    authData.m_authenticationTime =
        static_cast<qevercloud::Timestamp>(QDateTime::currentMSecsSinceEpoch());

    m_OAuthResult = authData;
    QNDEBUG("synchronization", "OAuth result = " << m_OAuthResult);

    const Account previousAccount = m_pRemoteToLocalSyncManager->account();

    Account newAccount(
        QString(), Account::Type::Evernote, userId,
        Account::EvernoteAccountType::Free, m_host);

    m_pRemoteToLocalSyncManager->setAccount(newAccount);
    m_pUserStore->setAuthData(authToken, m_OAuthResult.m_cookies);

    ErrorString error;
    const bool res = m_pRemoteToLocalSyncManager->syncUser(
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

    const auto & user = m_pRemoteToLocalSyncManager->user();
    if (Q_UNLIKELY(!user.username())) {
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

void SynchronizationManagerPrivate::onRequestAuthenticationToken()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onRequestAuthenticationToken");

    if (validAuthentication()) {
        QNDEBUG(
            "synchronization",
            "Found valid auth token and shard id, returning them");

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

    m_linkedNotebookAuthDataPendingAuthentication =
        std::move(linkedNotebookAuthData);

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
    QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid, // NOLINT
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

    m_cachedLinkedNotebookLastSyncTimeByGuid =
        std::move(lastSyncTimeByLinkedNotebookGuid);

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
    ErrorString errorDescription) // NOLINT
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
    QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid) // NOLINT
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

    const bool somethingDownloaded = m_somethingDownloaded;
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
    ErrorString errorDescription) // NOLINT
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
        &RemoteToLocalSynchronizationManager::syncChunksDataProcessingProgress,
        this, &SynchronizationManagerPrivate::syncChunksDataProcessingProgress,
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
        &RemoteToLocalSynchronizationManager::
            linkedNotebookSyncChunksDataProcessingProgress,
        this,
        &SynchronizationManagerPrivate::
            linkedNotebookSyncChunksDataProcessingProgress,
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

    const auto syncState = m_pSyncStateStorage->getSyncState(
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

    const QString keyGroup = QStringLiteral("Authentication/") + m_host +
        QStringLiteral("/") + QString::number(m_OAuthResult.m_userId) +
        QStringLiteral("/");

    const QVariant authenticationTimestamp =
        appSettings.value(keyGroup + AUTHENTICATION_TIMESTAMP_KEY);

    QDateTime authenticationDateTime;
    if (!authenticationTimestamp.isNull()) {
        bool conversionResult = false;

        const qint64 authenticationTimestampInt =
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
            "Last authentication was performed before Evernote introduced a "
                << "bug which requires to set a particular cookie into API "
                << "calls which was received during OAuth. Forcing new OAuth");
        launchOAuth();
        return;
    }

    m_OAuthResult.m_authenticationTime = static_cast<qevercloud::Timestamp>(
        authenticationDateTime.toMSecsSinceEpoch());

    const QVariant tokenExpirationValue =
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

    const qevercloud::Timestamp tokenExpirationTimestamp =
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

    const QVariant noteStoreUrlValue =
        appSettings.value(keyGroup + NOTE_STORE_URL_KEY);

    if (noteStoreUrlValue.isNull()) {
        ErrorString error(
            QT_TR_NOOP("Failed to find the note store url within "
                       "persistent application settings"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    const QString noteStoreUrl = noteStoreUrlValue.toString();
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

    const QVariant webApiUrlPrefixValue =
        appSettings.value(keyGroup + WEB_API_URL_PREFIX_KEY);

    if (webApiUrlPrefixValue.isNull()) {
        ErrorString error(
            QT_TR_NOOP("Failed to find the web API url prefix "
                       "within persistent application settings"));
        QNWARNING("synchronization", error);
        Q_EMIT notifyError(error);
        return;
    }

    const QString webApiUrlPrefix = webApiUrlPrefixValue.toString();
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

    const QByteArray rawCookie =
        appSettings.value(keyGroup + USER_STORE_COOKIE_KEY).toByteArray();

    m_OAuthResult.m_cookies = QNetworkCookie::parseCookies(rawCookie);

    QNDEBUG(
        "synchronization",
        "Trying to restore the authentication token and "
            << "the shard id from the keychain");

    const QString readAuthTokenService =
        QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;

    const QString readAuthTokenKey = QCoreApplication::applicationName() +
        QStringLiteral("_auth_token_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    m_userIdsPendingAuthTokenReading.insert(m_OAuthResult.m_userId);

    QFuture<QString> readAuthTokenFuture = m_pKeychainService->readPassword(
        readAuthTokenService, readAuthTokenKey);

    auto readAuthTokenThenFuture = threading::then(
        std::move(readAuthTokenFuture),
        this,
        [this, userId = m_OAuthResult.m_userId](const QString & authToken) {
            onReadAuthTokenFinished(
                IKeychainService::ErrorCode::NoError, userId, ErrorString{},
                authToken);
        });

    threading::onFailed(
        std::move(readAuthTokenThenFuture),
        this,
        [this, userId = m_OAuthResult.m_userId](const QException & e) {
            const auto result = toErrorInfo(e);
            onReadAuthTokenFinished(
                result.first, userId, result.second, QString{});
        });

    const QString readShardIdService =
        QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;

    const QString readShardIdKey = QCoreApplication::applicationName() +
        QStringLiteral("_shard_id_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    m_userIdsPendingShardIdReading.insert(m_OAuthResult.m_userId);

    QFuture<QString> readShardIdFuture = m_pKeychainService->readPassword(
        readShardIdService, readShardIdKey);

    auto readShardIdThenFuture = threading::then(
        std::move(readShardIdFuture),
        this,
        [this, userId = m_OAuthResult.m_userId](const QString & shardId) {
            onReadShardIdFinished(
                IKeychainService::ErrorCode::NoError, userId, ErrorString{},
                shardId);
        });

    threading::onFailed(
        std::move(readShardIdThenFuture),
        [this, userId = m_OAuthResult.m_userId](const QException & e) {
            const auto result = toErrorInfo(e);
            onReadShardIdFinished(
                result.first, userId, result.second, QString{});
        });
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
    writeAuthToken(result.m_authToken);
    writeShardId(result.m_shardId);
}

void SynchronizationManagerPrivate::finalizeStoreOAuthResult()
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::finalizeStoreOAuthResult");

    const auto it = m_writtenOAuthResultByUserId.find(m_OAuthResult.m_userId);
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

    const QString keyGroup = QStringLiteral("Authentication/") + m_host +
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
        const QString cookieName = QString::fromUtf8(cookie.name());
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
            "Emitting the authenticationFinished signal: " << account);

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
            "Cleaning up the auth data for current user: " << userId);
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

    const auto storagePath = applicationPersistentStoragePath();

    const auto evernoteAccountsDirPath =
        storagePath + QStringLiteral("/EvernoteAccounts");

    QDir evernoteAccountsDir(evernoteAccountsDirPath);
    if (evernoteAccountsDir.exists() && evernoteAccountsDir.isReadable()) {
        auto subdirs = evernoteAccountsDir.entryList(
            QDir::Filters(QDir::AllDirs | QDir::NoDotAndDotDot));

        const QString userIdStr = QString::number(userId);

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

            const int numParts = nameParts.size();
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
            "Failed to detect existing Evernote account for user id "
                << userId << ", cannot remove its persistent auth info");
        return;
    }

    QNDEBUG(
        "synchronization",
        "Found Evernote account corresponding to user id "
            << userId << ": name = " << accountName << ", host = " << host);

    // Now can actually create this account and mess with its persistent
    // settings

    Account account(
        accountName, Account::Type::Evernote, userId,
        Account::EvernoteAccountType::Free, // it doesn't really matter now
        host);

    ApplicationSettings appSettings(account, SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString authKeyGroup = QStringLiteral("Authentication/") + host +
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

    const int timerId = pTimerEvent->timerId();
    killTimer(timerId);

    QNDEBUG("synchronization", "Timer event for timer id " << timerId);

    if (timerId == m_launchSyncPostponeTimerId) {
        QNDEBUG(
            "synchronization",
            "Re-launching the sync procedure due to RATE_LIMIT_REACHED "
                << "exception when trying to get the sync state the last time");
        launchSync();
        return;
    }

    if (timerId == m_authenticateToLinkedNotebooksPostponeTimerId) {
        m_authenticateToLinkedNotebooksPostponeTimerId = -1;
        QNDEBUG(
            "synchronization",
            "Re-attempting to authenticate to the remaining linked (shared) "
                << "notebooks");
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

    m_linkedNotebookGuidsPendingAuthTokenReading.clear();
    m_linkedNotebookGuidsPendingShardIdReading.clear();

    m_linkedNotebookAuthTokensPendingWritingByGuid.clear();
    m_linkedNotebookShardIdsPendingWritingByGuid.clear();

    m_linkedNotebookGuidsWithoutLocalAuthData.clear();

    m_userIdsPendingAuthTokenDeleting.clear();
    m_userIdsPendingShardIdDeleting.clear();

    m_userIdsPendingAuthTokenWriting.clear();
    m_userIdsPendingShardIdWriting.clear();

    m_linkedNotebookGuidsPendingAuthTokenWriting.clear();
    m_linkedNotebookGuidsPendingShardIdWriting.clear();

    m_userIdsPendingAuthTokenReading.clear();
    m_userIdsPendingShardIdReading.clear();

    m_linkedNotebookGuidsPendingAuthTokenReading.clear();
    m_linkedNotebookGuidsPendingShardIdReading.clear();

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

    return ((timestamp - currentTimestamp) < HALF_AN_HOUR_IN_MSEC);
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
            "No linked notebooks waiting for authentication, sending the "
                << "cached auth tokens, shard ids and expiration times");

        Q_EMIT sendAuthenticationTokensForLinkedNotebooks(
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
            m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
        return;
    }

    ApplicationSettings appSettings(
        m_pRemoteToLocalSyncManager->account(),
        SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup = QStringLiteral("Authentication/") + m_host +
        QStringLiteral("/") + QString::number(m_OAuthResult.m_userId) +
        QStringLiteral("/");

    QHash<QString, std::pair<QString, QString>>
        authTokensAndShardIdsToCacheByGuid;

    QHash<QString, qevercloud::Timestamp>
        authTokenExpirationTimestampsToCacheByGuid;

    const QString keyPrefix = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    QSet<QString> linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain;

    for (auto it = // NOLINT
             m_linkedNotebookAuthDataPendingAuthentication.begin(); // NOLINT
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

        const auto linkedNotebookAuthTokenIt =
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.find(guid);

        if (linkedNotebookAuthTokenIt ==
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid.end())
        {
            const auto noAuthDataIt =
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
                    "Haven't found the authentication token and shard id for "
                        << "linked notebook guid " << guid
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
                const QVariant expirationTimeVariant = appSettings.value(
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
                            "Can't convert linked notebook's authentication "
                                << "token's expiration time from QVariant "
                                << "retrieved from app settings "
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
                    "Found authentication data for linked notebook guid "
                        << guid << " + verified its expiration timestamp");
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
                "Authenticate to linked notebook postpone timer is active, "
                    << "will wait to preserve the breach of Evernote rate API "
                    << "limit");
            ++it;
            continue;
        }

        if (m_authContext != AuthContext::Blank) {
            QNDEBUG(
                "synchronization",
                "Authentication context variable is not set to blank which "
                    << "means that authentication must be in progress: "
                    << m_authContext << "; won't attempt to call "
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

        const qint32 errorCode = pNoteStore->authenticateToSharedNotebook(
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

        if (errorCode ==
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

        if (errorCode != 0) {
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
                << printableDateTimeFromTimestamp(authResult.currentTime())
                << ", expiration time for the authentication result "
                << "(expiration): "
                << printableDateTimeFromTimestamp(authResult.expiration())
                << ", user: "
                << (authResult.user() ? ToString(*authResult.user())
                                      : QStringLiteral("<empty>")));

        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[guid] =
            std::make_pair(authResult.authenticationToken(), shardId);

        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid[guid] =
            authResult.expiration();

        auto & authTokenAndShardId = authTokensAndShardIdsToCacheByGuid[guid];
        authTokenAndShardId.first = authResult.authenticationToken();
        authTokenAndShardId.second = shardId;

        authTokenExpirationTimestampsToCacheByGuid[guid] =
            authResult.expiration();

        it = m_linkedNotebookAuthDataPendingAuthentication.erase(it);
    }

    if (m_linkedNotebookAuthDataPendingAuthentication.isEmpty()) {
        QNDEBUG(
            "synchronization",
            "Retrieved authentication data for all requested linked notebooks, "
                << "sending the answer now");

        Q_EMIT sendAuthenticationTokensForLinkedNotebooks(
            m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid,
            m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid);
    }

    if (!linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain.isEmpty())
    {
        for (const auto & guid:
             linkedNotebookGuidsPendingReadAuthTokenAndShardIdInKeychain)
        {
            // 1) Read authentication token
            const auto readAuthTokenIt =
                m_linkedNotebookGuidsPendingAuthTokenReading.find(guid);

            if (readAuthTokenIt ==
                m_linkedNotebookGuidsPendingAuthTokenReading.end()) {
                m_linkedNotebookGuidsPendingAuthTokenReading.insert(guid);

                auto readPasswordFuture = m_pKeychainService->readPassword(
                    READ_LINKED_NOTEBOOK_AUTH_TOKEN_JOB,
                    keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + guid);

                auto readPasswordThenFuture = threading::then(
                    std::move(readPasswordFuture), this,
                    [this, guid](const QString & authToken) {
                        onReadLinkedNotebookAuthTokenFinished(
                            IKeychainService::ErrorCode::NoError, ErrorString{},
                            authToken, guid);
                    });

                threading::onFailed(
                    std::move(readPasswordThenFuture), this,
                    [this, guid](const QException & e) {
                        const auto result = toErrorInfo(e);
                        onReadLinkedNotebookAuthTokenFinished(
                            result.first, result.second, {}, guid);
                    });
            }

            // 2) Read shard id
            const auto readShardIdIt =
                m_linkedNotebookGuidsPendingShardIdReading.find(guid);

            if (readShardIdIt ==
                m_linkedNotebookGuidsPendingShardIdReading.end()) {
                m_linkedNotebookGuidsPendingShardIdReading.insert(guid);

                auto readPasswordFuture = m_pKeychainService->readPassword(
                    READ_LINKED_NOTEBOOK_SHARD_ID_JOB,
                    keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + guid);

                auto readPasswordThenFuture = threading::then(
                    std::move(readPasswordFuture), this,
                    [this, guid](const QString & shardId) {
                        onReadLinkedNotebookShardIdFinished(
                            IKeychainService::ErrorCode::NoError, ErrorString{},
                            shardId, guid);
                    });

                threading::onFailed(
                    std::move(readPasswordThenFuture), this,
                    [this, guid](const QException & e) {
                        const auto result = toErrorInfo(e);
                        onReadLinkedNotebookShardIdFinished(
                            result.first, result.second, {}, guid);
                    });
            }
        }

        QNDEBUG(
            "synchronization",
            "Pending read auth tokens and shard ids from keychain for "
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
        const QString key =
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

        if (!m_linkedNotebookGuidsPendingAuthTokenWriting.contains(guid)) {
            writeLinkedNotebookAuthToken(token, guid);
        }
        else {
            m_linkedNotebookAuthTokensPendingWritingByGuid[guid] = token;
        }

        if (!m_linkedNotebookGuidsPendingShardIdWriting.contains(guid)) {
            writeLinkedNotebookShardId(shardId, guid);
        }
        else {
            m_linkedNotebookShardIdsPendingWritingByGuid[guid] = shardId;
        }
    }
}

void SynchronizationManagerPrivate::writeAuthToken(const QString & authToken)
{
    m_userIdsPendingAuthTokenWriting.insert(m_OAuthResult.m_userId);

    QString writeAuthTokenService =
        QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;

    QString writeAuthTokenKey = QCoreApplication::applicationName() +
        QStringLiteral("_auth_token_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    auto writePasswordFuture = m_pKeychainService->writePassword(
        std::move(writeAuthTokenService), std::move(writeAuthTokenKey),
        authToken);

    auto writePasswordThenFuture = threading::then(
        std::move(writePasswordFuture), this,
        [this, userId = m_OAuthResult.m_userId] {
            onWriteAuthTokenFinished(
                IKeychainService::ErrorCode::NoError, userId, ErrorString{});
        });

    threading::onFailed(
        std::move(writePasswordThenFuture), this,
        [this, userId = m_OAuthResult.m_userId](const QException & e) {
            const auto result = toErrorInfo(e);
            onWriteAuthTokenFinished(result.first, userId, result.second);
        });
}

void SynchronizationManagerPrivate::writeShardId(const QString & shardId)
{
    m_userIdsPendingShardIdWriting.insert(m_OAuthResult.m_userId);

    QString writeShardIdService =
        QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;

    QString writeShardIdKey = QCoreApplication::applicationName() +
        QStringLiteral("_shard_id_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    auto writePasswordFuture = m_pKeychainService->writePassword(
        std::move(writeShardIdService), std::move(writeShardIdKey), shardId);

    auto writePasswordThenFuture = threading::then(
        std::move(writePasswordFuture), this,
        [this, userId = m_OAuthResult.m_userId] {
            onWriteShardIdFinished(
                IKeychainService::ErrorCode::NoError, userId, ErrorString{});
        });

    threading::onFailed(
        std::move(writePasswordThenFuture), this,
        [this, userId = m_OAuthResult.m_userId](const QException & e) {
            const auto result = toErrorInfo(e);
            onWriteShardIdFinished(result.first, userId, result.second);
        });
}

void SynchronizationManagerPrivate::writeLinkedNotebookAuthToken(
    const QString & authToken, const qevercloud::Guid & linkedNotebookGuid) // NOLINT
{
    m_linkedNotebookGuidsPendingAuthTokenWriting.insert(linkedNotebookGuid);

    const QString keyPrefix = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    auto writePasswordFuture = m_pKeychainService->writePassword(
        WRITE_LINKED_NOTEBOOK_AUTH_TOKEN_JOB,
        keyPrefix + LINKED_NOTEBOOK_AUTH_TOKEN_KEY_PART + linkedNotebookGuid,
        authToken);

    auto writePasswordThenFuture = threading::then(
        std::move(writePasswordFuture), this,
        [this, linkedNotebookGuid] {
            onWriteLinkedNotebookAuthTokenFinished(
                IKeychainService::ErrorCode::NoError, ErrorString{},
                linkedNotebookGuid);
        });

    threading::onFailed(
        std::move(writePasswordThenFuture), this,
        [this, linkedNotebookGuid](const QException & e) {
            const auto result = toErrorInfo(e);
            onWriteLinkedNotebookAuthTokenFinished(
                result.first, result.second, linkedNotebookGuid);
        });
}

void SynchronizationManagerPrivate::writeLinkedNotebookShardId(
    const QString & shardId, const qevercloud::Guid & linkedNotebookGuid) // NOLINT
{
    m_linkedNotebookGuidsPendingShardIdWriting.insert(linkedNotebookGuid);

    const QString keyPrefix = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(m_OAuthResult.m_userId);

    auto writePasswordFuture = m_pKeychainService->writePassword(
        WRITE_LINKED_NOTEBOOK_SHARD_ID_JOB,
        keyPrefix + LINKED_NOTEBOOK_SHARD_ID_KEY_PART + linkedNotebookGuid,
        shardId);

    auto writePasswordThenFuture = threading::then(
        std::move(writePasswordFuture), this,
        [this, linkedNotebookGuid] {
            onWriteLinkedNotebookShardIdFinished(
                IKeychainService::ErrorCode::NoError, ErrorString{},
                linkedNotebookGuid);
        });

    threading::onFailed(
        std::move(writePasswordThenFuture), this,
        [this, linkedNotebookGuid](const QException & e) {
            const auto result = toErrorInfo(e);
            onWriteLinkedNotebookShardIdFinished(
                result.first, result.second, linkedNotebookGuid);
        });
}

void SynchronizationManagerPrivate::deleteAuthToken(
    const qevercloud::UserID userId)
{
    m_userIdsPendingAuthTokenDeleting.insert(userId);

    QString deleteAuthTokenService =
        QCoreApplication::applicationName() + AUTH_TOKEN_KEYCHAIN_KEY_PART;

    QString deleteAuthTokenKey = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(userId);

    auto deletePasswordFuture = m_pKeychainService->deletePassword(
        std::move(deleteAuthTokenService), std::move(deleteAuthTokenKey));

    auto deletePasswordThenFuture = threading::then(
        std::move(deletePasswordFuture), this,
        [this, userId] {
            onDeleteAuthTokenFinished(
                IKeychainService::ErrorCode::NoError, userId, ErrorString{});
        });

    threading::onFailed(
        std::move(deletePasswordThenFuture), this,
        [this, userId](const QException & e) {
            const auto result = toErrorInfo(e);
            onDeleteAuthTokenFinished(result.first, userId, result.second);
        });
}

void SynchronizationManagerPrivate::deleteShardId(
    const qevercloud::UserID userId)
{
    m_userIdsPendingShardIdDeleting.insert(userId);

    QString deleteShardIdService =
        QCoreApplication::applicationName() + SHARD_ID_KEYCHAIN_KEY_PART;

    QString deleteShardIdKey = QCoreApplication::applicationName() +
        QStringLiteral("_") + m_host + QStringLiteral("_") +
        QString::number(userId);

    auto deletePasswordFuture = m_pKeychainService->deletePassword(
        std::move(deleteShardIdService), std::move(deleteShardIdKey));

    auto deletePasswordThenFuture = threading::then(
        std::move(deletePasswordFuture), this,
        [this, userId] {
            onDeleteShardIdFinished(
                IKeychainService::ErrorCode::NoError, userId, ErrorString{});
        });

    threading::onFailed(
        std::move(deletePasswordThenFuture), this,
        [this, userId](const QException & e) {
            const auto result = toErrorInfo(e);
            onDeleteShardIdFinished(result.first, userId, result.second);
        });
}

void SynchronizationManagerPrivate::onReadAuthTokenFinished(
    const IKeychainService::ErrorCode errorCode,
    const qevercloud::UserID userId, const ErrorString & errorDescription,
    const QString & authToken)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onReadAuthTokenFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    const auto it = m_userIdsPendingAuthTokenReading.find(userId);
    if (it == m_userIdsPendingAuthTokenReading.end()) {
        return;
    }

    m_userIdsPendingAuthTokenReading.erase(it);

    if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
        QNWARNING(
            "synchronization",
            "Unexpectedly missing OAuth token in the keychain: "
                << errorDescription << "; fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        QNWARNING(
            "synchronization",
            "Attempt to read the auth token returned with error: error code "
                << errorCode << ", " << errorDescription
                << ". Fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    QNDEBUG(
        "synchronization", "Successfully restored the authentication token");
    m_OAuthResult.m_authToken = authToken;

    if (!m_authenticationInProgress &&
        !isReadingShardId(m_OAuthResult.m_userId) &&
        !isWritingShardId(m_OAuthResult.m_userId))
    {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onReadShardIdFinished(
    const IKeychainService::ErrorCode errorCode,
    const qevercloud::UserID userId, const ErrorString & errorDescription,
    const QString & shardId)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onReadShardIdFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    const auto it = m_userIdsPendingShardIdReading.find(userId);
    if (it == m_userIdsPendingShardIdReading.end()) {
        return;
    }

    m_userIdsPendingShardIdReading.erase(it);

    if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
        QNWARNING(
            "synchronization",
            "Unexpectedly missing OAuth shard id in the keychain: "
                << errorDescription << "; fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    if (errorCode != IKeychainService::ErrorCode::NoError) {
        QNWARNING(
            "synchronization",
            "Attempt to read the shard id returned with error: error code "
                << errorCode << ", " << errorDescription
                << ". Fallback to explicit OAuth");
        launchOAuth();
        return;
    }

    QNDEBUG("synchronization", "Successfully restored the shard id");
    m_OAuthResult.m_shardId = shardId;

    if (!m_authenticationInProgress &&
        !isReadingAuthToken(m_OAuthResult.m_userId) &&
        !isWritingAuthToken(m_OAuthResult.m_userId))
    {
        finalizeAuthentication();
    }
}

void SynchronizationManagerPrivate::onReadLinkedNotebookAuthTokenFinished(
    IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription, const QString & authToken, // NOLINT
    const qevercloud::Guid & linkedNotebookGuid)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onReadLinkedNotebookAuthTokenFinished: "
            << "error code = " << errorCode << ", error description = "
            << errorDescription << ", linked notebook guid = "
            << linkedNotebookGuid);

    const auto it =
        m_linkedNotebookGuidsPendingAuthTokenReading.find(linkedNotebookGuid);
    if (it == m_linkedNotebookGuidsPendingAuthTokenReading.end()) {
        return;
    }

    m_linkedNotebookGuidsPendingAuthTokenReading.erase(it);

    if (errorCode == IKeychainService::ErrorCode::NoError) {
        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[linkedNotebookGuid]
            .first = authToken;
    }
    else if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
        Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
            linkedNotebookGuid))
    }
    else {
        QNWARNING(
            "synchronization",
            "Failed to read linked notebook's authentication token from "
                << "the keychain: error code = " << errorCode
                << ", error description: " << errorDescription
                << ", linked notebook guid: " << linkedNotebookGuid);

        /**
          * Try to recover by making user to authenticate again in the blind
          * hope that the next time the persistence of auth settings in the
          * keychain would work
          */
        Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
            linkedNotebookGuid))
    }

    if (m_linkedNotebookGuidsPendingAuthTokenReading.empty() &&
        m_linkedNotebookGuidsPendingShardIdReading.empty())
    {
        QNDEBUG(
            "synchronization",
            "No pending read linked notebook auth token or shard id job");
        authenticateToLinkedNotebooks();
    }
}

void SynchronizationManagerPrivate::onReadLinkedNotebookShardIdFinished(
    IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription, const QString & shardId, // NOLINT
    const qevercloud::Guid & linkedNotebookGuid)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onReadLinkedNotebookShardIdFinished: "
            << "error code = " << errorCode << ", error description = "
            << errorDescription << ", linked notebook guid = "
            << linkedNotebookGuid);

    const auto it =
        m_linkedNotebookGuidsPendingShardIdReading.find(linkedNotebookGuid);
    if (it == m_linkedNotebookGuidsPendingShardIdReading.end()) {
        return;
    }

    m_linkedNotebookGuidsPendingShardIdReading.erase(it);

    if (errorCode == IKeychainService::ErrorCode::NoError) {
        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid[linkedNotebookGuid]
            .second = shardId;
    }
    else if (errorCode == IKeychainService::ErrorCode::EntryNotFound) {
        Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
            linkedNotebookGuid))
    }
    else {
        QNWARNING(
            "synchronization",
            "Failed to read linked notebook's authentication token from "
                << "the keychain: error code = " << errorCode
                << ", error description: " << errorDescription);

        /**
            * Try to recover by making user to authenticate again in the blind
            * hope that the next time the persistence of auth settings in the
            * keychain would work
            */
        Q_UNUSED(m_linkedNotebookGuidsWithoutLocalAuthData.insert(
            linkedNotebookGuid))
    }

    if (m_linkedNotebookGuidsPendingAuthTokenReading.empty() &&
        m_linkedNotebookGuidsPendingShardIdReading.empty())
    {
        QNDEBUG(
            "synchronization",
            "No pending read linked notebook auth token or shard id job");
        authenticateToLinkedNotebooks();
    }
}

void SynchronizationManagerPrivate::onWriteAuthTokenFinished(
    const IKeychainService::ErrorCode errorCode,
    const qevercloud::UserID userId, const ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onWriteAuthTokenFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    const auto it = m_userIdsPendingAuthTokenWriting.find(userId);
    if (it == m_userIdsPendingAuthTokenWriting.end()) {
        return;
    }

    m_userIdsPendingAuthTokenWriting.erase(it);

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
        "Successfully stored the authentication token in the keychain");

    if (!m_authenticationInProgress &&
        !isReadingShardId(m_OAuthResult.m_userId) &&
        !isWritingShardId(m_OAuthResult.m_userId))
    {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onWriteShardIdFinished(
    const IKeychainService::ErrorCode errorCode,
    const qevercloud::UserID userId, const ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization",
        "SynchronizationManagerPrivate::onWriteShardIdFinished: error code = "
            << errorCode << ", error description = " << errorDescription);

    const auto it = m_userIdsPendingShardIdWriting.find(userId);
    if (it == m_userIdsPendingShardIdWriting.end()) {
        return;
    }

    m_userIdsPendingShardIdWriting.erase(it);

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
        "synchronization", "Successfully stored the shard id in the keychain");

    if (!m_authenticationInProgress &&
        !isReadingAuthToken(m_OAuthResult.m_userId) &&
        !isWritingAuthToken(m_OAuthResult.m_userId))
    {
        finalizeStoreOAuthResult();
    }
}

void SynchronizationManagerPrivate::onWriteLinkedNotebookAuthTokenFinished(
    IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription,
    const qevercloud::Guid & linkedNotebookGuid)
{
    const auto it =
        m_linkedNotebookGuidsPendingAuthTokenWriting.find(linkedNotebookGuid);
    if (it == m_linkedNotebookGuidsPendingAuthTokenWriting.end()) {
        return;
    }

    m_linkedNotebookGuidsPendingAuthTokenWriting.erase(it);

    const auto pendingItemIt =
        m_linkedNotebookAuthTokensPendingWritingByGuid.find(linkedNotebookGuid);

    if (pendingItemIt !=
        m_linkedNotebookAuthTokensPendingWritingByGuid.end()) {
        // NOTE: ignore the status of previous write job for this key,
        // it doesn't matter if we need to write another token
        const QString token = pendingItemIt.value();

        Q_UNUSED(m_linkedNotebookAuthTokensPendingWritingByGuid.erase(
                pendingItemIt))

        QNDEBUG(
            "synchronization",
            "Writing postponed auth token for linked notebook guid "
                << linkedNotebookGuid);

        writeLinkedNotebookAuthToken(token, linkedNotebookGuid);
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
}

void SynchronizationManagerPrivate::onWriteLinkedNotebookShardIdFinished(
    IKeychainService::ErrorCode errorCode,
    const ErrorString & errorDescription,
    const qevercloud::Guid & linkedNotebookGuid)
{
    const auto it =
        m_linkedNotebookGuidsPendingShardIdWriting.find(linkedNotebookGuid);
    if (it == m_linkedNotebookGuidsPendingShardIdWriting.end()) {
        return;
    }

    m_linkedNotebookGuidsPendingShardIdWriting.erase(it);

    const auto pendingItemIt =
        m_linkedNotebookShardIdsPendingWritingByGuid.find(linkedNotebookGuid);

    if (pendingItemIt != m_linkedNotebookShardIdsPendingWritingByGuid.end())
    {
        // NOTE: ignore the status of previous write job for this key,
        // it doesn't matter if we need to write another shard id
        const QString shardId = pendingItemIt.value();

        Q_UNUSED(m_linkedNotebookShardIdsPendingWritingByGuid.erase(
            pendingItemIt))

        QNDEBUG(
            "synchronization",
            "Writing postponed shard id " << shardId
                << " for linked notebook guid " << linkedNotebookGuid);

        writeLinkedNotebookShardId(shardId, linkedNotebookGuid);
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

    const auto it = m_userIdsPendingAuthTokenDeleting.find(userId);
    if (it == m_userIdsPendingAuthTokenDeleting.end()) {
        return;
    }

    m_userIdsPendingAuthTokenDeleting.erase(it);

    if ((errorCode != IKeychainService::ErrorCode::NoError) &&
        (errorCode != IKeychainService::ErrorCode::EntryNotFound))
    {
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

    const auto it = m_userIdsPendingShardIdDeleting.find(userId);
    if (it == m_userIdsPendingShardIdDeleting.end()) {
        return;
    }

    m_userIdsPendingShardIdDeleting.erase(it);

    if ((errorCode != IKeychainService::ErrorCode::NoError) &&
        (errorCode != IKeychainService::ErrorCode::EntryNotFound))
    {
        QNWARNING(
            "synchronization",
            "Attempt to delete the shard id returned with error: "
                << errorDescription);

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

    return m_userIdsPendingAuthTokenReading.contains(userId);
}

bool SynchronizationManagerPrivate::isReadingShardId(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return m_userIdsPendingShardIdReading.contains(userId);
}

bool SynchronizationManagerPrivate::isWritingAuthToken(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return m_userIdsPendingAuthTokenWriting.contains(userId);
}

bool SynchronizationManagerPrivate::isWritingShardId(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return m_userIdsPendingShardIdWriting.contains(userId);
}

bool SynchronizationManagerPrivate::isDeletingAuthToken(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return m_userIdsPendingAuthTokenDeleting.contains(userId);
}

bool SynchronizationManagerPrivate::isDeletingShardId(
    const qevercloud::UserID userId) const
{
    if (Q_UNLIKELY(userId < 0)) {
        return false;
    }

    return m_userIdsPendingShardIdDeleting.contains(userId);
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
            "Found no USNs for neither user's own account nor linked "
                << "notebooks");
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
            "Got updated sync state for user's own account: update count = "
                << m_lastUpdateCount << ", last sync time = "
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
                "Got updated sync state for linked notebook with guid "
                    << it.key() << ", update count = " << it.value()
                    << ", last sync time = " << lastSyncTime);
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

    const auto syncState = std::make_shared<synchronization::SyncState>();

    syncState->m_userDataUpdateCount = m_lastUpdateCount;
    syncState->m_userDataLastSyncTime = m_lastSyncTime;

    syncState->m_linkedNotebookUpdateCounts =
        m_cachedLinkedNotebookLastUpdateCountByGuid;

    syncState->m_linkedNotebookLastSyncTimes =
        m_cachedLinkedNotebookLastSyncTimeByGuid;

    m_pSyncStateStorage->setSyncState(
        m_pRemoteToLocalSyncManager->account(), syncState);
}

INoteStore * SynchronizationManagerPrivate::noteStoreForLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    QNTRACE(
        "synchronization",
        "SynchronizationManagerPrivate::noteStoreForLinkedNotebook: "
            << linkedNotebook);

    if (Q_UNLIKELY(!linkedNotebook.guid())) {
        QNTRACE(
            "synchronization",
            "Linked notebook has no guid, can't find or create note store for "
                << "it");
        return nullptr;
    }

    auto * pNoteStore = noteStoreForLinkedNotebookGuid(*linkedNotebook.guid());
    if (Q_UNLIKELY(!pNoteStore)) {
        return nullptr;
    }

    if (linkedNotebook.noteStoreUrl()) {
        QNTRACE(
            "synchronization",
            "Setting note store URL to the created and/or found note store: "
                << *linkedNotebook.noteStoreUrl());
        pNoteStore->setNoteStoreUrl(*linkedNotebook.noteStoreUrl());
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
            "Can't find or create the note store for empty linked notebook "
                << "guid");
        return nullptr;
    }

    const auto it = m_noteStoresByLinkedNotebookGuids.find(guid);
    if (it != m_noteStoresByLinkedNotebookGuids.end()) {
        QNDEBUG(
            "synchronization",
            "Found existing note store for linked notebook guid " << guid);
        return it.value();
    }

    QNDEBUG(
        "synchronization",
        "Found no existing note store corresponding to linked notebook guid "
            << guid);

    if (m_authenticationInProgress) {
        QNWARNING(
            "synchronization",
            "Can't create the note store for a linked notebook: the "
                << "authentication is in progress");
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
        const qevercloud::LinkedNotebook & linkedNotebook)
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

INoteStore & SynchronizationManagerPrivate::SendLocalChangesManagerController::
    noteStore()
{
    return *m_syncManager.m_pNoteStore;
}

INoteStore * SynchronizationManagerPrivate::SendLocalChangesManagerController::
    noteStoreForLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook)
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
