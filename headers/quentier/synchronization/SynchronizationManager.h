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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_H
#define LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_H

#include <quentier/synchronization/ForwardDeclarations.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/utility/ForwardDeclarations.h>
#include <quentier/utility/Linkage.h>

#include <QObject>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)
QT_FORWARD_DECLARE_CLASS(SynchronizationManagerPrivate)

/**
 * @brief The SynchronizationManager class encapsulates methods and signals &
 * slots required to perform the full or partial synchronization of data with
 * remote Evernote servers. The class also deals with authentication with
 * Evernote service through OAuth.
 */
class QUENTIER_EXPORT SynchronizationManager : public QObject
{
    Q_OBJECT
public:
    /**
     * @param host                      The host to use for the connection with
     *                                  the Evernote service - typically
     *                                  www.evernote.com but could also be
     *                                  sandbox.evernote.com or some other one
     * @param localStorageManagerAsync  Local storage manager
     * @param authenticationManager     Reference to an object implementing
     *                                  IAuthenticationManager interface;
     *                                  SynchronizationManager does not store
     *                                  this reference, it only connects to
     *                                  the object via signals and slots
     *                                  during construction
     * @param pNoteStore                Pointer to an object implementing
     *                                  INoteStore interface; if nullptr,
     *                                  SynchronizatrionManager would create and
     *                                  use its own instance; otherwise
     *                                  SynchronizationManager would set itself
     *                                  as the parent of the passed in object
     * @param pUserStore                Pointer to an object implementing
     *                                  IUserStore interface; if nullptr,
     *                                  SynchronizationManager would create and
     *                                  use its own instance
     * @param pKeychainService          Pointer to an object implementing
     *                                  IKeychainService interface; if nullptr,
     *                                  SynchronizationManager would create and
     *                                  use its own default instance; otherwise
     *                                  SynchronizationManager would set itself
     *                                  as the parent of the passed in object
     * @param pSyncStateStorage         Pointer to an object implementing
     *                                  ISyncStateStorage interface; if nullptr,
     *                                  SynchronizationManager would create and
     *                                  use its own instance; otherwise
     *                                  SynchronizationManager would set itself
     *                                  as the parent of the passed in object
     */
    SynchronizationManager(
        QString host, LocalStorageManagerAsync & localStorageManagerAsync,
        IAuthenticationManager & authenticationManager,
        QObject * parent = nullptr, INoteStorePtr pNoteStore = {},
        IUserStorePtr pUserStore = {},
        IKeychainServicePtr pKeychainService = {},
        ISyncStateStoragePtr pSyncStateStorage = {});

    virtual ~SynchronizationManager();

    /**
     * @return                          True if the synchronization is being
     *                                  performed at the moment, false otherwise
     */
    bool active() const;

    /**
     * @return                          True or false depending on the option to
     *                                  download the thumbnails for notes
     *                                  containing resources during sync; by
     *                                  default no thumbnails are downloaded
     */
    bool downloadNoteThumbnailsOption() const;

public Q_SLOTS:
    /**
     * Use this slot to set the current account for the synchronization manager.
     * If the slot is called during the synchronization running, it would stop,
     * any internal caches belonging to previously selected account (if any)
     * would be purged (but persistent settings like the authentication token
     * saved in the system keychain would remain). Setting the current account
     * won't automatically start the synchronization for it, use synchronize
     * slot for this.
     *
     * The attempt to set the current account of "Local" type would just
     * clean up the synchronization manager as if it was just created
     *
     * After the method finishes its job, setAccountDone signal is emitted
     */
    void setAccount(Account account);

    /**
     * Use this slot to authenticate the new user to do the synchronization with
     * the Evernote service via the client app. The invocation of this slot
     * would respond asynchronously with authenticationFinished signal but won't
     * start the synchronization.
     *
     * Note that this slot would always proceed to the actual OAuth.
     */
    void authenticate();

