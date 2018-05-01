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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_PRIVATE_H
#define LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_PRIVATE_H

#include "RemoteToLocalSynchronizationManager.h"
#include "SendLocalChangesManager.h"
#include <quentier/synchronization/IAuthenticationManager.h>
#include <quentier/types/Account.h>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5keychain/keychain.h>
#else
#include <qtkeychain/keychain.h>
#endif

#include <QObject>
#include <QScopedPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(INoteStore)
QT_FORWARD_DECLARE_CLASS(IUserStore)

class Q_DECL_HIDDEN SynchronizationManagerPrivate: public QObject
{
    Q_OBJECT
public:
    SynchronizationManagerPrivate(const QString & host, LocalStorageManagerAsync & localStorageManagerAsync,
                                  IAuthenticationManager & authenticationManager,
                                  INoteStore * pNoteStore, IUserStore * pUserStore);
    virtual ~SynchronizationManagerPrivate();

    bool active() const;
    bool downloadNoteThumbnailsOption() const;

Q_SIGNALS:
    void notifyStart();
    void notifyStop();
    void notifyError(ErrorString errorDescription);
    void notifyRemoteToLocalSyncDone(bool somethingDownloaded);
    void notifyFinish(Account account, bool somethingDownloaded, bool somethingSent);

// progress signals
    void syncChunksDownloadProgress(qint32 highestDownloadedUsn, qint32 highestServerUsn, qint32 lastPreviousUsn);
    void syncChunksDownloaded();
    void linkedNotebookSyncChunksDownloadProgress(qint32 highestDownloadedUsn, qint32 highestServerUsn,
                                                  qint32 lastPreviousUsn, LinkedNotebook linkedNotebook);
    void linkedNotebooksSyncChunksDownloaded();

    void notesDownloadProgress(quint32 notesDownloaded, quint32 totalNotesToDownload);
    void linkedNotebooksNotesDownloadProgress(quint32 notesDownloaded, quint32 totalNotesToDownload);

    void resourcesDownloadProgress(quint32 resourcesDownloaded, quint32 totalResourcesToDownload);
    void linkedNotebooksResourcesDownloadProgress(quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void preparedDirtyObjectsForSending();
    void preparedLinkedNotebooksDirtyObjectsForSending();

// state signals
    void remoteToLocalSyncStopped();
    void sendLocalChangesStopped();

    void authenticationFinished(bool success, ErrorString errorDescription,
                                Account account);
    void authenticationRevoked(bool success, ErrorString errorDescription,
                               qevercloud::UserID userId);

// other informative signals
    void willRepeatRemoteToLocalSyncAfterSendingChanges();
    void detectedConflictDuringLocalChangesSending();
    void rateLimitExceeded(qint32 secondsToWait);

public Q_SLOTS:
    void setAccount(const Account & account);
    void synchronize();
    void authenticate();
    void stop();

    void revokeAuthentication(const qevercloud::UserID userId);

    void setDownloadNoteThumbnails(const bool flag);
    void setDownloadInkNoteImages(const bool flag);
    void setInkNoteImagesStoragePath(const QString & path);

Q_SIGNALS:
// private signals
    void requestAuthentication();
    void sendAuthenticationTokenAndShardId(QString authToken, QString shardId, qevercloud::Timestamp expirationTime);
    void sendAuthenticationTokensForLinkedNotebooks(QHash<QString,QPair<QString,QString> > authenticationTokensAndShardIdsByLinkedNotebookGuids,
                                                    QHash<QString,qevercloud::Timestamp> authenticatonTokenExpirationTimesByLinkedNotebookGuids);
    void sendLastSyncParameters(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
                                QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid,
                                QHash<QString,qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);

    void stopRemoteToLocalSync();
    void stopSendingLocalChanges();

private Q_SLOTS:
    void onOAuthResult(bool success, qevercloud::UserID userId, QString authToken,
                       qevercloud::Timestamp authTokenExpirationTime, QString shardId,
                       QString noteStoreUrl, QString webApiUrlPrefix, ErrorString errorDescription);

    void onKeychainJobFinished(QKeychain::Job * job);

    void onRequestAuthenticationToken();
    void onRequestAuthenticationTokensForLinkedNotebooks(QVector<LinkedNotebookAuthData> linkedNotebookAuthData);

    void onRequestLastSyncParameters();

    void onRemoteToLocalSyncFinished(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime,
                                     QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid,
                                     QHash<QString,qevercloud::Timestamp> lastSyncTimeByLinkedNotebookGuid);
    void onRemoteToLocalSyncStopped();
    void onRemoteToLocalSyncFailure(ErrorString errorDescription);
    void onRemoteToLocalSynchronizedContentFromUsersOwnAccount(qint32 lastUpdateCount, qevercloud::Timestamp lastSyncTime);

    void onShouldRepeatIncrementalSync();
    void onConflictDetectedDuringLocalChangesSending();

    void onLocalChangesSent(qint32 lastUpdateCount, QHash<QString,qint32> lastUpdateCountByLinkedNotebookGuid);
    void onSendLocalChangesStopped();
    void onSendLocalChangesFailure(ErrorString errorDescription);

    void onRateLimitExceeded(qint32 secondsToWait);

private:
    void createConnections(IAuthenticationManager & authenticationManager);

    void readLastSyncParameters();

    struct AuthContext
    {
        enum type {
            Blank = 0,
            SyncLaunch,
            Request,
            AuthToLinkedNotebooks,
        };
    };

    void launchOAuth();
    void authenticateImpl(const AuthContext::type authContext);
    void finalizeAuthentication();

    class AuthData: public Printable
    {
    public:
        qevercloud::UserID      m_userId;
        QString                 m_authToken;
        qevercloud::Timestamp   m_expirationTime;
        QString                 m_shardId;
        QString                 m_noteStoreUrl;
        QString                 m_webApiUrlPrefix;

        virtual QTextStream & print(QTextStream & strm) const Q_DECL_OVERRIDE;
    };

    void launchStoreOAuthResult(const AuthData & result);
    void finalizeStoreOAuthResult();

    void finalizeRevokeAuthentication();

    void launchSync();
    void launchFullSync();
    void launchIncrementalSync();
    void sendChanges();

    virtual void timerEvent(QTimerEvent * pTimerEvent);

    void clear();

    bool validAuthentication() const;
    bool checkIfTimestampIsAboutToExpireSoon(const qevercloud::Timestamp timestamp) const;
    void authenticateToLinkedNotebooks();

    void onReadAuthTokenFinished();
    void onReadShardIdFinished();
    void onWriteAuthTokenFinished();
    void onWriteShardIdFinished();
    void onDeleteAuthTokenFinished();
    void onDeleteShardIdFinished();

    void tryUpdateLastSyncStatus();
    void updatePersistentSyncSettings();

    INoteStore * noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook);
    INoteStore * noteStoreForLinkedNotebookGuid(const QString & guid);

