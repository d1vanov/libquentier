/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#include <qevercloud/generated/types/LinkedNotebook.h>

#include <QHash>
#include <QObject>
#include <QVector>

namespace quentier {

class LocalStorageManagerAsync;
class SynchronizationManager;

class SynchronizationManagerSignalsCatcher final: public QObject
{
    Q_OBJECT
public:
    SynchronizationManagerSignalsCatcher(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManager & synchronizationManager,
        ISyncStateStorage & syncStateStorage, QObject * parent = nullptr);

    ~SynchronizationManagerSignalsCatcher() override;

    [[nodiscard]] bool receivedStartedSignal() const noexcept
    {
        return m_receivedStartedSignal;
    }

    [[nodiscard]] bool receivedStoppedSignal() const noexcept
    {
        return m_receivedStoppedSignal;
    }

    [[nodiscard]] bool receivedFailedSignal() const noexcept
    {
        return m_receivedFailedSignal;
    }

    [[nodiscard]] const ErrorString & failureErrorDescription() const noexcept
    {
        return m_failureErrorDescription;
    }

    [[nodiscard]] bool receivedFinishedSignal() const noexcept
    {
        return m_receivedFinishedSignal;
    }

    [[nodiscard]] const Account & finishedAccount() const noexcept
    {
        return m_finishedAccount;
    }

    [[nodiscard]] bool finishedSomethingDownloaded() const noexcept
    {
        return m_finishedSomethingDownloaded;
    }

    [[nodiscard]] bool finishedSomethingSent() const noexcept
    {
        return m_finishedSomethingSent;
    }

    [[nodiscard]] bool receivedAuthenticationRevokedSignal() const noexcept
    {
        return m_receivedAuthenticationRevokedSignal;
    }

    [[nodiscard]] bool authenticationRevokeSuccess() const noexcept
    {
        return m_authenticationRevokeSuccess;
    }

    [[nodiscard]] const ErrorString & authenticationRevokeErrorDescription()
        const noexcept
    {
        return m_authenticationRevokeErrorDescription;
    }

    [[nodiscard]] qevercloud::UserID authenticationRevokeUserId() const noexcept
    {
        return m_authenticationRevokeUserId;
    }

    [[nodiscard]] bool receivedAuthenticationFinishedSignal() const noexcept
    {
        return m_receivedAuthenticationFinishedSignal;
    }

    [[nodiscard]] bool authenticationSuccess() const noexcept
    {
        return m_authenticationSuccess;
    }

    [[nodiscard]] const ErrorString & authenticationErrorDescription()
        const noexcept
    {
        return m_authenticationErrorDescription;
    }

    [[nodiscard]] const Account & authenticationAccount() const noexcept
    {
        return m_authenticationAccount;
    }

    [[nodiscard]] bool receivedRemoteToLocalSyncStopped() const noexcept
    {
        return m_receivedRemoteToLocalSyncStopped;
    }

    [[nodiscard]] bool receivedSendLocalChangedStopped() const noexcept
    {
        return m_receivedSendLocalChangesStopped;
    }

    [[nodiscard]] bool receivedWillRepeatRemoteToLocalSyncAfterSendingChanges()
        const noexcept
    {
        return m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges;
    }

    [[nodiscard]] bool receivedDetectedConflictDuringLocalChangesSending()
        const noexcept
    {
        return m_receivedDetectedConflictDuringLocalChangesSending;
    }

    [[nodiscard]] bool receivedRateLimitExceeded() const noexcept
    {
        return m_receivedRateLimitExceeded;
    }

    [[nodiscard]] qint32 rateLimitSeconds() const noexcept
    {
        return m_rateLimitSeconds;
    }

    [[nodiscard]] bool receivedRemoteToLocalSyncDone() const noexcept
    {
        return m_receivedRemoteToLocalSyncDone;
    }

    [[nodiscard]] bool remoteToLocalSyncDoneSomethingDownloaded() const noexcept
    {
        return m_remoteToLocalSyncDoneSomethingDownloaded;
    }

    [[nodiscard]] bool receivedSyncChunksDownloaded() const noexcept
    {
        return m_receivedSyncChunksDownloaded;
    }

