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

#include "SynchronizationManagerSignalsCatcher.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/SynchronizationManager.h>
#include <quentier/utility/Compat.h>

#include <QtTest/QtTest>

namespace quentier {

SynchronizationManagerSignalsCatcher::SynchronizationManagerSignalsCatcher(
    LocalStorageManagerAsync & localStorageManagerAsync,
    SynchronizationManager & synchronizationManager,
    ISyncStateStorage & syncStateStorage, QObject * parent) :
    QObject(parent)
{
    createConnections(
        localStorageManagerAsync, synchronizationManager, syncStateStorage);
}

bool SynchronizationManagerSignalsCatcher::checkSyncChunkDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkSyncChunkDownloadProgressOrderImpl(
        m_syncChunkDownloadProgress, errorDescription);
}

bool SynchronizationManagerSignalsCatcher::
    checkLinkedNotebookSyncChunkDownloadProgressOrder(
        ErrorString & errorDescription) const
{
    for (const auto it: qevercloud::toRange(
             qAsConst(m_linkedNotebookSyncChunkDownloadProgress)))
    {
        bool res = checkSyncChunkDownloadProgressOrderImpl(
            it.value(), errorDescription);

        if (!res) {
            return false;
        }
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkNoteDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkNoteDownloadProgressOrderImpl(
        m_noteDownloadProgress, errorDescription);
}

bool SynchronizationManagerSignalsCatcher::
    checkLinkedNotebookNoteDownloadProgressOrder(
        ErrorString & errorDescription) const
{
    return checkNoteDownloadProgressOrderImpl(
        m_linkedNotebookNoteDownloadProgress, errorDescription);
}

bool SynchronizationManagerSignalsCatcher::checkResourceDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkResourceDownloadProgressOrderImpl(
        m_resourceDownloadProgress, errorDescription);
}

bool SynchronizationManagerSignalsCatcher::
    checkLinkedNotebookResourceDownloadProgressOrder(
        ErrorString & errorDescription) const
{
    return checkResourceDownloadProgressOrderImpl(
        m_linkedNotebookResourceDownloadProgress, errorDescription);
}

void SynchronizationManagerSignalsCatcher::onStart()
{
    m_receivedStartedSignal = true;
}

void SynchronizationManagerSignalsCatcher::onStop()
{
    m_receivedStoppedSignal = true;
}

void SynchronizationManagerSignalsCatcher::onFailure(
    ErrorString errorDescription)
{
    m_receivedFailedSignal = true;
    m_failureErrorDescription = errorDescription;

    auto * pSyncManager = qobject_cast<SynchronizationManager *>(sender());
    if (pSyncManager) {
        pSyncManager->stop();
    }

    Q_EMIT ready();
}

void SynchronizationManagerSignalsCatcher::onFinish(
    Account account, bool somethingDownloaded, bool somethingSent)
{
    m_receivedFinishedSignal = true;
    m_finishedAccount = account;
    m_finishedSomethingDownloaded = somethingDownloaded;
    m_finishedSomethingSent = somethingSent;

    Q_EMIT ready();
}

void SynchronizationManagerSignalsCatcher::onAuthenticationRevoked(
    bool success, ErrorString errorDescription, qevercloud::UserID userId)
{
    m_receivedAuthenticationRevokedSignal = true;
    m_authenticationRevokeSuccess = success;
    m_authenticationRevokeErrorDescription = errorDescription;
    m_authenticationRevokeUserId = userId;
}

void SynchronizationManagerSignalsCatcher::onAuthenticationFinished(
    bool success, ErrorString errorDescription, Account account)
{
    m_receivedAuthenticationFinishedSignal = true;
    m_authenticationSuccess = success;
    m_authenticationErrorDescription = errorDescription;
    m_authenticationAccount = account;
}

void SynchronizationManagerSignalsCatcher::onRemoteToLocalSyncStopped()
{
    m_receivedRemoteToLocalSyncStopped = true;
}

