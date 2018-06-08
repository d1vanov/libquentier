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

#include <quentier/synchronization/SynchronizationManager.h>
#include "SynchronizationManager_p.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier_private/synchronization/SynchronizationManagerDependencyInjector.h>

namespace quentier {

SynchronizationManager::SynchronizationManager(const QString & host, LocalStorageManagerAsync & localStorageManagerAsync,
                                               IAuthenticationManager & authenticationManager,
                                               SynchronizationManagerDependencyInjector * pInjector) :
    d_ptr(new SynchronizationManagerPrivate(host, localStorageManagerAsync, authenticationManager, pInjector))
{
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyStart),
                     this, QNSIGNAL(SynchronizationManager,started));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyStop),
                     this, QNSIGNAL(SynchronizationManager,stopped));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyError,ErrorString),
                     this, QNSIGNAL(SynchronizationManager,failed,ErrorString));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyFinish,Account,bool,bool),
                     this, QNSIGNAL(SynchronizationManager,finished,Account,bool,bool));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,syncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManager,syncChunksDownloaded));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,syncChunksDownloadProgress,qint32,qint32,qint32),
                     this, QNSIGNAL(SynchronizationManager,syncChunksDownloadProgress,qint32,qint32,qint32));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebookSyncChunksDownloadProgress,qint32,qint32,qint32,LinkedNotebook),
                     this, QNSIGNAL(SynchronizationManager,linkedNotebookSyncChunksDownloadProgress,qint32,qint32,qint32,LinkedNotebook));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksSyncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManager,linkedNotebooksSyncChunksDownloaded));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManager,notesDownloadProgress,quint32,quint32));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksNotesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManager,linkedNotebooksNotesDownloadProgress,quint32,quint32));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,resourcesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManager,resourcesDownloadProgress,quint32,quint32));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksResourcesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManager,linkedNotebooksResourcesDownloadProgress,quint32,quint32));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,preparedDirtyObjectsForSending),
                     this, QNSIGNAL(SynchronizationManager,preparedDirtyObjectsForSending));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,preparedLinkedNotebooksDirtyObjectsForSending),
                     this, QNSIGNAL(SynchronizationManager,preparedLinkedNotebooksDirtyObjectsForSending));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,authenticationFinished,bool,ErrorString,Account),
                     this, QNSIGNAL(SynchronizationManager,authenticationFinished,bool,ErrorString,Account));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,authenticationRevoked,bool,ErrorString,qevercloud::UserID),
                     this, QNSIGNAL(SynchronizationManager,authenticationRevoked,bool,ErrorString,qevercloud::UserID));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,remoteToLocalSyncStopped),
                     this, QNSIGNAL(SynchronizationManager,remoteToLocalSyncStopped));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,sendLocalChangesStopped),
                     this, QNSIGNAL(SynchronizationManager,sendLocalChangesStopped));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,willRepeatRemoteToLocalSyncAfterSendingChanges),
                     this, QNSIGNAL(SynchronizationManager,willRepeatRemoteToLocalSyncAfterSendingChanges));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,detectedConflictDuringLocalChangesSending),
                     this, QNSIGNAL(SynchronizationManager,detectedConflictDuringLocalChangesSending));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,rateLimitExceeded,qint32),
                     this, QNSIGNAL(SynchronizationManager,rateLimitExceeded,qint32));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyRemoteToLocalSyncDone,bool),
                     this, QNSIGNAL(SynchronizationManager,remoteToLocalSyncDone,bool));
}

SynchronizationManager::~SynchronizationManager()
{
    delete d_ptr;
}

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

void SynchronizationManager::revokeAuthentication(const qevercloud::UserID userId)
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
