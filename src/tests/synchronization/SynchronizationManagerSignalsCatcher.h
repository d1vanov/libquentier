/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/LinkedNotebook.h>

#include <QHash>
#include <QObject>
#include <QVector>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)
QT_FORWARD_DECLARE_CLASS(SynchronizationManager)

class SynchronizationManagerSignalsCatcher : public QObject
{
    Q_OBJECT
public:
    SynchronizationManagerSignalsCatcher(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManager & synchronizationManager,
        ISyncStateStorage & syncStateStorage, QObject * parent = nullptr);

    bool receivedStartedSignal() const
    {
        return m_receivedStartedSignal;
    }

    bool receivedStoppedSignal() const
    {
        return m_receivedStoppedSignal;
    }

    bool receivedFailedSignal() const
    {
        return m_receivedFailedSignal;
    }

    const ErrorString & failureErrorDescription() const
    {
        return m_failureErrorDescription;
    }

    bool receivedFinishedSignal() const
    {
        return m_receivedFinishedSignal;
    }

    const Account & finishedAccount() const
    {
        return m_finishedAccount;
    }

    bool finishedSomethingDownloaded() const
    {
        return m_finishedSomethingDownloaded;
    }

    bool finishedSomethingSent() const
    {
        return m_finishedSomethingSent;
    }

    bool receivedAuthenticationRevokedSignal() const
    {
        return m_receivedAuthenticationRevokedSignal;
    }

    bool authenticationRevokeSuccess() const
    {
        return m_authenticationRevokeSuccess;
    }

    const ErrorString & authenticationRevokeErrorDescription() const
    {
        return m_authenticationRevokeErrorDescription;
    }

    qevercloud::UserID authenticationRevokeUserId() const
    {
        return m_authenticationRevokeUserId;
    }

    bool receivedAuthenticationFinishedSignal() const
    {
        return m_receivedAuthenticationFinishedSignal;
    }

    bool authenticationSuccess() const
    {
        return m_authenticationSuccess;
    }

    const ErrorString & authenticationErrorDescription() const
    {
        return m_authenticationErrorDescription;
    }

    const Account & authenticationAccount() const
    {
        return m_authenticationAccount;
    }

    bool receivedRemoteToLocalSyncStopped() const
    {
        return m_receivedRemoteToLocalSyncStopped;
    }

    bool receivedSendLocalChangedStopped() const
    {
        return m_receivedSendLocalChangesStopped;
    }

    bool receivedWillRepeatRemoteToLocalSyncAfterSendingChanges() const
    {
        return m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges;
    }

    bool receivedDetectedConflictDuringLocalChangesSending() const
    {
        return m_receivedDetectedConflictDuringLocalChangesSending;
    }

    bool receivedRateLimitExceeded() const
    {
        return m_receivedRateLimitExceeded;
    }

    qint32 rateLimitSeconds() const
    {
        return m_rateLimitSeconds;
    }

    bool receivedRemoteToLocalSyncDone() const
    {
        return m_receivedRemoteToLocalSyncDone;
    }

    bool remoteToLocalSyncDoneSomethingDownloaded() const
    {
        return m_remoteToLocalSyncDoneSomethingDownloaded;
    }

    bool receivedSyncChunksDownloaded() const
    {
        return m_receivedSyncChunksDownloaded;
    }

    bool receivedLinkedNotebookSyncChunksDownloaded() const
    {
        return m_receivedLinkedNotebookSyncChunksDownloaded;
    }

    struct SyncChunkDownloadProgress
    {
        qint32 m_highestDownloadedUsn = 0;
        qint32 m_highestServerUsn = 0;
        qint32 m_lastPreviousUsn = 0;
    };

    const QVector<SyncChunkDownloadProgress> & syncChunkDownloadProgress() const
    {
        return m_syncChunkDownloadProgress;
    }

    const QHash<QString, QVector<SyncChunkDownloadProgress>> &
    linkedNotebookSyncChunksDownloadProgress() const
    {
        return m_linkedNotebookSyncChunkDownloadProgress;
    }

    struct NoteDownloadProgress
    {
        quint32 m_notesDownloaded = 0;
        quint32 m_totalNotesToDownload = 0;
    };

    const QVector<NoteDownloadProgress> & noteDownloadProgress() const
    {
        return m_noteDownloadProgress;
    }

    const QVector<NoteDownloadProgress> & linkedNotebookNoteDownloadProgress()
        const
    {
        return m_linkedNotebookNoteDownloadProgress;
    }

    struct ResourceDownloadProgress
    {
        quint32 m_resourcesDownloaded = 0;
        quint32 m_totalResourcesToDownload = 0;
    };

    const QVector<ResourceDownloadProgress> & resourceDownloadProgress() const
    {
        return m_resourceDownloadProgress;
    }

    const QVector<ResourceDownloadProgress> &
    linkedNotebookResourceDownloadProgress() const
    {
        return m_linkedNotebookResourceDownloadProgress;
    }

    bool receivedPreparedDirtyObjectsForSending() const
    {
        return m_receivedPreparedDirtyObjectsForSending;
    }

