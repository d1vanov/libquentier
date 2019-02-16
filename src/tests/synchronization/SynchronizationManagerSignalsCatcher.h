/*
 * Copyright 2018-2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_SIGNALS_CATCHER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_SIGNALS_CATCHER_H

#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Account.h>
#include <quentier/types/LinkedNotebook.h>
#include <QObject>
#include <QVector>
#include <QHash>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SynchronizationManager)
QT_FORWARD_DECLARE_CLASS(SyncStatePersistenceManager)

class SynchronizationManagerSignalsCatcher: public QObject
{
    Q_OBJECT
public:
    SynchronizationManagerSignalsCatcher(
        SynchronizationManager & synchronizationManager,
        SyncStatePersistenceManager & syncStatePersistenceManager,
        QObject * parent = Q_NULLPTR);

    bool receivedStartedSignal() const
    { return m_receivedStartedSignal; }

    bool receivedStoppedSignal() const
    { return m_receivedStoppedSignal; }

    bool receivedFailedSignal() const
    { return m_receivedFailedSignal; }

    const ErrorString & failureErrorDescription() const
    { return m_failureErrorDescription; }

    bool receivedFinishedSignal() const
    { return m_receivedFinishedSignal; }

    const Account & finishedAccount() const
    { return m_finishedAccount; }

    bool finishedSomethingDownloaded() const
    { return m_finishedSomethingDownloaded; }

    bool finishedSomethingSent() const
    { return m_finishedSomethingSent; }

    bool receivedAuthenticationRevokedSignal() const
    { return m_receivedAuthenticationRevokedSignal; }

    bool authenticationRevokeSuccess() const
    { return m_authenticationRevokeSuccess; }

    const ErrorString & authenticationRevokeErrorDescription() const
    { return m_authenticationRevokeErrorDescription; }

    qevercloud::UserID authenticationRevokeUserId() const
    { return m_authenticationRevokeUserId; }

    bool receivedAuthenticationFinishedSignal() const
    { return m_receivedAuthenticationFinishedSignal; }

    bool authenticationSuccess() const
    { return m_authenticationSuccess; }

    const ErrorString & authenticationErrorDescription() const
    { return m_authenticationErrorDescription; }

    const Account & authenticationAccount() const
    { return m_authenticationAccount; }

    bool receivedRemoteToLocalSyncStopped() const
    { return m_receivedRemoteToLocalSyncStopped; }

    bool receivedSendLocalChangedStopped() const
    { return m_receivedSendLocalChangesStopped; }

    bool receivedWillRepeatRemoteToLocalSyncAfterSendingChanges() const
    { return m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges; }

    bool receivedDetectedConflictDuringLocalChangesSending() const
    { return m_receivedDetectedConflictDuringLocalChangesSending; }

    bool receivedRateLimitExceeded() const
    { return m_receivedRateLimitExceeded; }

    qint32 rateLimitSeconds() const
    { return m_rateLimitSeconds; }

    bool receivedRemoteToLocalSyncDone() const
    { return m_receivedRemoteToLocalSyncDone; }

    bool remoteToLocalSyncDoneSomethingDownloaded() const
    { return m_remoteToLocalSyncDoneSomethingDownloaded; }

    bool receivedSyncChunksDownloaded() const
    { return m_receivedSyncChunksDownloaded; }

    bool receivedLinkedNotebookSyncChunksDownloaded() const
    { return m_receivedLinkedNotebookSyncChunksDownloaded; }

    struct SyncChunkDownloadProgress
    {
        SyncChunkDownloadProgress() :
            m_highestDownloadedUsn(0),
            m_highestServerUsn(0),
            m_lastPreviousUsn(0)
        {}

        qint32 m_highestDownloadedUsn;
        qint32 m_highestServerUsn;
        qint32 m_lastPreviousUsn;
    };

    const QVector<SyncChunkDownloadProgress> & syncChunkDownloadProgress() const
    { return m_syncChunkDownloadProgress; }

    const QHash<QString, QVector<SyncChunkDownloadProgress> > &
    linkedNotebookSyncChunksDownloadProgress() const
    { return m_linkedNotebookSyncChunkDownloadProgress; }

    struct NoteDownloadProgress
    {
        NoteDownloadProgress() :
            m_notesDownloaded(0),
            m_totalNotesToDownload(0)
        {}

        quint32     m_notesDownloaded;
        quint32     m_totalNotesToDownload;
    };

    const QVector<NoteDownloadProgress> & noteDownloadProgress() const
    { return m_noteDownloadProgress; }

    const QVector<NoteDownloadProgress> & linkedNotebookNoteDownloadProgress() const
    { return m_linkedNotebookNoteDownloadProgress; }

    struct ResourceDownloadProgress
    {
        ResourceDownloadProgress() :
            m_resourcesDownloaded(0),
            m_totalResourcesToDownload(0)
        {}

        quint32     m_resourcesDownloaded;
        quint32     m_totalResourcesToDownload;
    };

    const QVector<ResourceDownloadProgress> & resourceDownloadProgress() const
    { return m_resourceDownloadProgress; }

    const QVector<ResourceDownloadProgress> &
    linkedNotebookResourceDownloadProgress() const
    { return m_linkedNotebookResourceDownloadProgress; }

    bool receivedPreparedDirtyObjectsForSending() const
    { return m_receivedPreparedDirtyObjectsForSending; }

    bool receivedPreparedLinkedNotebookDirtyObjectsForSending() const
    { return m_receivedPreparedLinkedNotebookDirtyObjectsForSending; }

    struct PersistedSyncStateUpdateCounts
    {
        PersistedSyncStateUpdateCounts() :
            m_userOwnUpdateCount(0),
            m_linkedNotebookUpdateCountsByLinkedNotebookGuid()
        {}

        qint32                  m_userOwnUpdateCount;
        QHash<QString,qint32>   m_linkedNotebookUpdateCountsByLinkedNotebookGuid;
    };

    const QVector<PersistedSyncStateUpdateCounts> &
    persistedSyncStateUpdateCounts() const
    { return m_persistedSyncStateUpdateCounts; }

public:
    bool checkSyncChunkDownloadProgressOrder(ErrorString & errorDescription) const;
    bool checkLinkedNotebookSyncChunkDownloadProgressOrder(
        ErrorString & errorDescription) const;

    bool checkNoteDownloadProgressOrder(ErrorString & errorDescription) const;
    bool checkLinkedNotebookNoteDownloadProgressOrder(
        ErrorString & errorDescription) const;

    bool checkResourceDownloadProgressOrder(ErrorString & errorDescription) const;
    bool checkLinkedNotebookResourceDownloadProgressOrder(
        ErrorString & errorDescription) const;

Q_SIGNALS:
    void ready();

private Q_SLOTS:
    void onStart();
    void onStop();
    void onFailure(ErrorString errorDescription);
    void onFinish(Account account, bool somethingDownloaded, bool somethingSent);
    void onAuthenticationRevoked(bool success, ErrorString errorDescription,
                                 qevercloud::UserID userId);
    void onAuthenticationFinished(bool success, ErrorString errorDescription,
                                  Account account);
    void onRemoteToLocalSyncStopped();
    void onSendLocalChangesStopped();
    void onWillRepeatRemoteToLocalSyncAfterSendingLocalChanges();
    void onDetectedConflictDuringLocalChangesSending();
    void onRateLimitExceeded(qint32 rateLimitSeconds);
    void onRemoteToLocalSyncDone(bool somethingDownloaded);
    void onSyncChunksDownloaded();
    void onLinkedNotebookSyncChunksDownloaded();
    void onSyncChunkDownloadProgress(qint32 highestDownloadedUsn,
                                     qint32 highestServerUsn,
                                     qint32 lastPreviousUsn);
    void onLinkedNotebookSyncChunkDownloadProgress(qint32 highestDownloadedUsn,
                                                   qint32 highestServerUsn,
                                                   qint32 lastPreviousUsn,
                                                   LinkedNotebook linkedNotebook);
    void onNoteDownloadProgress(quint32 notesDownloaded,
                                quint32 totalNotesToDownload);
    void onLinkedNotebookNoteDownloadProgress(quint32 notesDownloaded,
                                              quint32 totalNotesToDownload);
    void onResourceDownloadProgress(quint32 resourcesDownloaded,
                                    quint32 totalResourcesToDownload);
    void onLinkedNotebookResourceDownloadProgress(quint32 resourcesDownloaded,
                                                  quint32 totalResourcesToDownload);
    void onPreparedDirtyObjectsForSending();
    void onPreparedLinkedNotebookDirtyObjectsForSending();

    void onSyncStatePersisted(
        Account account, qint32 userOwnDataUpdateCount,
        qevercloud::Timestamp userOwnDataSyncTime,
        QHash<QString,qint32> linkedNotebookUpdateCountsByLinkedNotebookGuid,
        QHash<QString,qevercloud::Timestamp> linkedNotebookSyncTimesByLinkedNotebookGuid);

private:
    void createConnections(SynchronizationManager & synchronizationManager,
                           SyncStatePersistenceManager & syncStatePersistenceManager);

    bool checkSyncChunkDownloadProgressOrderImpl(
        const QVector<SyncChunkDownloadProgress> & syncChunkDownloadProgress,
        ErrorString & errorDescription) const;

    bool checkSingleSyncChunkDownloadProgress(
        const SyncChunkDownloadProgress & progress,
        ErrorString & errorDescription) const;

    bool checkNoteDownloadProgressOrderImpl(
        const QVector<NoteDownloadProgress> & noteDownloadProgress,
        ErrorString & errorDescription) const;

    bool checkSingleNoteDownloadProgress(
        const NoteDownloadProgress & progress,
        ErrorString & errorDescription) const;

    bool checkResourceDownloadProgressOrderImpl(
        const QVector<ResourceDownloadProgress> & resourceDownloadProgress,
        ErrorString & errorDescription) const;

    bool checkSingleResourceDownloadProgress(
        const ResourceDownloadProgress & progress,
        ErrorString & errorDescription) const;

private:
    bool            m_receivedStartedSignal;
    bool            m_receivedStoppedSignal;

    bool            m_receivedFailedSignal;
    ErrorString     m_failureErrorDescription;

    bool            m_receivedFinishedSignal;
    Account         m_finishedAccount;
    bool            m_finishedSomethingDownloaded;
    bool            m_finishedSomethingSent;

    bool            m_receivedAuthenticationRevokedSignal;
    bool            m_authenticationRevokeSuccess;
    ErrorString     m_authenticationRevokeErrorDescription;
    qevercloud::UserID  m_authenticationRevokeUserId;

    bool            m_receivedAuthenticationFinishedSignal;
    bool            m_authenticationSuccess;
    ErrorString     m_authenticationErrorDescription;
    Account         m_authenticationAccount;

    bool            m_receivedRemoteToLocalSyncStopped;
    bool            m_receivedSendLocalChangesStopped;
    bool            m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges;
    bool            m_receivedDetectedConflictDuringLocalChangesSending;

    bool            m_receivedRateLimitExceeded;
    qint32          m_rateLimitSeconds;

    bool            m_receivedRemoteToLocalSyncDone;
    bool            m_remoteToLocalSyncDoneSomethingDownloaded;

    bool            m_receivedSyncChunksDownloaded;
    bool            m_receivedLinkedNotebookSyncChunksDownloaded;

    QVector<SyncChunkDownloadProgress>      m_syncChunkDownloadProgress;
    QHash<QString, QVector<SyncChunkDownloadProgress> >     m_linkedNotebookSyncChunkDownloadProgress;

    QVector<NoteDownloadProgress>           m_noteDownloadProgress;
    QVector<NoteDownloadProgress>           m_linkedNotebookNoteDownloadProgress;

    QVector<ResourceDownloadProgress>       m_resourceDownloadProgress;
    QVector<ResourceDownloadProgress>       m_linkedNotebookResourceDownloadProgress;

    QVector<PersistedSyncStateUpdateCounts>     m_persistedSyncStateUpdateCounts;

    bool            m_receivedPreparedDirtyObjectsForSending;
    bool            m_receivedPreparedLinkedNotebookDirtyObjectsForSending;
};

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_SIGNALS_CATCHER_H
