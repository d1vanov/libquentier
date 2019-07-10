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

#include "SynchronizationManagerSignalsCatcher.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/synchronization/SynchronizationManager.h>
#include <quentier_private/synchronization/SyncStatePersistenceManager.h>
#include <quentier/logging/QuentierLogger.h>
#include <QtTest/QtTest>

namespace quentier {

SynchronizationManagerSignalsCatcher::SynchronizationManagerSignalsCatcher(
        LocalStorageManagerAsync & localStorageManagerAsync,
        SynchronizationManager & synchronizationManager,
        SyncStatePersistenceManager & syncStatePersistenceManager,
        QObject * parent) :
    QObject(parent),
    m_receivedStartedSignal(false),
    m_receivedStoppedSignal(false),
    m_receivedFailedSignal(false),
    m_failureErrorDescription(),
    m_receivedFinishedSignal(false),
    m_finishedAccount(),
    m_finishedSomethingDownloaded(false),
    m_finishedSomethingSent(false),
    m_receivedAuthenticationRevokedSignal(false),
    m_authenticationRevokeSuccess(false),
    m_authenticationRevokeErrorDescription(),
    m_authenticationRevokeUserId(0),
    m_receivedAuthenticationFinishedSignal(false),
    m_authenticationSuccess(false),
    m_authenticationErrorDescription(),
    m_authenticationAccount(),
    m_receivedRemoteToLocalSyncStopped(false),
    m_receivedSendLocalChangesStopped(false),
    m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges(false),
    m_receivedDetectedConflictDuringLocalChangesSending(false),
    m_receivedRateLimitExceeded(false),
    m_rateLimitSeconds(0),
    m_receivedRemoteToLocalSyncDone(false),
    m_remoteToLocalSyncDoneSomethingDownloaded(false),
    m_receivedSyncChunksDownloaded(false),
    m_receivedLinkedNotebookSyncChunksDownloaded(false),
    m_syncChunkDownloadProgress(),
    m_linkedNotebookSyncChunkDownloadProgress(),
    m_noteDownloadProgress(),
    m_linkedNotebookNoteDownloadProgress(),
    m_resourceDownloadProgress(),
    m_linkedNotebookResourceDownloadProgress(),
    m_receivedPreparedDirtyObjectsForSending(false),
    m_receivedPreparedLinkedNotebookDirtyObjectsForSending(false)
{
    createConnections(
        localStorageManagerAsync,
        synchronizationManager,
        syncStatePersistenceManager);
}

bool SynchronizationManagerSignalsCatcher::checkSyncChunkDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkSyncChunkDownloadProgressOrderImpl(m_syncChunkDownloadProgress,
                                                   errorDescription);
}

bool SynchronizationManagerSignalsCatcher::checkLinkedNotebookSyncChunkDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    for(auto it = m_linkedNotebookSyncChunkDownloadProgress.constBegin(),
        end = m_linkedNotebookSyncChunkDownloadProgress.constEnd();
        it != end; ++it)
    {
        if (!checkSyncChunkDownloadProgressOrderImpl(it.value(),
                                                     errorDescription))
        {
            return false;
        }
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkNoteDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkNoteDownloadProgressOrderImpl(m_noteDownloadProgress,
                                              errorDescription);
}

bool SynchronizationManagerSignalsCatcher::checkLinkedNotebookNoteDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkNoteDownloadProgressOrderImpl(m_linkedNotebookNoteDownloadProgress,
                                              errorDescription);
}

bool SynchronizationManagerSignalsCatcher::checkResourceDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkResourceDownloadProgressOrderImpl(m_resourceDownloadProgress,
                                                  errorDescription);
}

bool SynchronizationManagerSignalsCatcher::checkLinkedNotebookResourceDownloadProgressOrder(
    ErrorString & errorDescription) const
{
    return checkResourceDownloadProgressOrderImpl(m_linkedNotebookResourceDownloadProgress,
                                                  errorDescription);
}

void SynchronizationManagerSignalsCatcher::onStart()
{
    m_receivedStartedSignal = true;
}

void SynchronizationManagerSignalsCatcher::onStop()
{
    m_receivedStoppedSignal = true;
}

