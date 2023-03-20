/*
 * Copyright 2023 Dmitry Ivanov
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

#include "SyncEventsNotifier.h"
#include "SyncEventsNotifierProxy.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Post.h>

#include <QThread>

namespace quentier::synchronization {

SyncEventsNotifierProxy::SyncEventsNotifierProxy(threading::QThreadPtr thread) :
    m_thread{std::move(thread)}
{
    if (Q_UNLIKELY(!m_thread)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("SyncEventsNotifierProxy ctor: thread is null")}};
    }

    m_notifier = new SyncEventsNotifier(m_thread.get());
}

SyncEventsNotifierProxy::~SyncEventsNotifierProxy()
{
    if (m_notifier) {
        m_notifier->disconnect();
        m_notifier->deleteLater();
    }
}

ISyncEventsNotifier * SyncEventsNotifierProxy::notifier() const noexcept
{
    return m_notifier;
}

void SyncEventsNotifierProxy::notifySyncChunksDownloadProgress(
    qint32 highestDownloadedUsn, qint32 highestServerUsn,
    qint32 lastPreviousUsn)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifySyncChunksDownloadProgress(
            highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, highestDownloadedUsn, highestServerUsn,
         lastPreviousUsn] {
            notifier->notifySyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
        });
}

void SyncEventsNotifierProxy::notifySyncChunksDownloaded()
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifySyncChunksDownloaded();
        return;
    }

    threading::postToObject(m_notifier, [notifier = m_notifier] {
        notifier->notifySyncChunksDownloaded();
    });
}

void SyncEventsNotifierProxy::notifySyncChunksDataProcessingProgress(
    ISyncChunksDataCountersPtr counters)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifySyncChunksDataProcessingProgress(std::move(counters));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, counters = std::move(counters)]() mutable {
            notifier->notifySyncChunksDataProcessingProgress(
                std::move(counters));
        });
}

void SyncEventsNotifierProxy::notifyStartLinkedNotebooksDataDownloading(
    const QList<qevercloud::LinkedNotebook> & linkedNotebooks)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyStartLinkedNotebooksDataDownloading(linkedNotebooks);
        return;
    }

    threading::postToObject(
        m_notifier, [notifier = m_notifier, linkedNotebooks] {
            notifier->notifyStartLinkedNotebooksDataDownloading(
                linkedNotebooks);
        });
}

void SyncEventsNotifierProxy::notifyLinkedNotebookSyncChunksDownloadProgress(
    qint32 highestDownloadedUsn, qint32 highestServerUsn,
    qint32 lastPreviousUsn, const qevercloud::LinkedNotebook & linkedNotebook)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyLinkedNotebookSyncChunksDownloadProgress(
            highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
            linkedNotebook);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, highestDownloadedUsn, highestServerUsn,
         lastPreviousUsn, linkedNotebook] {
            notifier->notifyLinkedNotebookSyncChunksDownloadProgress(
                highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
                linkedNotebook);
        });
}

void SyncEventsNotifierProxy::notifyLinkedNotebookSyncChunksDownloaded(
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyLinkedNotebookSyncChunksDownloaded(linkedNotebook);
        return;
    }

    threading::postToObject(
        m_notifier, [notifier = m_notifier, linkedNotebook] {
            notifier->notifyLinkedNotebookSyncChunksDownloaded(linkedNotebook);
        });
}

void SyncEventsNotifierProxy::
    notifyLinkedNotebookSyncChunksDataProcessingProgress(
        ISyncChunksDataCountersPtr counters,
        const qevercloud::LinkedNotebook & linkedNotebook)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyLinkedNotebookSyncChunksDataProcessingProgress(
            std::move(counters), linkedNotebook);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, counters = std::move(counters),
         linkedNotebook]() mutable {
            notifier->notifyLinkedNotebookSyncChunksDataProcessingProgress(
                std::move(counters), linkedNotebook);
        });
}

void SyncEventsNotifierProxy::notifyNotesDownloadProgress(
    quint32 notesDownloaded, quint32 totalNotesToDownload)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyNotesDownloadProgress(
            notesDownloaded, totalNotesToDownload);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, notesDownloaded, totalNotesToDownload] {
            notifier->notifyNotesDownloadProgress(
                notesDownloaded, totalNotesToDownload);
        });
}

void SyncEventsNotifierProxy::notifyLinkedNotebookNotesDownloadProgress(
    quint32 notesDownloaded, quint32 totalNotesToDownload,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyLinkedNotebookNotesDownloadProgress(
            notesDownloaded, totalNotesToDownload, linkedNotebook);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, notesDownloaded, totalNotesToDownload,
         linkedNotebook] {
            notifier->notifyLinkedNotebookNotesDownloadProgress(
                notesDownloaded, totalNotesToDownload, linkedNotebook);
        });
}

void SyncEventsNotifierProxy::notifyResourcesDownloadProgress(
    quint32 resourcesDownloaded, quint32 totalResourcesToDownload)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyResourcesDownloadProgress(
            resourcesDownloaded, totalResourcesToDownload);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, resourcesDownloaded, totalResourcesToDownload] {
            notifier->notifyResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload);
        });
}

void SyncEventsNotifierProxy::notifyLinkedNotebookResourcesDownloadProgress(
    quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyLinkedNotebookResourcesDownloadProgress(
            resourcesDownloaded, totalResourcesToDownload, linkedNotebook);
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, resourcesDownloaded, totalResourcesToDownload,
         linkedNotebook] {
            notifier->notifyLinkedNotebookResourcesDownloadProgress(
                resourcesDownloaded, totalResourcesToDownload, linkedNotebook);
        });
}

void SyncEventsNotifierProxy::notifyUserOwnSendStatusUpdate(
    ISendStatusPtr sendStatus)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyUserOwnSendStatusUpdate(std::move(sendStatus));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, sendStatus = std::move(sendStatus)]() mutable {
            notifier->notifyUserOwnSendStatusUpdate(std::move(sendStatus));
        });
}

void SyncEventsNotifierProxy::notifyLinkedNotebookSendStatusUpdate(
    const qevercloud::Guid & linkedNotebookGuid, ISendStatusPtr sendStatus)
{
    if (QThread::currentThread() == m_thread.get()) {
        m_notifier->notifyLinkedNotebookSendStatusUpdate(
            linkedNotebookGuid, std::move(sendStatus));
        return;
    }

    threading::postToObject(
        m_notifier,
        [notifier = m_notifier, linkedNotebookGuid,
         sendStatus = std::move(sendStatus)]() mutable {
            notifier->notifyLinkedNotebookSendStatusUpdate(
                linkedNotebookGuid, std::move(sendStatus));
        });
}

} // namespace quentier::synchronization