    void launchStoreLinkedNotebookAuthToken(const QString & key, const QString & authToken);
    void postponeStoreLinkedNotebookAuthToken(const QString & key, const QString & authToken);

    void launchStoreLinkedNotebookShardId(const QString & key, const QString & shardId);
    void postponeStoreLinkedNotebookShardId(const QString & key, const QString & shardId);

private:
    class SendLocalChangesManagerController;
    friend class SendLocalChangesManagerController;

    class RemoteToLocalSynchronizationManagerController;
    friend class RemoteToLocalSynchronizationManagerController;

private:
    Q_DISABLE_COPY(SynchronizationManagerPrivate)

private:
    QString                                 m_host;

    qint32                                  m_maxSyncChunkEntries;

    qint32                                  m_previousUpdateCount;
    qint32                                  m_lastUpdateCount;
    qevercloud::Timestamp                   m_lastSyncTime;
    QHash<QString,qint32>                   m_cachedLinkedNotebookLastUpdateCountByGuid;
    QHash<QString,qevercloud::Timestamp>    m_cachedLinkedNotebookLastSyncTimeByGuid;
    bool                                    m_onceReadLastSyncParams;

    INoteStore *                            m_pNoteStore;
    QScopedPointer<IUserStore>              m_pUserStore;

    AuthContext::type                       m_authContext;

    int                                     m_launchSyncPostponeTimerId;

    AuthData                                m_OAuthResult;
    bool                                    m_authenticationInProgress;

    QScopedPointer<RemoteToLocalSynchronizationManagerController>   m_pRemoteToLocalSyncManagerController;
    RemoteToLocalSynchronizationManager     m_remoteToLocalSyncManager;

    // The flag coming from RemoteToLocalSynchronizationManager and telling whether something was downloaded
    // during the last remote to local sync
    bool                                    m_somethingDownloaded;

    QScopedPointer<SendLocalChangesManagerController>   m_pSendLocalChangesManagerController;
    SendLocalChangesManager                 m_sendLocalChangesManager;

    QHash<QString,QPair<QString,QString> >  m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid;
    QHash<QString,qevercloud::Timestamp>    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid;

    QVector<LinkedNotebookAuthData>         m_linkedNotebookAuthDataPendingAuthentication;

    QHash<QString, INoteStore*>             m_noteStoresByLinkedNotebookGuids;

    int                                     m_authenticateToLinkedNotebooksPostponeTimerId;

    QKeychain::ReadPasswordJob              m_readAuthTokenJob;
    QKeychain::ReadPasswordJob              m_readShardIdJob;
    bool                                    m_readingAuthToken;
    bool                                    m_readingShardId;

    QKeychain::WritePasswordJob             m_writeAuthTokenJob;
    QKeychain::WritePasswordJob             m_writeShardIdJob;
    bool                                    m_writingAuthToken;
    bool                                    m_writingShardId;
    AuthData                                m_writtenOAuthResult;

    QKeychain::DeletePasswordJob            m_deleteAuthTokenJob;
    QKeychain::DeletePasswordJob            m_deleteShardIdJob;
    bool                                    m_deletingAuthToken;
    bool                                    m_deletingShardId;
    qevercloud::UserID                      m_lastRevokedAuthenticationUserId;

    QHash<QString,QKeychain::ReadPasswordJob*>      m_readLinkedNotebookAuthTokenJobsByGuidKey;
    QHash<QString,QKeychain::ReadPasswordJob*>      m_readLinkedNotebookShardIdJobsByGuidKey;
    QHash<QString,QKeychain::WritePasswordJob*>     m_writeLinkedNotebookAuthTokenJobsByGuidKey;
    QHash<QString,QKeychain::WritePasswordJob*>     m_writeLinkedNotebookShardIdJobsByGuidKey;

    QHash<QString,QString>                  m_linkedNotebookAuthTokensPendingWritingByGuidKey;
    QHash<QString,QString>                  m_linkedNotebookShardIdsPendingWritingByGuidKey;

    QSet<QString>                           m_linkedNotebookGuidsWithoutLocalAuthData;

    bool                                    m_shouldRepeatIncrementalSyncAfterSendingChanges;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_PRIVATE_H