    /**
     * Use this slot to authenticate the current account to do
     * the synchronization with the Evernote service via the client app.
     * The invocation of this slot would respond asynchronously with
     * authenticationFinished signal but won't start the synchronization
     *
     * If no account was set to SynchronizationManager prior to this slot
     * invocation, it would proceed to OAuth. Otherwise SynchronizationManager
     * would first check whether the persistent authentication data is in place
     * and actual. If yes, no OAuth would be performed.
     */
    void authenticateCurrentAccount();

    /**
     * Use this slot to launch the synchronization of data
     */
    void synchronize();

    /**
     * Use this slot to stop the running synchronization; if no synchronization
     * is running by the moment of this slot call, it has no effect
     */
    void stop();

    /**
     * Use this slot to remove any previously cached authentication tokens
     * (and shard ids) for a given user ID. After the call of this method
     * the next attempt to synchronize the data for this user ID would cause
     * the launch of OAuth to get the new authentication token
     */
    void revokeAuthentication(const qevercloud::UserID userId);

    /**
     * Use this slot to switch the option whether the synchronization of notes
     * would download the note thumbnails or not. By default the downloading
     * of thumbnails for notes containing resources is disabled.
     *
     * NOTE: even if thumbnails downloading is enabled, the thumbnails would
     * be downloaded during sync only for notes containing resources
     *
     * After the method finishes its job, setDownloadNoteThumbnailsDone signal
     * is emitted
     */
    void setDownloadNoteThumbnails(bool flag);

    /**
     * Use this slot to switch the option whether the synchronization of notes
     * would download the plain images corresponding to ink notes or not.
     * By default the downloading of ink note images is disabled.
     *
     * After the method finishes its job, setDownloadInkNoteImagesDone signal
     * is emitted
     */
    void setDownloadInkNoteImages(bool flag);

    /**
     * Use this slot to specify the path to folder at which the downloaded
     * ink note images should be stored. Each ink note image would be stored
     * in a separate PNG file which name would be the same as the guid of
     * the corresponding resource and the file extension would be PNG
     *
     * The default storage path would be the folder "inkNoteImages" within
     * the folder returned by applicationPersistentStoragePath function found
     * in quentier/StandardPaths.h header
     *
     * WARNING: if the passed in path cannot be used (either it doesn't exist
     * and cannot be created or exists but is not writable), the default path
     * is silently restored. So make sure you're setting a valid path
     *
     * After the method finishes its job, setInkNoteImagesStoragePathDone signal
     * is emitted
     */
    void setInkNoteImagesStoragePath(QString path);

Q_SIGNALS:
    /**
     * This signal is emitted when the synchronization is started
     * (authentication is not considered a part of synchronization so this
     * signal is only emitted when the authentication is completed)
     */
    void started();

    /**
     * This signal is emitted in response to invoking the stop slot, whether it
     * was invoked manually or from within the SynchronizationManager itself
     * (due to sync failure, for example)
     */
    void stopped();

    /**
     * This signal is emitted when the synchronization fails; at this moment
     * there is no error code explaining the reason of the failure
     * programmatically so the only explanation available is the textual one for
     * the end user
     */
    void failed(ErrorString errorDescription);

    /**
     * This signal is emitted when the synchronization is finished
     *
     * @param account               Represents the latest version of Account
     *                              structure filled during the synchronization
     *                              procedure
     * @param somethingDownloaded   Boolean parameter telling the receiver
     *                              whether any data items were actually
     *                              downloaded during remote to local
     *                              synchronization step; if there was nothing
     *                              to sync up from the remote storage, this
     *                              boolean would be false, otherwise it would
     *                              be true
     * @param somethingSent         Boolean parameter telling the receiver
     *                              whether any data items were actually sent
     *                              during the send local changes
     *                              synchronization step; if there was nothing
     *                              to send to the remote storage, this boolean
     *                              would be false, otherwise it would be true
     */
    void finished(
        Account account, bool somethingDownloaded, bool somethingSent);

