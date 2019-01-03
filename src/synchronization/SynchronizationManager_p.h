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
#include <quentier_private/utility/IKeychainService.h>

// NOTE: Workaround a bug in Qt4 which may prevent building with some boost versions
#ifndef Q_MOC_RUN
#include <boost/bimap.hpp>
#endif

#include <QObject>
#include <QScopedPointer>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SynchronizationManagerDependencyInjector)
QT_FORWARD_DECLARE_CLASS(INoteStore)
QT_FORWARD_DECLARE_CLASS(IUserStore)
QT_FORWARD_DECLARE_CLASS(SyncStatePersistenceManager)

class Q_DECL_HIDDEN SynchronizationManagerPrivate: public QObject
{
    Q_OBJECT
public:
    SynchronizationManagerPrivate(const QString & host, LocalStorageManagerAsync & localStorageManagerAsync,
                                  IAuthenticationManager & authenticationManager,
                                  SynchronizationManagerDependencyInjector * pInjector,
                                  QObject * parent);
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
    void authenticateCurrentAccount();
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

private:
    // NOTE: this is required for Qt4 connection syntax, it won't properly understand IKeychainService::ErrorCode::type
    typedef IKeychainService::ErrorCode ErrorCode;

private Q_SLOTS:
    void onOAuthResult(bool success, qevercloud::UserID userId, QString authToken,
                       qevercloud::Timestamp authTokenExpirationTime, QString shardId,
                       QString noteStoreUrl, QString webApiUrlPrefix, ErrorString errorDescription);

    void onWritePasswordJobFinished(QUuid jobId, ErrorCode::type errorCode, ErrorString errorDescription);
    void onReadPasswordJobFinished(QUuid jobId, ErrorCode::type errorCode, ErrorString errorDescription, QString password);
    void onDeletePasswordJobFinished(QUuid jobId, ErrorCode::type errorCode, ErrorString errorDescription);

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
            NewUserRequest,
            CurrentUserRequest,
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

    void launchSync();
    void launchFullSync();
    void launchIncrementalSync();
    void sendChanges();

    virtual void timerEvent(QTimerEvent * pTimerEvent);

    void clear();

    bool validAuthentication() const;
    bool checkIfTimestampIsAboutToExpireSoon(const qevercloud::Timestamp timestamp) const;
    void authenticateToLinkedNotebooks();

    void onReadAuthTokenFinished(const IKeychainService::ErrorCode::type errorCode,
                                 const ErrorString & errorDescription, const QString & password);
    void onReadShardIdFinished(const IKeychainService::ErrorCode::type errorCode,
                               const ErrorString & errorDescription, const QString & password);
    void onWriteAuthTokenFinished(const IKeychainService::ErrorCode::type errorCode, const ErrorString & errorDescription);
    void onWriteShardIdFinished(const IKeychainService::ErrorCode::type errorCode, const ErrorString & errorDescription);
    void onDeleteAuthTokenFinished(const IKeychainService::ErrorCode::type errorCode, const ErrorString & errorDescription);
    void onDeleteShardIdFinished(const IKeychainService::ErrorCode::type errorCode, const ErrorString & errorDescription);

    void tryUpdateLastSyncStatus();
    void updatePersistentSyncSettings();

    INoteStore * noteStoreForLinkedNotebook(const LinkedNotebook & linkedNotebook);
    INoteStore * noteStoreForLinkedNotebookGuid(const QString & guid);

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

    SyncStatePersistenceManager *           m_pSyncStatePersistenceManager;
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
    RemoteToLocalSynchronizationManager *   m_pRemoteToLocalSyncManager;

    // The flag coming from RemoteToLocalSynchronizationManager and telling whether something was downloaded
    // during the last remote to local sync
    bool                                    m_somethingDownloaded;

    QScopedPointer<SendLocalChangesManagerController>   m_pSendLocalChangesManagerController;
    SendLocalChangesManager *               m_pSendLocalChangesManager;

    QHash<QString,QPair<QString,QString> >  m_cachedLinkedNotebookAuthTokensAndShardIdsByGuid;
    QHash<QString,qevercloud::Timestamp>    m_cachedLinkedNotebookAuthTokenExpirationTimeByGuid;

    QVector<LinkedNotebookAuthData>         m_linkedNotebookAuthDataPendingAuthentication;

    QHash<QString, INoteStore*>             m_noteStoresByLinkedNotebookGuids;

    int                                     m_authenticateToLinkedNotebooksPostponeTimerId;

    IKeychainService *                      m_pKeychainService;

    QUuid                                   m_readAuthTokenJobId;
    QUuid                                   m_readShardIdJobId;
    bool                                    m_readingAuthToken;
    bool                                    m_readingShardId;

    QUuid                                   m_writeAuthTokenJobId;
    QUuid                                   m_writeShardIdJobId;
    bool                                    m_writingAuthToken;
    bool                                    m_writingShardId;
    AuthData                                m_writtenOAuthResult;

    QUuid                                   m_deleteAuthTokenJobId;
    QUuid                                   m_deleteShardIdJobId;
    bool                                    m_deletingAuthToken;
    bool                                    m_deletingShardId;
    qevercloud::UserID                      m_lastRevokedAuthenticationUserId;

    typedef boost::bimap<QString,QUuid> JobIdWithGuidBimap;

    JobIdWithGuidBimap                      m_readLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids;
    JobIdWithGuidBimap                      m_readLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids;
    JobIdWithGuidBimap                      m_writeLinkedNotebookAuthTokenJobIdsWithLinkedNotebookGuids;
    JobIdWithGuidBimap                      m_writeLinkedNotebookShardIdJobIdsWithLinkedNotebookGuids;

    QHash<QString,QString>                  m_linkedNotebookAuthTokensPendingWritingByGuid;
    QHash<QString,QString>                  m_linkedNotebookShardIdsPendingWritingByGuid;

    QSet<QString>                           m_linkedNotebookGuidsWithoutLocalAuthData;

    bool                                    m_shouldRepeatIncrementalSyncAfterSendingChanges;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_PRIVATE_H