void SynchronizationManagerSignalsCatcher::onFailure(ErrorString errorDescription)
{
    m_receivedFailedSignal = true;
    m_failureErrorDescription = errorDescription;

    SynchronizationManager * pSyncManager =
        qobject_cast<SynchronizationManager*>(sender());
    if (pSyncManager) {
        pSyncManager->stop();
    }

    Q_EMIT ready();
}

void SynchronizationManagerSignalsCatcher::onFinish(Account account,
                                                    bool somethingDownloaded,
                                                    bool somethingSent)
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

void SynchronizationManagerSignalsCatcher::onWillRepeatRemoteToLocalSyncAfterSendingLocalChanges()
{
    m_receivedWillRepeatRemoteToLocalSyncAfterSendingChanges = true;
}

void SynchronizationManagerSignalsCatcher::onDetectedConflictDuringLocalChangesSending()
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

void SynchronizationManagerSignalsCatcher::onLinkedNotebookSyncChunksDownloaded()
{
    m_receivedLinkedNotebookSyncChunksDownloaded = true;
}

void SynchronizationManagerSignalsCatcher::onSyncChunkDownloadProgress(
    qint32 highestDownloadedUsn, qint32 highestServerUsn, qint32 lastPreviousUsn)
{
    QNDEBUG("SynchronizationManagerSignalsCatcher::"
            << "onSyncChunkDownloadProgress: highest downloaded USN = "
            << highestDownloadedUsn << ", highest server USN = "
            << highestServerUsn << ", last previous USN = "
            << lastPreviousUsn);

    SyncChunkDownloadProgress progress;
    progress.m_highestDownloadedUsn = highestDownloadedUsn;
    progress.m_highestServerUsn = highestServerUsn;
    progress.m_lastPreviousUsn = lastPreviousUsn;

    m_syncChunkDownloadProgress << progress;
}

