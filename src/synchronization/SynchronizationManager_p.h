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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_PRIVATE_H
#define LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_PRIVATE_H

#include "RemoteToLocalSynchronizationManager.h"
#include "SendLocalChangesManager.h"

#include <quentier/synchronization/Fwd.h>
#include <quentier/types/Account.h>
#include <quentier/utility/IKeychainService.h>
#include <quentier/utility/SuppressWarnings.h>

SAVE_WARNINGS

MSVC_SUPPRESS_WARNING(4834)

#include <boost/bimap.hpp>

RESTORE_WARNINGS

#include <QObject>
#include <QSet>

#include <memory>
#include <utility>

class QDebug;

namespace quentier {

class Q_DECL_HIDDEN SynchronizationManagerPrivate final : public QObject
{
    Q_OBJECT
public:
    SynchronizationManagerPrivate(
        QString host, LocalStorageManagerAsync & localStorageManagerAsync,
        IAuthenticationManager & authenticationManager, QObject * parent,
        INoteStorePtr pNoteStore, IUserStorePtr pUserStore,
        IKeychainServicePtr pKeychainService,
        ISyncStateStoragePtr pSyncStateStorage);

    ~SynchronizationManagerPrivate() override;

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool downloadNoteThumbnailsOption() const;

    // Only public because otherwise can't implement printing
    enum class AuthContext
    {
        Blank = 0,
        SyncLaunch,
        NewUserRequest,
        CurrentUserRequest,
        AuthToLinkedNotebooks,
    };

    friend QDebug & operator<<(QDebug & dbg, AuthContext ctx);
    friend QTextStream & operator<<(QTextStream & strm, AuthContext ctx);

Q_SIGNALS:
    void notifyStart();
    void notifyStop();
    void notifyError(ErrorString errorDescription);
    void notifyRemoteToLocalSyncDone(bool somethingDownloaded);

    void notifyFinish(
        Account account, bool somethingDownloaded, bool somethingSent);

    // progress signals
    void syncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn);

    void syncChunksDownloaded();

    void syncChunksDataProcessingProgress(ISyncChunksDataCountersPtr counters);

    void linkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn, qevercloud::LinkedNotebook linkedNotebook);

    void linkedNotebooksSyncChunksDownloaded();

    void linkedNotebookSyncChunksDataProcessingProgress(
        ISyncChunksDataCountersPtr counters);

    void notesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void linkedNotebooksNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void resourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void linkedNotebooksResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void preparedDirtyObjectsForSending();
    void preparedLinkedNotebooksDirtyObjectsForSending();

    // state signals
    void remoteToLocalSyncStopped();
    void sendLocalChangesStopped();

    void authenticationFinished(
        bool success, ErrorString errorDescription, Account account);

    void authenticationRevoked(
        bool success, ErrorString errorDescription, qevercloud::UserID userId);

    // other informative signals
    void willRepeatRemoteToLocalSyncAfterSendingChanges();
    void detectedConflictDuringLocalChangesSending();
    void rateLimitExceeded(qint32 secondsToWait);

public Q_SLOTS:
    void setAccount(const Account & account);
    void synchronize();
    void authenticate();
    void authenticateCurrentAccount();
    void stop();

    void revokeAuthentication(qevercloud::UserID userId);

    void setDownloadNoteThumbnails(bool flag);
    void setDownloadInkNoteImages(bool flag);
    void setInkNoteImagesStoragePath(const QString & path);

Q_SIGNALS:
    // private signals
    void requestAuthentication();

    void sendAuthenticationTokenAndShardId(
        QString authToken, QString shardId,
        qevercloud::Timestamp expirationTime);

    void sendAuthenticationTokensForLinkedNotebooks(
        QHash<QString, std::pair<QString, QString>>
            authTokensAndShardIdsByLinkedNotebookGuids,
        QHash<QString, qevercloud::Timestamp>
            authTokenExpirationByLinkedNotebookGuids);

    void sendLastSyncParameters(
        qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
        QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);