    /**
     * This signal is emitted in response to the attempt to revoke
     * the authentication for a given user ID
     *
     * @param success           True if the authentication was revoked
     *                          successfully, false otherwise
     * @param errorDescription  The textual explanation of the failure to
     *                          revoke the authentication
     * @param userId            The ID of the user for which the revoke of
     *                          the authentication was requested
     */
    void authenticationRevoked(
        bool success, ErrorString errorDescription, qevercloud::UserID userId);

    /**
     * This signal is emitted in response to the explicit attempt to
     * authenticate the new user of the client app to the Evernote service.
     * NOTE: this signal is not emitted if the authentication was requested
     * automatically during sync attempt, it is only emitted in response to
     * the explicit invokation of authenticate slot.
     *
     * @param success           True if the authentication was successful,
     *                          false otherwise
     * @param errorDescription  The textual explanation of the failure to
     *                          authenticate the new user
     * @param account           The account of the authenticated user
     */
    void authenticationFinished(
        bool success, ErrorString errorDescription, Account account);

    /**
     * This signal is emitted when the "remote to local" synchronization step
     * is stopped
     */
    void remoteToLocalSyncStopped();

    /**
     * This signal is emitted when the "send local changes" synchronization step
     * is stopped
     */
    void sendLocalChangesStopped();

    /**
     * This signal is emitted if during the "send local changes" synchronization
     * step it was found out that new changes from the Evernote service are
     * available yet no conflict between remote and local changes was found yet.
     *
     * Such situation can rarely happen in case of changes introduced
     * concurrently with the running synchronization - perhaps via another
     * client. The algorithm will handle it, the signal is just for the sake of
     * diagnostic.
     */
    void willRepeatRemoteToLocalSyncAfterSendingChanges();

    /**
     * This signal is emitted if during the "send local changes" synchronization
     * step it was found out that new changes from the Evernote service are
     * available AND some of them conflict with the local changes being sent.
     *
     * Such situation can rarely happen in case of changes introduced
     * concurrently with the running synchronization - perhaps via another
     * client. The algorithm will handle it by repeating the "remote to local"
     * incremental synchronization step, the signal is just for the sake of
     * diagnostic.
     */
    void detectedConflictDuringLocalChangesSending();

    /**
     * This signal is emitted when the Evernote API rate limit is breached
     * during the synchronization; the algorithm will handle it by auto-pausing
     * itself until the time necessary to wait passes and then automatically
     * continuing the synchronization.
     *
     * @param secondsToWait     The amount of time (in seconds) necessary to
     *                          wait before the synchronization will continue
     */
    void rateLimitExceeded(qint32 secondsToWait);

    /**
     * This signal is emitted when the "remote to local" synchronization step
     * is finished; once that step is done, the algorithn switches to sending
     * the local changes back to the Evernote service.
     *
     * @param somethingDownloaded       Boolean parameter telling the receiver
     *                                  whether any data items were actually
     *                                  downloaded during remote to local
     *                                  synchronization step; if there was
     *                                  nothing to sync up from the remote
     *                                  storage, this boolean would be false,
     *                                  otherwise it would be true
     */
    void remoteToLocalSyncDone(bool somethingDownloaded);

    /**
     * This signal is emitted during user own account's sync chunks downloading
     * and denotes the progress of that step. The percentage of completeness can
     * be computed roughly as (highestDownloadedUsn - lastPreviousUsn) /
     * (highestServerUsn - lastPreviousUsn) * 100%
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
     * This signal is emitted when the sync chunks for the stuff from user's own
     * account are downloaded during "remote to local" synchronization step
     */
    void syncChunksDownloaded();