    bool receivedPreparedLinkedNotebookDirtyObjectsForSending() const
    {
        return m_receivedPreparedLinkedNotebookDirtyObjectsForSending;
    }

    struct PersistedSyncStateUpdateCounts
    {
        qint32 m_userOwnUpdateCount = 0;
        QHash<QString, qint32> m_linkedNotebookUpdateCountsByLinkedNotebookGuid;
    };

    const QVector<PersistedSyncStateUpdateCounts> &
    persistedSyncStateUpdateCounts() const
    {
        return m_persistedSyncStateUpdateCounts;
    }

public:
    bool checkSyncChunkDownloadProgressOrder(
        ErrorString & errorDescription) const;

    bool checkLinkedNotebookSyncChunkDownloadProgressOrder(
        ErrorString & errorDescription) const;

    bool checkNoteDownloadProgressOrder(ErrorString & errorDescription) const;

    bool checkLinkedNotebookNoteDownloadProgressOrder(
        ErrorString & errorDescription) const;

    bool checkResourceDownloadProgressOrder(
        ErrorString & errorDescription) const;

    bool checkLinkedNotebookResourceDownloadProgressOrder(
        ErrorString & errorDescription) const;

Q_SIGNALS:
    void ready();

private Q_SLOTS:
    void onStart();
    void onStop();
    void onFailure(ErrorString errorDescription);

    void onFinish(
        Account account, bool somethingDownloaded, bool somethingSent);

    void onAuthenticationRevoked(
        bool success, ErrorString errorDescription, qevercloud::UserID userId);

    void onAuthenticationFinished(
        bool success, ErrorString errorDescription, Account account);

    void onRemoteToLocalSyncStopped();
    void onSendLocalChangesStopped();
    void onWillRepeatRemoteToLocalSyncAfterSendingLocalChanges();
    void onDetectedConflictDuringLocalChangesSending();
    void onRateLimitExceeded(qint32 rateLimitSeconds);
    void onRemoteToLocalSyncDone(bool somethingDownloaded);
    void onSyncChunksDownloaded();
    void onLinkedNotebookSyncChunksDownloaded();

    void onSyncChunkDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn);

    void onLinkedNotebookSyncChunkDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn, LinkedNotebook linkedNotebook);

    void onNoteDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void onLinkedNotebookNoteDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void onResourceDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void onLinkedNotebookResourceDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void onPreparedDirtyObjectsForSending();
    void onPreparedLinkedNotebookDirtyObjectsForSending();

    void onSyncStatePersisted(
        Account account, ISyncStateStorage::ISyncStatePtr syncState);

    void onNoteMovedToAnotherNotebook(
        QString noteLocalUid, QString previousNotebookLocalUid,
        QString newNotebookLocalUid);

    void onNoteTagListChanged(
        QString noteLocalUid, QStringList previousNoteTagLocalUids,
        QStringList newNoteTagLocalUids);

private:
    void createConnections(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManager & synchronizationManager,
        ISyncStateStorage & syncStateStorage);

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
    bool m_receivedStartedSignal = false;
    bool m_receivedStoppedSignal = false;

    bool m_receivedFailedSignal = false;
    ErrorString m_failureErrorDescription;

    bool m_receivedFinishedSignal = false;
    Account m_finishedAccount;
    bool m_finishedSomethingDownloaded = false;
    bool m_finishedSomethingSent = false;

    bool m_receivedAuthenticationRevokedSignal = false;
    bool m_authenticationRevokeSuccess = false;
    ErrorString m_authenticationRevokeErrorDescription;
    qevercloud::UserID m_authenticationRevokeUserId = 0;

    bool m_receivedAuthenticationFinishedSignal = false;
    bool m_authenticationSuccess = false;
    ErrorString m_authenticationErrorDescription;
    Account m_authenticationAccount;

    bool m_receivedRemoteToLocalSyncStopped = false;
    bool m_receivedSendLocalChangesStopped = false;
    bool m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges = false;
    bool m_receivedDetectedConflictDuringLocalChangesSending = false;

    bool m_receivedRateLimitExceeded = false;
    qint32 m_rateLimitSeconds = 0;

    bool m_receivedRemoteToLocalSyncDone = false;
    bool m_remoteToLocalSyncDoneSomethingDownloaded = false;

    bool m_receivedSyncChunksDownloaded = false;
    bool m_receivedLinkedNotebookSyncChunksDownloaded = false;

    QVector<SyncChunkDownloadProgress> m_syncChunkDownloadProgress;
    QHash<QString, QVector<SyncChunkDownloadProgress>>
        m_linkedNotebookSyncChunkDownloadProgress;

    QVector<NoteDownloadProgress> m_noteDownloadProgress;
    QVector<NoteDownloadProgress> m_linkedNotebookNoteDownloadProgress;

    QVector<ResourceDownloadProgress> m_resourceDownloadProgress;
    QVector<ResourceDownloadProgress> m_linkedNotebookResourceDownloadProgress;

    QVector<PersistedSyncStateUpdateCounts> m_persistedSyncStateUpdateCounts;

    bool m_receivedPreparedDirtyObjectsForSending = false;
    bool m_receivedPreparedLinkedNotebookDirtyObjectsForSending = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_SIGNALS_CATCHER_H