void SynchronizationManagerSignalsCatcher::onLinkedNotebookSyncChunkDownloadProgress(
    qint32 highestDownloadedUsn, qint32 highestServerUsn,
    qint32 lastPreviousUsn, LinkedNotebook linkedNotebook)
{
    QNDEBUG("SynchronizationManagerSignalsCatcher::"
            << "onLinkedNotebookSyncChunkDownloadProgress: "
            << "highest downloaded USN = "
            << highestDownloadedUsn << ", highest server USN = "
            << highestServerUsn << ", last previous USN = "
            << lastPreviousUsn << ", linked notebook: "
            << linkedNotebook);

    QVERIFY2(linkedNotebook.hasGuid(),
             "Detected sync chunk download progress "
             "for a linked notebook without guid");

    SyncChunkDownloadProgress progress;
    progress.m_highestDownloadedUsn = highestDownloadedUsn;
    progress.m_highestServerUsn = highestServerUsn;
    progress.m_lastPreviousUsn = lastPreviousUsn;

    m_linkedNotebookSyncChunkDownloadProgress[linkedNotebook.guid()] << progress;
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

void SynchronizationManagerSignalsCatcher::onLinkedNotebookResourceDownloadProgress(
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

void SynchronizationManagerSignalsCatcher::onPreparedLinkedNotebookDirtyObjectsForSending()
{
    m_receivedPreparedLinkedNotebookDirtyObjectsForSending = true;
}

void SynchronizationManagerSignalsCatcher::onSyncStatePersisted(
    Account account, qint32 userOwnDataUpdateCount,
    qevercloud::Timestamp userOwnDataSyncTime,
    QHash<QString,qint32> linkedNotebookUpdateCountsByLinkedNotebookGuid,
    QHash<QString,qevercloud::Timestamp> linkedNotebookSyncTimesByLinkedNotebookGuid)
{
    Q_UNUSED(account)
    Q_UNUSED(userOwnDataSyncTime)
    Q_UNUSED(linkedNotebookSyncTimesByLinkedNotebookGuid)

    PersistedSyncStateUpdateCounts data;
    data.m_userOwnUpdateCount = userOwnDataUpdateCount;
    data.m_linkedNotebookUpdateCountsByLinkedNotebookGuid =
        linkedNotebookUpdateCountsByLinkedNotebookGuid;
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
    SyncStatePersistenceManager & syncStatePersistenceManager)
{
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              noteMovedToAnotherNotebook,
                              QString,QString,QString),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onNoteMovedToAnotherNotebook,
                            QString,QString,QString));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,noteTagListChanged,
                              QString,QStringList,QStringList),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onNoteTagListChanged,QString,QStringList,QStringList));

    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,started),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,onStart));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,stopped),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,onStop));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,failed,ErrorString),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onFailure,ErrorString));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,finished,Account,bool,bool),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onFinish,Account,bool,bool));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,authenticationRevoked,
                              bool,ErrorString,qevercloud::UserID),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onAuthenticationRevoked,
                            bool,ErrorString,qevercloud::UserID));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,authenticationFinished,
                              bool,ErrorString,Account),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onAuthenticationFinished,bool,ErrorString,Account));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,remoteToLocalSyncStopped),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onRemoteToLocalSyncStopped));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,sendLocalChangesStopped),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onSendLocalChangesStopped));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,
                              willRepeatRemoteToLocalSyncAfterSendingChanges),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onWillRepeatRemoteToLocalSyncAfterSendingLocalChanges));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,
                              detectedConflictDuringLocalChangesSending),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onDetectedConflictDuringLocalChangesSending));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,rateLimitExceeded,qint32),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onRateLimitExceeded,qint32));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,remoteToLocalSyncDone,bool),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onRemoteToLocalSyncDone,bool));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,syncChunksDownloaded),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onSyncChunksDownloaded));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,
                              linkedNotebooksSyncChunksDownloaded),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onLinkedNotebookSyncChunksDownloaded));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,syncChunksDownloadProgress,
                              qint32,qint32,qint32),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onSyncChunkDownloadProgress,qint32,qint32,qint32));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,
                              linkedNotebookSyncChunksDownloadProgress,
                              qint32,qint32,qint32,LinkedNotebook),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onLinkedNotebookSyncChunkDownloadProgress,
                            qint32,qint32,qint32,LinkedNotebook));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,notesDownloadProgress,
                              quint32,quint32),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onNoteDownloadProgress,quint32,quint32));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,
                              linkedNotebooksNotesDownloadProgress,
                              quint32,quint32),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onLinkedNotebookNoteDownloadProgress,
                            quint32,quint32));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,resourcesDownloadProgress,
                              quint32,quint32),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onResourceDownloadProgress,quint32,quint32));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,
                              preparedDirtyObjectsForSending),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onPreparedDirtyObjectsForSending));
    QObject::connect(&synchronizationManager,
                     QNSIGNAL(SynchronizationManager,
                              preparedLinkedNotebooksDirtyObjectsForSending),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onPreparedLinkedNotebookDirtyObjectsForSending));

    QObject::connect(&syncStatePersistenceManager,
                     QNSIGNAL(SyncStatePersistenceManager,
                              notifyPersistentSyncStateUpdated,
                              Account,qint32,qevercloud::Timestamp,
                              QHash<QString,qint32>,
                              QHash<QString,qevercloud::Timestamp>),
                     this,
                     QNSLOT(SynchronizationManagerSignalsCatcher,
                            onSyncStatePersisted,
                            Account,qint32,qevercloud::Timestamp,
                            QHash<QString,qint32>,
                            QHash<QString,qevercloud::Timestamp>));
}