void SynchronizationManagerSignalsCatcher::onSendLocalChangesStopped()
{
    m_receivedSendLocalChangesStopped = true;
}

void SynchronizationManagerSignalsCatcher::
    onWillRepeatRemoteToLocalSyncAfterSendingLocalChanges()
{
    m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges = true;
}

void SynchronizationManagerSignalsCatcher::
    onDetectedConflictDuringLocalChangesSending()
{
    m_receivedDetectedConflictDuringLocalChangesSending = true;
}

void SynchronizationManagerSignalsCatcher::onRateLimitExceeded(
    qint32 rateLimitSeconds)
{
    m_receivedRateLimitExceeded = true;
    m_rateLimitSeconds = rateLimitSeconds;
}

void SynchronizationManagerSignalsCatcher::onRemoteToLocalSyncDone(
    bool somethingDownloaded)
{
    m_receivedRemoteToLocalSyncDone = true;
    m_remoteToLocalSyncDoneSomethingDownloaded = somethingDownloaded;
}

void SynchronizationManagerSignalsCatcher::onSyncChunksDownloaded()
{
    m_receivedSyncChunksDownloaded = true;
}

void SynchronizationManagerSignalsCatcher::
    onLinkedNotebookSyncChunksDownloaded()
{
    m_receivedLinkedNotebookSyncChunksDownloaded = true;
}

void SynchronizationManagerSignalsCatcher::onSyncChunkDownloadProgress(
    qint32 highestDownloadedUsn, qint32 highestServerUsn,
    qint32 lastPreviousUsn)
{
    QNDEBUG(
        "tests:synchronization",
        "SynchronizationManagerSignalsCatcher::onSyncChunkDownloadProgress: "
            << "highest downloaded USN = " << highestDownloadedUsn
            << ", highest server USN = " << highestServerUsn
            << ", last previous USN = " << lastPreviousUsn);

    SyncChunkDownloadProgress progress;
    progress.m_highestDownloadedUsn = highestDownloadedUsn;
    progress.m_highestServerUsn = highestServerUsn;
    progress.m_lastPreviousUsn = lastPreviousUsn;

    m_syncChunkDownloadProgress << progress;
}

void SynchronizationManagerSignalsCatcher::
    onLinkedNotebookSyncChunkDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn, LinkedNotebook linkedNotebook)
{
    QNDEBUG(
        "tests:synchronization",
        "SynchronizationManagerSignalsCatcher"
            << "::onLinkedNotebookSyncChunkDownloadProgress: "
            << "highest downloaded USN = " << highestDownloadedUsn
            << ", highest server USN = " << highestServerUsn
            << ", last previous USN = " << lastPreviousUsn
            << ", linked notebook: " << linkedNotebook);

    QVERIFY2(
        linkedNotebook.hasGuid(),
        "Detected sync chunk download progress "
        "for a linked notebook without guid");

    SyncChunkDownloadProgress progress;
    progress.m_highestDownloadedUsn = highestDownloadedUsn;
    progress.m_highestServerUsn = highestServerUsn;
    progress.m_lastPreviousUsn = lastPreviousUsn;

    m_linkedNotebookSyncChunkDownloadProgress[linkedNotebook.guid()]
        << progress;
}

void SynchronizationManagerSignalsCatcher::onNoteDownloadProgress(
    quint32 notesDownloaded, quint32 totalNotesToDownload)
{
    NoteDownloadProgress progress;
    progress.m_notesDownloaded = notesDownloaded;
    progress.m_totalNotesToDownload = totalNotesToDownload;

    m_noteDownloadProgress << progress;
}

void SynchronizationManagerSignalsCatcher::onLinkedNotebookNoteDownloadProgress(
    quint32 notesDownloaded, quint32 totalNotesToDownload)
{
    NoteDownloadProgress progress;
    progress.m_notesDownloaded = notesDownloaded;
    progress.m_totalNotesToDownload = totalNotesToDownload;

    m_linkedNotebookNoteDownloadProgress << progress;
}