private Q_SLOTS:
    void onOAuthResult(
        bool success, qevercloud::UserID userId, QString authToken,
        qevercloud::Timestamp authTokenExpirationTime, QString shardId,
        QString noteStoreUrl, QString webApiUrlPrefix,
        QList<QNetworkCookie> userStoreCookies, ErrorString errorDescription);

    void onWritePasswordJobFinished(
        QUuid jobId, IKeychainService::ErrorCode errorCode,
        ErrorString errorDescription);

    void onDeletePasswordJobFinished(
        QUuid jobId, IKeychainService::ErrorCode errorCode,
        ErrorString errorDescription);

    void onRequestAuthenticationToken();
    void onRequestAuthenticationTokensForLinkedNotebooks(
        QVector<LinkedNotebookAuthData> linkedNotebookAuthData);

    void onRequestLastSyncParameters();

    void onRemoteToLocalSyncFinished(
        qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
        QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid,
        QHash<QString, qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);

    void onRemoteToLocalSyncStopped();
    void onRemoteToLocalSyncFailure(ErrorString errorDescription);

    void onRemoteToLocalSynchronizedContentFromUsersOwnAccount(
        qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime);

    void onShouldRepeatIncrementalSync();
    void onConflictDetectedDuringLocalChangesSending();

    void onLocalChangesSent(
        qint32 lastUpdateCount,
        QHash<QString, qint32> lastUpdateCountByLinkedNotebookGuid);

    void onSendLocalChangesStopped();
    void onSendLocalChangesFailure(ErrorString errorDescription);

    void onRateLimitExceeded(qint32 secondsToWait);

private:
    void createConnections(IAuthenticationManager & authenticationManager);

    void readLastSyncParameters();

    void launchOAuth();
    void authenticateImpl(AuthContext authContext);
    void finalizeAuthentication();

    class AuthData : public Printable
    {
    public:
        qevercloud::UserID m_userId = -1;
        QString m_authToken;
        qevercloud::Timestamp m_authenticationTime = 0;
        qevercloud::Timestamp m_expirationTime = 0;
        QString m_shardId;
        QString m_noteStoreUrl;
        QString m_webApiUrlPrefix;
        QList<QNetworkCookie> m_cookies;

        QTextStream & print(QTextStream & strm) const override;
    };

    void launchStoreOAuthResult(const AuthData & result);
    void finalizeStoreOAuthResult();

    void finalizeRevokeAuthentication(qevercloud::UserID userId);
    void removeNonSecretPersistentAuthInfo(qevercloud::UserID userId);

    void launchSync();
    void launchFullSync();
    void launchIncrementalSync();
    void sendChanges();

    void timerEvent(QTimerEvent * pTimerEvent) override;

    void clear();

    [[nodiscard]] bool validAuthentication() const;

    [[nodiscard]] bool checkIfTimestampIsAboutToExpireSoon(
        qevercloud::Timestamp timestamp) const;

    void authenticateToLinkedNotebooks();

    void onReadAuthTokenFinished(
        IKeychainService::ErrorCode errorCode,
        const ErrorString & errorDescription, const QString & authToken);

    void onReadShardIdFinished(
        IKeychainService::ErrorCode errorCode,
        const ErrorString & errorDescription, const QString & shardId);

    void onReadLinkedNotebookAuthTokenFinished(
        IKeychainService::ErrorCode errorCode,
        const ErrorString & errorDescription, const QString & authToken,
        const qevercloud::Guid & linkedNotebookGuid);

    void onReadLinkedNotebookShardIdFinished(
        IKeychainService::ErrorCode errorCode,
        const ErrorString & errorDescription, const QString & shardId,
        const qevercloud::Guid & linkedNotebookGuid);

    void onWriteAuthTokenFinished(
        IKeychainService::ErrorCode errorCode,
        const ErrorString & errorDescription);

    void onWriteShardIdFinished(
        IKeychainService::ErrorCode errorCode,
        const ErrorString & errorDescription);

    void onDeleteAuthTokenFinished(
        IKeychainService::ErrorCode errorCode,
        qevercloud::UserID userId, const ErrorString & errorDescription);

    void onDeleteShardIdFinished(
        IKeychainService::ErrorCode errorCode,
        qevercloud::UserID userId, const ErrorString & errorDescription);

    [[nodiscard]] bool isReadingAuthToken(qevercloud::UserID userId) const;

    [[nodiscard]] bool isReadingShardId(qevercloud::UserID userId) const;

    [[nodiscard]] bool isWritingAuthToken(qevercloud::UserID userId) const;

    [[nodiscard]] bool isWritingShardId(qevercloud::UserID userId) const;

    [[nodiscard]] bool isDeletingAuthToken(qevercloud::UserID userId) const;

    [[nodiscard]] bool isDeletingShardId(qevercloud::UserID userId) const;

    void tryUpdateLastSyncStatus();
    void updatePersistentSyncSettings();

    INoteStore * noteStoreForLinkedNotebook(
        const qevercloud::LinkedNotebook & linkedNotebook);

    INoteStore * noteStoreForLinkedNotebookGuid(const QString & guid);