bool SynchronizationManagerSignalsCatcher::checkSyncChunkDownloadProgressOrderImpl(
    const QVector<SyncChunkDownloadProgress> & syncChunkDownloadProgress,
    ErrorString & errorDescription) const
{
    if (syncChunkDownloadProgress.isEmpty()) {
        return true;
    }

    if (syncChunkDownloadProgress.size() == 1) {
        return checkSingleSyncChunkDownloadProgress(syncChunkDownloadProgress[0],
                                                    errorDescription);
    }

    for(int i = 1, size = syncChunkDownloadProgress.size(); i < size; ++i)
    {
        const SyncChunkDownloadProgress & currentProgress =
            syncChunkDownloadProgress[i];
        if (!checkSingleSyncChunkDownloadProgress(currentProgress,
                                                  errorDescription))
        {
            return false;
        }

        const SyncChunkDownloadProgress & previousProgress =
            syncChunkDownloadProgress[i-1];
        if ((i == 1) &&
            !checkSingleSyncChunkDownloadProgress(previousProgress,
                                                  errorDescription))
        {
            return false;
        }

        if (previousProgress.m_highestDownloadedUsn >=
            currentProgress.m_highestDownloadedUsn)
        {
            errorDescription.setBase(QStringLiteral("Found decreasing highest "
                                                    "downloaded USN"));
            return false;
        }

        if (previousProgress.m_highestServerUsn !=
            currentProgress.m_highestServerUsn)
        {
            errorDescription.setBase(QStringLiteral("Highest server USN changed "
                                                    "between two sync chunk "
                                                    "download progresses"));
            return false;
        }

        if (previousProgress.m_lastPreviousUsn !=
            currentProgress.m_lastPreviousUsn)
        {
            errorDescription.setBase(QStringLiteral("Last previous USN changed "
                                                    "between two sync chunk "
                                                    "download progresses"));
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
        errorDescription.setBase(QStringLiteral("Detected highest downloaded USN "
                                                "greater than highest server USN"));
        return false;
    }

    if (progress.m_lastPreviousUsn > progress.m_highestDownloadedUsn) {
        errorDescription.setBase(QStringLiteral("Detected last previous USN "
                                                "greater than highest "
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
        return checkSingleNoteDownloadProgress(noteDownloadProgress[0],
                                               errorDescription);
    }

    for(int i = 1, size = noteDownloadProgress.size(); i < size; ++i)
    {
        const NoteDownloadProgress & currentProgress = noteDownloadProgress[i];
        if (!checkSingleNoteDownloadProgress(currentProgress, errorDescription)) {
            return false;
        }

        const NoteDownloadProgress & previousProgress = noteDownloadProgress[i-1];
        if ((i == 1) &&
            !checkSingleNoteDownloadProgress(previousProgress, errorDescription))
        {
            return false;
        }

        if (previousProgress.m_notesDownloaded >=
            currentProgress.m_notesDownloaded)
        {
            errorDescription.setBase(QStringLiteral("Found non-increasing "
                                                    "downloaded notes count"));
            return false;
        }

        if (previousProgress.m_totalNotesToDownload !=
            currentProgress.m_totalNotesToDownload)
        {
            errorDescription.setBase(QStringLiteral("The total number of notes "
                                                    "to download has changed "
                                                    "between two progresses"));
            return false;
        }
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkSingleNoteDownloadProgress(
    const NoteDownloadProgress & progress,
    ErrorString & errorDescription) const
{
    if (progress.m_notesDownloaded > progress.m_totalNotesToDownload)
    {
        errorDescription.setBase(QStringLiteral("The number of downloaded notes "
                                                "is greater than the total number "
                                                "of notes to download"));
        return false;
    }

    return true;
}

bool SynchronizationManagerSignalsCatcher::checkResourceDownloadProgressOrderImpl(
    const QVector<ResourceDownloadProgress> & resourceDownloadProgress,
    ErrorString & errorDescription) const
{
    if (resourceDownloadProgress.isEmpty()) {
        return true;
    }

    if (resourceDownloadProgress.size() == 1) {
        return checkSingleResourceDownloadProgress(resourceDownloadProgress[0],
                                                   errorDescription);
    }

    for(int i = 1, size = resourceDownloadProgress.size(); i < size; ++i)
    {
        const ResourceDownloadProgress & currentProgress =
            resourceDownloadProgress[i];
        if (!checkSingleResourceDownloadProgress(currentProgress,
                                                 errorDescription))
        {
            return false;
        }

        const ResourceDownloadProgress & previousProgress =
            resourceDownloadProgress[i-1];
        if ((i == 1) &&
            !checkSingleResourceDownloadProgress(previousProgress, errorDescription))
        {
            return false;
        }

        if (previousProgress.m_resourcesDownloaded >=
            currentProgress.m_resourcesDownloaded)
        {
            errorDescription.setBase(QStringLiteral("Found non-increasing "
                                                    "downloaded resources count"));
            return false;
        }

        if (previousProgress.m_totalResourcesToDownload !=
            currentProgress.m_totalResourcesToDownload)
        {
            errorDescription.setBase(QStringLiteral("The total number of resources "
                                                    "to download has changed "
                                                    "between two progresses"));
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
        errorDescription.setBase(QStringLiteral("The number of downloaded "
                                                "resources is greater than "
                                                "the total number of resources "
                                                "to download"));
        return false;
    }

    return true;
}

} // namespace quentier