void SynchronizationManagerSignalsCatcher::onResourceDownloadProgress(
    quint32 resourcesDownloaded, quint32 totalResourcesToDownload)
{
    ResourceDownloadProgress progress;
    progress.m_resourcesDownloaded = resourcesDownloaded;
    progress.m_totalResourcesToDownload = totalResourcesToDownload;

    m_resourceDownloadProgress << progress;
}

void SynchronizationManagerSignalsCatcher::
    onLinkedNotebookResourceDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload)
{
    ResourceDownloadProgress progress;
    progress.m_resourcesDownloaded = resourcesDownloaded;
    progress.m_totalResourcesToDownload = totalResourcesToDownload;

    m_linkedNotebookResourceDownloadProgress << progress;
}

void SynchronizationManagerSignalsCatcher::onPreparedDirtyObjectsForSending()
{
    m_receivedPreparedDirtyObjectsForSending = true;
}

void SynchronizationManagerSignalsCatcher::
    onPreparedLinkedNotebookDirtyObjectsForSending()
{
    m_receivedPreparedLinkedNotebookDirtyObjectsForSending = true;
}

void SynchronizationManagerSignalsCatcher::onSyncStatePersisted(
    Account account, ISyncStateStorage::ISyncStatePtr syncState)
{
    Q_UNUSED(account)

    PersistedSyncStateUpdateCounts data;
    data.m_userOwnUpdateCount = syncState->userDataUpdateCount();

    data.m_linkedNotebookUpdateCountsByLinkedNotebookGuid =
        syncState->linkedNotebookUpdateCounts();

    m_persistedSyncStateUpdateCounts << data;
}

void SynchronizationManagerSignalsCatcher::onNoteMovedToAnotherNotebook(
    QString noteLocalUid, QString previousNotebookLocalUid,
    QString newNotebookLocalUid)
{
    Q_UNUSED(noteLocalUid)
    Q_UNUSED(previousNotebookLocalUid)
    Q_UNUSED(newNotebookLocalUid)
}

void SynchronizationManagerSignalsCatcher::onNoteTagListChanged(
    QString noteLocalUid, QStringList previousNoteTagLocalUids,
    QStringList newNoteTagLocalUids)
{
    Q_UNUSED(noteLocalUid)
    Q_UNUSED(previousNoteTagLocalUids)
    Q_UNUSED(newNoteTagLocalUids)
}