    /**
     * This signal is emitted during linked notebooks sync chunks downloading
     * and denotes the progress of that step, individually for each linked
     * notebook. The percentage of completeness can be computed roughly as
     * (highestDownloadedUsn - lastPreviousUsn) /
     * (highestServerUsn - lastPreviousUsn) * 100%.
     * The sync chunks for each linked notebook are downloaded sequentially so
     * the signals for one linked notebook should not intermix with signals for
     * other linked notebooks, however, it is within hands of Qt's slot
     * schedulers
     *
     * @param highestDownloadedUsn      The highest update sequence number
     *                                  within data items from linked notebook
     *                                  sync chunks downloaded so far
     * @param highestServerUsn          The current highest update sequence
     *                                  number within the linked notebook
     * @param lastPreviousUsn           The last update sequence number from
     *                                  previous sync of the given linked
     *                                  notebook; if current sync is the first
     *                                  one, this value is zero
     * @param linkedNotebook            The linked notebook which sync chunks
     *                                  download progress is reported
     */
    void linkedNotebookSyncChunksDownloadProgress(
        qint32 highestDownloadedUsn, qint32 highestServerUsn,
        qint32 lastPreviousUsn, LinkedNotebook linkedNotebook);

    /**
     * This signal is emitted when the sync chunks for the stuff from linked
     * notebooks are downloaded during "remote to local" synchronization step
     */
    void linkedNotebooksSyncChunksDownloaded();

    /**
     * This signal is emitted on each successful download of full note data from
     * use 's own account.
     *
     * @param notesDownloaded       The number of notes downloaded by the moment
     * @param totalNotesToDownload  The total number of notes that need to be
     *                              downloaded
     */
    void notesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    /**
     * This signal is emitted on each successful download of full note data from
     * linked notebooks.
     *
     * @param notesDownloaded       The number of notes downloaded by the moment
     * @param totalNotesToDownload  The total number of notes that need to be
     *                              downloaded
     */
    void linkedNotebooksNotesDownloadProgress(
        quint32 notesDownloaded, quint32 totalNotesToDownload);

    /**
     * This signal is emitted on each successful doenload of full resource data
     * from user's own account during the incremental sync (as individual
     * resources are downloaded along with their notes during full sync).
     *
     * @param resourcesDownloaded       The number of resources downloaded
     *                                  by the moment
     * @param totalResourcesToDownload  The total number of resources that
     *                                  need to be downloaded
     */
    void resourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    /**
     * This signal is emitted on each successful download of full resource data
     * from linked notebooks during the incremental sync (as individual
     * resources are downloaded along with their notes during full sync).
     *
     * @param resourcesDownloaded       The number of resources downloaded
     *                                  by the moment
     * @param totalResourcesToDownload  The total number of resources that
     *                                  need to be downloaded
     */
    void linkedNotebooksResourcesDownloadProgress(
        quint32 resourcesDownloaded, quint32 totalResourcesToDownload);

    /**
     * This signal is emitted during "send local changes" synchronization step
     * when all the relevant data elements from user's own account were prepared
     * for sending to the Evernote service
     */
    void preparedDirtyObjectsForSending();

    /**
     * This signal is emitted during "send local changes" synchronization step
     * when all the relevant data elements from linked notebooks were prepared
     * for sending to the Evernote service
     */
    void preparedLinkedNotebooksDirtyObjectsForSending();

    /**
     * This signal is emitted in response to invoking the setAccount slot after
     * all the activities involved in switching the account inside
     * SynchronizationManager are finished
     */
    void setAccountDone(Account account);

    /**
     * This signal is emitted in response to invoking
     * the setDownloadNoteThumbnails slot after the setting is accepted
     */
    void setDownloadNoteThumbnailsDone(bool flag);

    /**
     * This signal is emitted in response to invoking
     * the setDownloadInkNoteImages slot after the setting is accepted
     */
    void setDownloadInkNoteImagesDone(bool flag);

    /**
     * This signal is emitted in response to invoking
     * the setInkNoteImagesStoragePath slot after the setting is accepted
     */
    void setInkNoteImagesStoragePathDone(QString path);

private:
    SynchronizationManager() = delete;
    Q_DISABLE_COPY(SynchronizationManager)

    SynchronizationManagerPrivate * d_ptr;
    Q_DECLARE_PRIVATE(SynchronizationManager)
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_H
