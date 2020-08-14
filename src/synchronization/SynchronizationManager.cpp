/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include <quentier/synchronization/SynchronizationManager.h>

#include "SynchronizationManager_p.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>

namespace quentier {

SynchronizationManager::SynchronizationManager(
    QString host, LocalStorageManagerAsync & localStorageManagerAsync,
    IAuthenticationManager & authenticationManager, QObject * parent,
    INoteStorePtr pNoteStore, IUserStorePtr pUserStore,
    IKeychainServicePtr pKeychainService,
    ISyncStateStoragePtr pSyncStateStorage) :
    QObject(parent),
    d_ptr(new SynchronizationManagerPrivate(
        std::move(host), localStorageManagerAsync, authenticationManager, this,
        std::move(pNoteStore), std::move(pUserStore),
        std::move(pKeychainService), std::move(pSyncStateStorage)))
{
    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::notifyStart, this,
        &SynchronizationManager::started);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::notifyStop, this,
        &SynchronizationManager::stopped);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::notifyError, this,
        &SynchronizationManager::failed);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::notifyFinish, this,
        &SynchronizationManager::finished);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::syncChunksDownloaded, this,
        &SynchronizationManager::syncChunksDownloaded);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::syncChunksDownloadProgress, this,
        &SynchronizationManager::syncChunksDownloadProgress);

    QObject::connect(
        d_ptr,
        &SynchronizationManagerPrivate::
            linkedNotebookSyncChunksDownloadProgress,
        this,
        &SynchronizationManager::linkedNotebookSyncChunksDownloadProgress);

    QObject::connect(
        d_ptr,
        &SynchronizationManagerPrivate::linkedNotebooksSyncChunksDownloaded,
        this, &SynchronizationManager::linkedNotebooksSyncChunksDownloaded);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::notesDownloadProgress, this,
        &SynchronizationManager::notesDownloadProgress);

    QObject::connect(
        d_ptr,
        &SynchronizationManagerPrivate::linkedNotebooksNotesDownloadProgress,
        this, &SynchronizationManager::linkedNotebooksNotesDownloadProgress);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::resourcesDownloadProgress, this,
        &SynchronizationManager::resourcesDownloadProgress);

    QObject::connect(
        d_ptr,
        &SynchronizationManagerPrivate::
            linkedNotebooksResourcesDownloadProgress,
        this,
        &SynchronizationManager::linkedNotebooksResourcesDownloadProgress);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::preparedDirtyObjectsForSending,
        this, &SynchronizationManager::preparedDirtyObjectsForSending);

    QObject::connect(
        d_ptr,
        &SynchronizationManagerPrivate::
            preparedLinkedNotebooksDirtyObjectsForSending,
        this,
        &SynchronizationManager::preparedLinkedNotebooksDirtyObjectsForSending);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::authenticationFinished, this,
        &SynchronizationManager::authenticationFinished);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::authenticationRevoked, this,
        &SynchronizationManager::authenticationRevoked);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::remoteToLocalSyncStopped, this,
        &SynchronizationManager::remoteToLocalSyncStopped);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::sendLocalChangesStopped, this,
        &SynchronizationManager::sendLocalChangesStopped);

    QObject::connect(
        d_ptr,
        &SynchronizationManagerPrivate::
            willRepeatRemoteToLocalSyncAfterSendingChanges,
        this,
        &SynchronizationManager::
            willRepeatRemoteToLocalSyncAfterSendingChanges);

    QObject::connect(
        d_ptr,
        &SynchronizationManagerPrivate::
            detectedConflictDuringLocalChangesSending,
        this,
        &SynchronizationManager::detectedConflictDuringLocalChangesSending);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::rateLimitExceeded, this,
        &SynchronizationManager::rateLimitExceeded);

    QObject::connect(
        d_ptr, &SynchronizationManagerPrivate::notifyRemoteToLocalSyncDone,
        this, &SynchronizationManager::remoteToLocalSyncDone);
}

SynchronizationManager::~SynchronizationManager() {}

bool SynchronizationManager::active() const
{
    Q_D(const SynchronizationManager);
    return d->active();
}

bool SynchronizationManager::downloadNoteThumbnailsOption() const
{
    Q_D(const SynchronizationManager);
    return d->downloadNoteThumbnailsOption();
}

void SynchronizationManager::setAccount(Account account)
{
    Q_D(SynchronizationManager);
    d->setAccount(account);

    Q_EMIT setAccountDone(account);
}

void SynchronizationManager::authenticate()
{
    Q_D(SynchronizationManager);
    d->authenticate();
}

void SynchronizationManager::authenticateCurrentAccount()
{
    Q_D(SynchronizationManager);
    d->authenticateCurrentAccount();
}

void SynchronizationManager::synchronize()
{
    Q_D(SynchronizationManager);
    d->synchronize();
}

void SynchronizationManager::stop()
{
    Q_D(SynchronizationManager);
    d->stop();
}

void SynchronizationManager::revokeAuthentication(
    const qevercloud::UserID userId)
{
    Q_D(SynchronizationManager);
    d->revokeAuthentication(userId);
}

void SynchronizationManager::setDownloadNoteThumbnails(bool flag)
{
    Q_D(SynchronizationManager);
    d->setDownloadNoteThumbnails(flag);

    Q_EMIT setDownloadNoteThumbnailsDone(flag);
}

void SynchronizationManager::setDownloadInkNoteImages(bool flag)
{
    Q_D(SynchronizationManager);
    d->setDownloadInkNoteImages(flag);

    Q_EMIT setDownloadInkNoteImagesDone(flag);
}

void SynchronizationManager::setInkNoteImagesStoragePath(QString path)
{
    Q_D(SynchronizationManager);
    d->setInkNoteImagesStoragePath(path);

    Q_EMIT setInkNoteImagesStoragePathDone(path);
}

} // namespace quentier