void SynchronizationManagerSignalsCatcher::createConnections(
    LocalStorageManagerAsync & localStorageManagerAsync,
    SynchronizationManager & synchronizationManager,
    ISyncStateStorage & syncStateStorage)
{
    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::noteMovedToAnotherNotebook, this,
        &SynchronizationManagerSignalsCatcher::onNoteMovedToAnotherNotebook);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::noteTagListChanged, this,
        &SynchronizationManagerSignalsCatcher::onNoteTagListChanged);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::started, this,
        &SynchronizationManagerSignalsCatcher::onStart);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::stopped, this,
        &SynchronizationManagerSignalsCatcher::onStop);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::failed, this,
        &SynchronizationManagerSignalsCatcher::onFailure);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::finished, this,
        &SynchronizationManagerSignalsCatcher::onFinish);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::authenticationRevoked,
        this, &SynchronizationManagerSignalsCatcher::onAuthenticationRevoked);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::authenticationFinished, this,
        &SynchronizationManagerSignalsCatcher::onAuthenticationFinished);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::remoteToLocalSyncStopped, this,
        &SynchronizationManagerSignalsCatcher::onRemoteToLocalSyncStopped);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::sendLocalChangesStopped, this,
        &SynchronizationManagerSignalsCatcher::onSendLocalChangesStopped);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::willRepeatRemoteToLocalSyncAfterSendingChanges,
        this,
        &SynchronizationManagerSignalsCatcher::
            onWillRepeatRemoteToLocalSyncAfterSendingLocalChanges);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::detectedConflictDuringLocalChangesSending,
        this,
        &SynchronizationManagerSignalsCatcher::
            onDetectedConflictDuringLocalChangesSending);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::rateLimitExceeded,
        this, &SynchronizationManagerSignalsCatcher::onRateLimitExceeded);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::remoteToLocalSyncDone,
        this, &SynchronizationManagerSignalsCatcher::onRemoteToLocalSyncDone);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::syncChunksDownloaded,
        this, &SynchronizationManagerSignalsCatcher::onSyncChunksDownloaded);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::linkedNotebooksSyncChunksDownloaded, this,
        &SynchronizationManagerSignalsCatcher::
            onLinkedNotebookSyncChunksDownloaded);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::syncChunksDownloadProgress, this,
        &SynchronizationManagerSignalsCatcher::onSyncChunkDownloadProgress);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::linkedNotebookSyncChunksDownloadProgress, this,
        &SynchronizationManagerSignalsCatcher::
            onLinkedNotebookSyncChunkDownloadProgress);

    QObject::connect(
        &synchronizationManager, &SynchronizationManager::notesDownloadProgress,
        this, &SynchronizationManagerSignalsCatcher::onNoteDownloadProgress);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::linkedNotebooksNotesDownloadProgress, this,
        &SynchronizationManagerSignalsCatcher::
            onLinkedNotebookNoteDownloadProgress);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::resourcesDownloadProgress, this,
        &SynchronizationManagerSignalsCatcher::onResourceDownloadProgress);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::preparedDirtyObjectsForSending, this,
        &SynchronizationManagerSignalsCatcher::
            onPreparedDirtyObjectsForSending);

    QObject::connect(
        &synchronizationManager,
        &SynchronizationManager::preparedLinkedNotebooksDirtyObjectsForSending,
        this,
        &SynchronizationManagerSignalsCatcher::
            onPreparedLinkedNotebookDirtyObjectsForSending);

    QObject::connect(
        &syncStateStorage, &ISyncStateStorage::notifySyncStateUpdated, this,
        &SynchronizationManagerSignalsCatcher::onSyncStatePersisted);
}

