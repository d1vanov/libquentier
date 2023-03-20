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

#include "Fwd.h"

#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/threading/Fwd.h>

#include <qevercloud/types/LinkedNotebook.h>

#include <QtGlobal>

namespace quentier::synchronization {

/**
 * @brief The SyncEventsNotifierProxy class proxies notifications to the
 * internally created and managed object of SyncEventsNotifier class.
 *
 * The purpose behind the proxy object is to ensure methods of
 * SyncEventsNotifier are always called from the same thread. It also allows to
 * manage the lifetime of SyncEventsNotifier object more properly:
 * SyncEventsNotifier is guaranteed to be alive at least for as long as
 * SyncEventsNotifierProxy.
 */
class SyncEventsNotifierProxy
{
public:
    explicit SyncEventsNotifierProxy(threading::QThreadPtr notifierThread);
    ~SyncEventsNotifierProxy();

    // for subscription to signals
    [[nodiscard]] ISyncEventsNotifier * notifier() const noexcept;

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

    void notifyUserOwnSendStatusUpdate(ISendStatusPtr sendStatus);

    void notifyLinkedNotebookSendStatusUpdate(
        const qevercloud::Guid & linkedNotebookGuid, ISendStatusPtr sendStatus);

private:
    const threading::QThreadPtr m_thread;
    SyncEventsNotifier * m_notifier = nullptr;
};

} // namespace quentier::synchronization
