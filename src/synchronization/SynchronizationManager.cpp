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

namespace quentier {

SynchronizationManager::SynchronizationManager(const QString & consumerKey, const QString & consumerSecret,
                                               const QString & host, LocalStorageManagerAsync & localStorageManagerAsync,
                                               IAuthenticationManager & authenticationManager) :
    d_ptr(new SynchronizationManagerPrivate(consumerKey, consumerSecret, host, localStorageManagerAsync, authenticationManager))
{
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyStart),
                     this, QNSIGNAL(SynchronizationManager,started));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyStop),
                     this, QNSIGNAL(SynchronizationManager,stopped));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyError,ErrorString),
                     this, QNSIGNAL(SynchronizationManager,failed,ErrorString));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyFinish,Account),
                     this, QNSIGNAL(SynchronizationManager,finished,Account));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,syncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManager,syncChunksDownloaded));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksSyncChunksDownloaded),
                     this, QNSIGNAL(SynchronizationManager,linkedNotebooksSyncChunksDownloaded));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManager,notesDownloadProgress,quint32,quint32));
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,linkedNotebooksNotesDownloadProgress,quint32,quint32),
                     this, QNSIGNAL(SynchronizationManager,linkedNotebooksNotesDownloadProgress,quint32,quint32));
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
    QObject::connect(d_ptr, QNSIGNAL(SynchronizationManagerPrivate,notifyRemoteToLocalSyncDone),
                     this, QNSIGNAL(SynchronizationManager,remoteToLocalSyncDone));
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

    emit setAccountDone(account);
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

    emit setDownloadNoteThumbnailsDone(flag);
}

void SynchronizationManager::setDownloadInkNoteImages(bool flag)
{
    Q_D(SynchronizationManager);
    d->setDownloadInkNoteImages(flag);

    emit setDownloadInkNoteImagesDone(flag);
}

void SynchronizationManager::setInkNoteImagesStoragePath(QString path)
{
    Q_D(SynchronizationManager);
    d->setInkNoteImagesStoragePath(path);

    emit setInkNoteImagesStoragePathDone(path);
}

} // namespace quentier