bool SynchronizationManagerSignalsCatcher::
    checkSyncChunkDownloadProgressOrderImpl(
        const QVector<SyncChunkDownloadProgress> & syncChunkDownloadProgress,
        ErrorString & errorDescription) const
{
    if (syncChunkDownloadProgress.isEmpty()) {
        return true;
    }

    if (syncChunkDownloadProgress.size() == 1) {
        return checkSingleSyncChunkDownloadProgress(
            syncChunkDownloadProgress[0], errorDescription);
    }

    for (int i = 1, size = syncChunkDownloadProgress.size(); i < size; ++i) {
        const auto & currentProgress = syncChunkDownloadProgress[i];

        bool res = checkSingleSyncChunkDownloadProgress(
            currentProgress, errorDescription);

        if (!res) {
            return false;
        }

        const auto & previousProgress = syncChunkDownloadProgress[i - 1];
        if (i == 1) {
            res = checkSingleSyncChunkDownloadProgress(
                previousProgress, errorDescription);

            if (!res) {
                return false;
            }
        }

        if (previousProgress.m_highestDownloadedUsn >=
            currentProgress.m_highestDownloadedUsn)
        {
            errorDescription.setBase(
                QStringLiteral("Found decreasing highest downloaded USN"));
            return false;
        }

        if (previousProgress.m_highestServerUsn !=
            currentProgress.m_highestServerUsn) {
            errorDescription.setBase(
                QStringLiteral("Highest server USN changed between two sync "
                               "chunk download progresses"));
            return false;
        }

        if (previousProgress.m_lastPreviousUsn !=
            currentProgress.m_lastPreviousUsn) {
            errorDescription.setBase(
                QStringLiteral("Last previous USN changed between two sync "
                               "chunk download progresses"));
            return false;
        }
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkSingleSyncChunkDownloadProgress(
    const SyncChunkDownloadProgress & progress,
    ErrorString & errorDescription) const
{
    if (progress.m_highestDownloadedUsn > progress.m_highestServerUsn) {
        errorDescription.setBase(
            QStringLiteral("Detected highest downloaded USN "
                           "greater than highest server USN"));
        return false;
    }

    if (progress.m_lastPreviousUsn > progress.m_highestDownloadedUsn) {
        errorDescription.setBase(
            QStringLiteral("Detected last previous USN greater than highest "
                           "downloaded USN"));
        return false;
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkNoteDownloadProgressOrderImpl(
    const QVector<NoteDownloadProgress> & noteDownloadProgress,
    ErrorString & errorDescription) const
{
    if (noteDownloadProgress.isEmpty()) {
        return true;
    }

    if (noteDownloadProgress.size() == 1) {
        return checkSingleNoteDownloadProgress(
            noteDownloadProgress[0], errorDescription);
    }

    for (int i = 1, size = noteDownloadProgress.size(); i < size; ++i) {
        const auto & currentProgress = noteDownloadProgress[i];
        if (!checkSingleNoteDownloadProgress(currentProgress, errorDescription))
        {
            return false;
        }

        const auto & previousProgress = noteDownloadProgress[i - 1];
        if ((i == 1) &&
            !checkSingleNoteDownloadProgress(
                previousProgress, errorDescription))
        {
            return false;
        }

        if (previousProgress.m_notesDownloaded >=
            currentProgress.m_notesDownloaded) {
            errorDescription.setBase(
                QStringLiteral("Found non-increasing downloaded notes count"));
            return false;
        }

        if (previousProgress.m_totalNotesToDownload !=
            currentProgress.m_totalNotesToDownload)
        {
            errorDescription.setBase(
                QStringLiteral("The total number of notes to download has "
                               "changed between two progresses"));
            return false;
        }
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkSingleNoteDownloadProgress(
    const NoteDownloadProgress & progress, ErrorString & errorDescription) const
{
    if (progress.m_notesDownloaded > progress.m_totalNotesToDownload) {
        errorDescription.setBase(
            QStringLiteral("The number of downloaded notes is greater than "
                           "the total number of notes to download"));
        return false;
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::
    checkResourceDownloadProgressOrderImpl(
        const QVector<ResourceDownloadProgress> & resourceDownloadProgress,
        ErrorString & errorDescription) const
{
    if (resourceDownloadProgress.isEmpty()) {
        return true;
    }

    if (resourceDownloadProgress.size() == 1) {
        return checkSingleResourceDownloadProgress(
            resourceDownloadProgress[0], errorDescription);
    }

    for (int i = 1, size = resourceDownloadProgress.size(); i < size; ++i) {
        const auto & currentProgress = resourceDownloadProgress[i];

        bool res = checkSingleResourceDownloadProgress(
            currentProgress, errorDescription);

        if (!res) {
            return false;
        }

        const auto & previousProgress = resourceDownloadProgress[i - 1];
        if (i == 1) {
            res = checkSingleResourceDownloadProgress(
                previousProgress, errorDescription);

            if (!res) {
                return false;
            }
        }

        if (previousProgress.m_resourcesDownloaded >=
            currentProgress.m_resourcesDownloaded)
        {
            errorDescription.setBase(
                "Found non-increasing downloaded resources "
                "count");

            return false;
        }

        if (previousProgress.m_totalResourcesToDownload !=
            currentProgress.m_totalResourcesToDownload)
        {
            errorDescription.setBase(
                "The total number of resources to download has "
                "changed between two progresses");

            return false;
        }
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkSingleResourceDownloadProgress(
    const ResourceDownloadProgress & progress,
    ErrorString & errorDescription) const
{
    if (progress.m_resourcesDownloaded > progress.m_totalResourcesToDownload) {
        errorDescription.setBase(
            "The number of downloaded resources is greater than "
            "the total number of resources to download");

        return false;
    }

    return true;
}

} // namespace quentier
