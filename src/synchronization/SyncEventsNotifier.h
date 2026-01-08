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

#pragma once

#include <quentier/synchronization/ISyncEventsNotifier.h>

namespace quentier::synchronization {

class SyncEventsNotifier final : public ISyncEventsNotifier
{
    Q_OBJECT
public:
    explicit SyncEventsNotifier(QObject * parent = nullptr);
    ~SyncEventsNotifier() override = default;

    void notifySyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn);

    void notifySyncChunksDownloaded();

    void notifySyncChunksDataProcessingProgress(
        ISyncChunksDataCountersPtr counters);

    void notifyStartLinkedNotebooksDataDownloading(
        const QList<qevercloud::LinkedNotebook> & linkedNotebooks);

    void notifyLinkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void notifyLinkedNotebookSyncChunksDownloaded(
        const qevercloud::LinkedNotebook & linkedNotebook);

    void notifyLinkedNotebookSyncChunksDataProcessingProgress(
        ISyncChunksDataCountersPtr counters,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void notifyNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    void notifyLinkedNotebookNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void notifyResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    void notifyLinkedNotebookResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook);

    void notifyDownloadFinished(bool dataDownloaded);

    void notifyUserOwnSendStatusUpdate(ISendStatusPtr sendStatus);

    void notifyLinkedNotebookSendStatusUpdate(
        const qevercloud::Guid & linkedNotebookGuid, ISendStatusPtr sendStatus);
};

} // namespace quentier::synchronization
