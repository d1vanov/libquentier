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

namespace quentier::synchronization {

SyncEventsNotifier::SyncEventsNotifier(QObject * parent) :
    ISyncEventsNotifier(parent)
{}

void SyncEventsNotifier::notifySyncChunksDownloadProgress(
    qint32 highestDownloadedUsn, qint32 highestServerUsn,
    qint32 lastPreviousUsn)
{
    Q_EMIT syncChunksDownloadProgress(
        highestDownloadedUsn, highestServerUsn, lastPreviousUsn);
}

void SyncEventsNotifier::notifySyncChunksDownloaded()
{
    Q_EMIT syncChunksDownloaded();
}

void SyncEventsNotifier::notifySyncChunksDataProcessingProgress(
    ISyncChunksDataCountersPtr counters)
{
    Q_EMIT syncChunksDataProcessingProgress(std::move(counters));
}

void SyncEventsNotifier::notifyStartLinkedNotebooksDataDownloading(
    const QList<qevercloud::LinkedNotebook> & linkedNotebooks)
{
    Q_EMIT startLinkedNotebooksDataDownloading(linkedNotebooks);
}

void SyncEventsNotifier::notifyLinkedNotebookSyncChunksDownloadProgress(
    qint32 highestDownloadedUsn, qint32 highestServerUsn,
    qint32 lastPreviousUsn, const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_EMIT linkedNotebookSyncChunksDownloadProgress(
        highestDownloadedUsn, highestServerUsn, lastPreviousUsn,
        linkedNotebook);
}

void SyncEventsNotifier::notifyLinkedNotebookSyncChunksDownloaded(
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_EMIT linkedNotebookSyncChunksDownloaded(linkedNotebook);
}

void SyncEventsNotifier::notifyLinkedNotebookSyncChunksDataProcessingProgress(
    ISyncChunksDataCountersPtr counters,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_EMIT linkedNotebookSyncChunksDataProcessingProgress(
        std::move(counters), linkedNotebook);
}

void SyncEventsNotifier::notifyNotesDownloadProgress(
    quint32 notesDownloaded, quint32 totalNotesToDownload)
{
    Q_EMIT notesDownloadProgress(notesDownloaded, totalNotesToDownload);
}

void SyncEventsNotifier::notifyLinkedNotebookNotesDownloadProgress(
    quint32 notesDownloaded, quint32 totalNotesToDownload,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_EMIT linkedNotebookNotesDownloadProgress(
        notesDownloaded, totalNotesToDownload, linkedNotebook);
}

void SyncEventsNotifier::notifyResourcesDownloadProgress(
    quint32 resourcesDownloaded, quint32 totalResourcesToDownload)
{
    Q_EMIT resourcesDownloadProgress(
        resourcesDownloaded, totalResourcesToDownload);
}

void SyncEventsNotifier::notifyLinkedNotebookResourcesDownloadProgress(
    quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
    const qevercloud::LinkedNotebook & linkedNotebook)
{
    Q_EMIT linkedNotebookResourcesDownloadProgress(
        resourcesDownloaded, totalResourcesToDownload, linkedNotebook);
}

void SyncEventsNotifier::notifyUserOwnSendStatusUpdate(
    ISendStatusPtr sendStatus)
{
    Q_EMIT userOwnSendStatusUpdate(std::move(sendStatus));
}

void SyncEventsNotifier::notifyLinkedNotebookSendStatusUpdate(
    const qevercloud::Guid & linkedNotebookGuid, ISendStatusPtr sendStatus)
{
    Q_EMIT linkedNotebookSendStatusUpdate(
        linkedNotebookGuid, std::move(sendStatus));
}

} // namespace quentier::synchronization