private:
    class SendLocalChangesManagerController;
    friend class SendLocalChangesManagerController;

    class RemoteToLocalSynchronizationManagerController;
    friend class RemoteToLocalSynchronizationManagerController;

    using KeychainJobIdWithGuidBimap = boost::bimap<QString, QUuid>;
    using KeychainJobIdWithUserId = boost::bimap<qevercloud::UserID, QUuid>;

private:
    Q_DISABLE_COPY(SynchronizationManagerPrivate)

private:
    QString m_host;

    ISyncStateStoragePtr m_pSyncStateStorage;

    qint32 m_previousUpdateCount = -1;
    qint32 m_lastUpdateCount = -1;
    qevercloud::Timestamp m_lastSyncTime = -1;
    QHash<QString, qint32> m_cachedLinkedNotebookLastUpdateCountByGuid;

    QHash<QString, qevercloud::Timestamp>
        m_cachedLinkedNotebookLastSyncTimeByGuid;

    bool m_onceReadLastSyncParams = false;

    INoteStorePtr m_pNoteStore;
    IUserStorePtr m_pUserStore;

    AuthContext m_authContext = AuthContext::Blank;

    int m_launchSyncPostponeTimerId = -1;

    AuthData m_OAuthResult;
    bool m_authenticationInProgress = false;

    std::unique_ptr<RemoteToLocalSynchronizationManagerController>
        m_pRemoteToLocalSyncManagerController;

    RemoteToLocalSynchronizationManager * m_pRemoteToLocalSyncManager;

    // The flag coming from RemoteToLocalSynchronizationManager and telling
    // whether something was downloaded during the last remote to local sync
    bool m_somethingDownloaded;

    std::unique_ptr<SendLocalChangesManagerController>
        m_pSendLocalChangesManagerController;

    SendLocalChangesManager * m_pSendLocalChangesManager;

    QHash<QString, std::pair<QString, QString>>
        m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid;

    QHash<QString, qevercloud::Timestamp>
        m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid;

    QVector<LinkedNotebookAuthData>
        m_linkedNotebookAuthDataPendingAuthentication;

    QHash<QString, INoteStore *> m_noteStoresByLinkedNotebookGuids;

    int m_authenticateToLinkedNotebooksPostponeTimerId = -1;

    IKeychainServicePtr m_pKeychainService;

    QSet<qevercloud::UserID> m_userIdsPendingAuthTokenReading;
    QSet<qevercloud::UserID> m_userIdsPendingShardIdReading;

    KeychainJobIdWithUserId m_writeAuthTokenJobIdsWithUserIds;
    KeychainJobIdWithUserId m_writeShardIdJobIdsWithUserIds;

    QHash<qevercloud::UserID, AuthData> m_writtenOAuthResultByUserId;

    KeychainJobIdWithUserId m_deleteAuthTokenJobIdsWithUserIds;
    KeychainJobIdWithUserId m_deleteShardIdJobIdsWithUserIds;

    QSet<qevercloud::Guid> m_linkedNotebookGuidsPendingAuthTokenReading;
    QSet<qevercloud::Guid> m_linkedNotebookGuidsPendingShardIdReading;

    KeychainJobIdWithGuidBimap
        m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids;

    KeychainJobIdWithGuidBimap
        m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids;

    QHash<QString, QString> m_linkedNotebookAuthTokensPendingWritingByGuid;
    QHash<QString, QString> m_linkedNotebookShardIdsPendingWritingByGuid;

    QSet<QString> m_linkedNotebookGuidsWithoutLocalAuthData;

    bool m_shouldRepeatIncrementalSyncAfterSendingChanges = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_PRIVATE_H
