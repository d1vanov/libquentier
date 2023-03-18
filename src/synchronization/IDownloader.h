/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/cancelers/Fwd.h>

#include <qevercloud/types/LinkedNotebook.h>

#include <QFuture>
#include <QHash>

#include <memory>

namespace quentier::synchronization {

class IDownloader
{
public:
    virtual ~IDownloader() = default;

    class ICallback
    {
    public:
        virtual ~ICallback() = default;

        /**
        * This method is called during user own account's sync chunks
        * downloading and denotes the progress of that step. The percentage of
        * completeness can be computed roughly as
        * (highestDownloadedUsn - lastPreviousUsn) /
        * (highestServerUsn - lastPreviousUsn) * 100%.
        *
        * @param highestDownloadedUsn      The highest update sequence number
        *                                  within data items from sync chunks
        *                                  downloaded so far
        * @param highestServerUsn          The current highest update sequence
        *                                  number within the account
        * @param lastPreviousUsn           The last update sequence number from
        *                                  previous sync; if current sync is
        *                                  the first one, this value is zero
        */
        virtual void onSyncChunksDownloadProgress(
            qint32 highestDownloadedUsn, qint32 highestServerUsn,
            qint32 lastPreviousUsn) = 0;

        /**
        * This method is called when the sync chunks for data from user's
        * own account are downloaded during the download synchronization step.
        */
        virtual void onSyncChunksDownloaded() = 0;

        /**
        * This method is called during user own account's downloaded sync chunks
        * contents processing and denotes the progress on that step.
        */
        virtual void onSyncChunksDataProcessingProgress(
            ISyncChunksDataCountersPtr counters) = 0;

        /**
         * This method is called before the downloading of data corresponding
         * to linked notebooks starts.
         * @param linkedNotebooks           Linked notebooks the data from which
         *                                  will start being downloaded after
         *                                  the execution of this callback
         */
        virtual void onStartLinkedNotebooksDataDownloading(
            const QList<qevercloud::LinkedNotebook> & linkedNotebooks) = 0;

        /**
        * This method is called during linked notebooks sync chunks downloading
        * and denotes the progress of that step, individually for each linked
        * notebook. The percentage of completeness can be computed roughly as
        * (highestDownloadedUsn - lastPreviousUsn) /
        * (highestServerUsn - lastPreviousUsn) * 100%.
        * @param highestDownloadedUsn      The highest update sequence number
        *                                  within data items from linked
        *                                  notebook sync chunks downloaded so
        *                                  far
        * @param highestServerUsn          The current highest update sequence
        *                                  number within the linked notebook
        * @param lastPreviousUsn           The last update sequence number from
        *                                  previous sync of the given linked
        *                                  notebook; if current sync is the
        *                                  first one, this value is zero
        * @param linkedNotebook            The linked notebook which sync chunks
        *                                  download progress is reported
        */
        virtual void onLinkedNotebookSyncChunksDownloadProgress(
            qint32 highestDownloadedUsn, qint32 highestServerUsn,
            qint32 lastPreviousUsn,
            const qevercloud::LinkedNotebook & linkedNotebook) = 0;

        /**
        * This method is called when the sync chunks for data from some linked
        * notebook are downloaded during "remote to local" synchronization step
        * @param linkedNotebook             The linked notebook which sync
        *                                   chunks were downloaded
        */
        virtual void onLinkedNotebookSyncChunksDownloaded(
            const qevercloud::LinkedNotebook & linkedNotebook) = 0;

        /**
        * This method is called during some linked notebook's downloaded sync
        * chunks contents processing and denotes the progress on that step.
        * @param counters                   Updated sync chunks data counters
        * @param linkedNotebook             The linked notebook which sync
        *                                   chunks data processing progress
        *                                   is being reported
        */
        virtual void onLinkedNotebookSyncChunksDataProcessingProgress(
            ISyncChunksDataCountersPtr counters,
            const qevercloud::LinkedNotebook & linkedNotebook) = 0;

        /**
        * This method is called on each successful download of full note data
        * from user's own account.
        *
        * @param notesDownloaded       The number of notes downloaded by the
        *                              moment
        * @param totalNotesToDownload  The total number of notes that need to be
        *                              downloaded
        */
        virtual void onNotesDownloadProgress(
            quint32 notesDownloaded, quint32 totalNotesToDownload) = 0;

        /**
        * This method is called on each successful download of full note data
        * from some linked notebook.
        * @param notesDownloaded       The number of notes downloaded by the
        *                              moment
        * @param totalNotesToDownload  The total number of notes that need to be
        *                              downloaded
        * @param linkedNotebook        The linked notebook which notes download
        *                              progress is being reported
        */
        virtual void onLinkedNotebookNotesDownloadProgress(
            quint32 notesDownloaded, quint32 totalNotesToDownload,
            const qevercloud::LinkedNotebook & linkedNotebook) = 0;

        /**
        * This method is called on each successful doenload of full resource
        * data from user's own account during the incremental sync (as
        * individual resources are downloaded along with their notes during full
        * sync).
        * @param resourcesDownloaded       The number of resources downloaded
        *                                  by the moment
        * @param totalResourcesToDownload  The total number of resources that
        *                                  need to be downloaded
        */
        virtual void onResourcesDownloadProgress(
            quint32 resourcesDownloaded, quint32 totalResourcesToDownload) = 0;

        /**
        * This method is called on each successful download of full resource
        * data from linked notebooks during the incremental sync (as individual
        * resources are downloaded along with their notes during full sync).
        * @param resourcesDownloaded       The number of resources downloaded
        *                                  by the moment
        * @param totalResourcesToDownload  The total number of resources that
        *                                  need to be downloaded
        * @param linkedNotebook            The linked notebook which resources
        *                                  download progress is being reported
        */
        virtual void onLinkedNotebookResourcesDownloadProgress(
            quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
            const qevercloud::LinkedNotebook & linkedNotebook) = 0;
    };

    using ICallbackWeakPtr = std::weak_ptr<ICallback>;

    struct LocalResult
    {
        ISyncChunksDataCountersPtr syncChunksDataCounters;
        IDownloadNotesStatusPtr downloadNotesStatus;
        IDownloadResourcesStatusPtr downloadResourcesStatus;
    };

    struct Result
    {
        // Result for user own account
        LocalResult userOwnResult;

        // Results for linked notebooks
        QHash<qevercloud::Guid, LocalResult> linkedNotebookResults;
    };

    [[nodiscard]] virtual QFuture<Result> download(
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) = 0;
};

} // namespace quentier::synchronization