    [[nodiscard]] bool receivedLinkedNotebookSyncChunksDownloaded()
        const noexcept
    {
        return m_receivedLinkedNotebookSyncChunksDownloaded;
    }

    struct SyncChunkDownloadProgress
    {
        qint32 m_highestDownloadedUsn = 0;
        qint32 m_highestServerUsn = 0;
        qint32 m_lastPreviousUsn = 0;
    };

    [[nodiscard]] const QVector<SyncChunkDownloadProgress> &
    syncChunkDownloadProgress() const noexcept
    {
        return m_syncChunkDownloadProgress;
    }

    [[nodiscard]] const QHash<QString, QVector<SyncChunkDownloadProgress>> &
    linkedNotebookSyncChunksDownloadProgress() const noexcept
    {
        return m_linkedNotebookSyncChunkDownloadProgress;
    }

    struct NoteDownloadProgress
    {
        quint32 m_notesDownloaded = 0;
        quint32 m_totalNotesToDownload = 0;
    };

    [[nodiscard]] const QVector<NoteDownloadProgress> & noteDownloadProgress()
        const noexcept
    {
        return m_noteDownloadProgress;
    }

    [[nodiscard]] const QVector<NoteDownloadProgress> &
    linkedNotebookNoteDownloadProgress() const noexcept
    {
        return m_linkedNotebookNoteDownloadProgress;
    }

    struct ResourceDownloadProgress
    {
        quint32 m_resourcesDownloaded = 0;
        quint32 m_totalResourcesToDownload = 0;
    };

    [[nodiscard]] const QVector<ResourceDownloadProgress> &
    resourceDownloadProgress() const noexcept
    {
        return m_resourceDownloadProgress;
    }

    [[nodiscard]] const QVector<ResourceDownloadProgress> &
    linkedNotebookResourceDownloadProgress() const noexcept
    {
        return m_linkedNotebookResourceDownloadProgress;
    }

    [[nodiscard]] bool receivedPreparedDirtyObjectsForSending() const noexcept
    {
        return m_receivedPreparedDirtyObjectsForSending;
    }

    [[nodiscard]] bool receivedPreparedLinkedNotebookDirtyObjectsForSending()
        const noexcept
    {
        return m_receivedPreparedLinkedNotebookDirtyObjectsForSending;
    }

    struct PersistedSyncStateUpdateCounts
    {
        qint32 m_userOwnUpdateCount = 0;
        QHash<QString, qint32> m_linkedNotebookUpdateCountsByLinkedNotebookGuid;
    };

    [[nodiscard]] const QVector<PersistedSyncStateUpdateCounts> &
    persistedSyncStateUpdateCounts() const noexcept
    {
        return m_persistedSyncStateUpdateCounts;
    }

public:
    [[nodiscard]] bool checkSyncChunkDownloadProgressOrder(
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkLinkedNotebookSyncChunkDownloadProgressOrder(
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkNoteDownloadProgressOrder(
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkLinkedNotebookNoteDownloadProgressOrder(
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkResourceDownloadProgressOrder(
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkLinkedNotebookResourceDownloadProgressOrder(
        ErrorString & errorDescription) const noexcept;

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
        qint32 lastPreviousUsn, qevercloud::LinkedNotebook linkedNotebook);

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

    [[nodiscard]] bool checkSyncChunkDownloadProgressOrderImpl(
        const QVector<SyncChunkDownloadProgress> & syncChunkDownloadProgress,
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkSingleSyncChunkDownloadProgress(
        const SyncChunkDownloadProgress & progress,
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkNoteDownloadProgressOrderImpl(
        const QVector<NoteDownloadProgress> & noteDownloadProgress,
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkSingleNoteDownloadProgress(
        const NoteDownloadProgress & progress,
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkResourceDownloadProgressOrderImpl(
        const QVector<ResourceDownloadProgress> & resourceDownloadProgress,
        ErrorString & errorDescription) const noexcept;

    [[nodiscard]] bool checkSingleResourceDownloadProgress(
        const ResourceDownloadProgress & progress,
        ErrorString & errorDescription) const noexcept;

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
