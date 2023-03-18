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

#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <qevercloud/types/LinkedNotebook.h>

#include <QList>
#include <QObject>

namespace quentier::synchronization {

class QUENTIER_EXPORT ISyncEventsNotifier : public QObject
{
    Q_OBJECT
protected:
    explicit ISyncEventsNotifier(QObject * parent = nullptr);

public:
    ~ISyncEventsNotifier() override;

Q_SIGNALS:
    /**
    * This signal is emitted during user own account's sync chunks
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
    void syncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn);

    /**
    * This signal is emitted when the sync chunks for data from user's
    * own account are downloaded during the download synchronization step.
    */
    void syncChunksDownloaded();

    /**
    * This signal is emitted during user own account's downloaded sync chunks
    * contents processing and denotes the progress on that step.
    */
    void syncChunksDataProcessingProgress(
        ISyncChunksDataCountersPtr counters);

    /**
    * This signal is emitted before the downloading of data corresponding
    * to linked notebooks starts.
    * @param linkedNotebooks           Linked notebooks the data from which
    *                                  will start being downloaded after
    *                                  the execution of this callback
    */
    void startLinkedNotebooksDataDownloading(
        const QList<qevercloud::LinkedNotebook> & linkedNotebooks);

    /**
    * This signal is emitted during linked notebooks sync chunks downloading
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
    void linkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn,
        const qevercloud::LinkedNotebook & linkedNotebook);

    /**
    * This signal is emitted when the sync chunks for data from some linked
    * notebook are downloaded during "remote to local" synchronization step
    * @param linkedNotebook             The linked notebook which sync
    *                                   chunks were downloaded
    */
    void linkedNotebookSyncChunksDownloaded(
        const qevercloud::LinkedNotebook & linkedNotebook);

    /**
    * This signal is emitted during some linked notebook's downloaded sync
    * chunks contents processing and denotes the progress on that step.
    * @param counters                   Updated sync chunks data counters
    * @param linkedNotebook             The linked notebook which sync
    *                                   chunks data processing progress
    *                                   is being reported
    */
    void linkedNotebookSyncChunksDataProcessingProgress(
        ISyncChunksDataCountersPtr counters,
        const qevercloud::LinkedNotebook & linkedNotebook);

    /**
    * This signal is emitted on each successful download of full note data
    * from user's own account.
    *
    * @param notesDownloaded       The number of notes downloaded by the
    *                              moment
    * @param totalNotesToDownload  The total number of notes that need to be
    *                              downloaded
    */
    void notesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    /**
    * This signal is emitted on each successful download of full note data
    * from some linked notebook.
    * @param notesDownloaded       The number of notes downloaded by the
    *                              moment
    * @param totalNotesToDownload  The total number of notes that need to be
    *                              downloaded
    * @param linkedNotebook        The linked notebook which notes download
    *                              progress is being reported
    */
    void linkedNotebookNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook);

    /**
    * This signal is emitted on each successful doenload of full resource
    * data from user's own account during the incremental sync (as
    * individual resources are downloaded along with their notes during full
    * sync).
    * @param resourcesDownloaded       The number of resources downloaded
    *                                  by the moment
    * @param totalResourcesToDownload  The total number of resources that
    *                                  need to be downloaded
    */
    void resourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    /**
    * This signal is emitted on each successful download of full resource
    * data from linked notebooks during the incremental sync (as individual
    * resources are downloaded along with their notes during full sync).
    * @param resourcesDownloaded       The number of resources downloaded
    *                                  by the moment
    * @param totalResourcesToDownload  The total number of resources that
    *                                  need to be downloaded
    * @param linkedNotebook            The linked notebook which resources
    *                                  download progress is being reported
    */
    void linkedNotebookResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload,
        const qevercloud::LinkedNotebook & linkedNotebook);

    /**
     * This signal is emitted on each successful or unsuccessful attempt to
     * send some new or locally modified data item from user's own account to
     * Evernote.
     * @param sendStatus                The updated send status
     */
    void userOwnSendStatusUpdate(ISendStatusPtr sendStatus);

    /**
     * This signal is emitted on each successful or unsuccessful attempt to
     * send some new or locally modified data item from some linked notebook to
     * Evernote.
     * @param linkedNotebookGuid        Guid of the linked notebook for which
     *                                  the send status was updated
     * @param sendStatus                The updated send status
     */
    void linkedNotebookSendStatusUpdate(
        const qevercloud::Guid & linkedNotebookGuid, ISendStatusPtr sendStatus);
};

} // namespace quentier::synchronization
