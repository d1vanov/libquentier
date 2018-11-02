/*
 * Copyright 2016-2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_RESOURCE_DATA_IN_TEMPORARY_FILE_STORAGE_MANAGER_H
#define LIB_QUENTIER_NOTE_EDITOR_RESOURCE_DATA_IN_TEMPORARY_FILE_STORAGE_MANAGER_H

#include <quentier/utility/Macros.h>
#include <quentier/utility/FileSystemWatcher.h>
#include <quentier/types/ErrorString.h>
#include <QObject>
#include <QUuid>
#include <QStringList>
#include <QHash>
#include <QScopedPointer>

// NOTE: Workaround a bug in Qt4 which may prevent building with some boost versions
#ifndef Q_MOC_RUN
#include <boost/function.hpp>
#endif

QT_FORWARD_DECLARE_CLASS(QWidget)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Note)
QT_FORWARD_DECLARE_CLASS(Resource)

/**
 * @brief The ResourceDataInTemporaryFileStorageManager class is intended to provide the service of
 * reading and writing the resource data from/to temporary files. The purpose of having
 * a separate class for that is to encapsulate the logics around the checks for resource
 * temporary files existence and actuality and also to make it possible to move all
 * the resource file IO into a separate thread.
 */
class Q_DECL_HIDDEN ResourceDataInTemporaryFileStorageManager: public QObject
{
    Q_OBJECT
public:
    explicit ResourceDataInTemporaryFileStorageManager(QObject * parent = Q_NULLPTR);

    struct Errors
    {
        enum type
        {
            EmptyLocalUid = -1,
            EmptyRequestId = -2,
            EmptyData = -3,
            NoResourceFileStorageLocation = -4
        };
    };

    static QString imageResourceFileStorageFolderPath();
    static QString nonImageResourceFileStorageFolderPath();

Q_SIGNALS:
    void writeResourceToFileCompleted(QUuid requestId, QByteArray dataHash,
                                      QString fileStoragePath, int errorCode, ErrorString errorDescription);
    void readResourceFromFileCompleted(QUuid requestId, QByteArray data, QByteArray dataHash,
                                       int errorCode, ErrorString errorDescription);

    void resourceFileChanged(QString localUid, QString fileStoragePath);

    // 1) ========= Signals notifying about the state after changing the current note ========

    /**
     * @brief failedToPutResourceDataIntoTemporaryFile signal is emitted when
     * the resource data of some image resource failed to be written into a
     * temporary file for the sake of safe display within note editor's page
     *
     * @param resourceLocalUid              The local uid of the resource which data was not successfully written
     *                                      into a temporary file
     * @param noteLocalUid                  The local uid of the note one of which resource's data was not successfully
     *                                      written into a temporary file
     * @param errorDescription              The textual description of the error which occurred on attempt
     *                                      to write resource data to a temporary file
     */
    void failedToPutResourceDataIntoTemporaryFile(QString resourceLocalUid, QString noteLocalUid, ErrorString errorDescription);

    /**
     * @brief noteResourcesPreparationProgress signal is emitted to notify the
     * client about the progress in putting the note's image resources into
     * temporary files for the sake of their safe display within note editor's
     * page.
     *
     * @param progress                      Progress value, between 0 and 1
     * @param noteLocalUid                  The local uid of the note which
     *                                      resources preparation progress is being
     *                                      notified about
     */
    void noteResourcesPreparationProgress(double progress, QString noteLocalUid);

    /**
     * @brief noteResourcesPreparationError signal is emitted when some error occurs
     * which leads to incorrect or incomplete preparation of note's image resources
     * for note editor page's loading
     *
     * @param errorDescription              The textual description of the error
     * @param noteLocalUid                  The local uid of the note which resources preparation errored
     */
    void noteResourcesPreparationError(ErrorString errorDescription, QString noteLocalUid);

    /**
     * @brief noteResourcesReady signal is emitted when all image resources for
     * the current note were put into temporary files so the note editor's page
     * can safely load them
     *
     * @param noteLocalUid                  The local uid of the note all of which
     *                                      image resources were successfully
     *                                      put into temporary files
     */
    void noteResourcesReady(QString noteLocalUid);

    // 2) ======== Signals notifying about the state of open resource operation ========

    /**
     * @brief openResourcePreparationProgress signal is emitted to notify the
     * client about the progress in preparing the resource file for being
     * opened in some external program for viewing/editing
     *
     * @param progress                      Progress value, between 0 and 1
     * @param resourceLocalUid              The local uid of the resource which
     *                                      temporary file is being prepared for being opened
     * @param noteLocalUid                  The local uid of the note which
     *                                      resource temporary file is being prepared
     *                                      for being opened
     */
    void openResourcePreparationProgress(double progress, QString resourceLocalUid, QString noteLocalUid);

    /**
     * @brief failedToOpenResource signal is emitted if opening the temporary
     * file containing the resource data failed
     *
     * @param resourceLocalUid              The local uid of the resource which
     *                                      temporary file failed to be opened
     * @param noteLocalUid                  The local uid of the note which
     *                                      resource temporary file failed
     *                                      to be opened
     */
    void failedToOpenResource(QString resourceLocalUid, QString noteLocalUid, ErrorString errorDescription);

    /**
     * @brief openedResource signal is emitted after the successful opening of
     * the resource in the external program for viewing/editing
     *
     * @param resourceLocalUid              The local uid of the resource which
     *                                      temporary file was opened
     * @param noteLocalUid                  The local uid of the note which
     *                                      resource temporary file was opened
     */
    void openedResource(QString resourceLocalUid, QString noteLocalUid);

    // 3) ======== Auxiliary signals ========

    /**
     * @brief diagnosticsCollected signal is emitted in response to the previous
     * invocation of onRequestDiagnostics slot
     *
     * @param requestId                     The identifier of the request to collect the diagnostic
     * @param diagnostics                   Collected diagnostics
     */
    void diagnosticsCollected(QUuid requestId, QString diagnostics);

public Q_SLOTS:
    /**
     * @brief onWriteResourceToFileRequest - slot being called when the resource data needs to be written
     * to local file; the method would also check that the already existing file (if any) is actual.
     * If so, it would return successfully without doing any IO.
     * @param noteLocalUid - the local uid of the note to which the resource belongs
     * @param resourceLocalUid - the local uid of the resource for which the data is written to file
     * @param data - the resource data to be written to file
     * @param dataHash - the hash of the resource data; if it's empty, it would be calculated by the method itself
     * @param preferredFileSuffix - the preferred file suffix for the resource; if empty, the resource file is written withoug suffix
     * @param requestId - request identifier for writing the data to file
     * @param isImage - indicates whether the resource is the image which can be displayed inline in the note editor page
     */
    void onWriteResourceToFileRequest(QString noteLocalUid, QString resourceLocalUid, QByteArray data, QByteArray dataHash,
                                      QString preferredFileSuffix, QUuid requestId, bool isImage);

    /**
     * @brief onReadResourceFromFileRequest - slot being called when the resource data and hash need to be read
     * from local file
     * @param fileStoragePath - the path at which the resource is stored
     * @param resourceLocalUid - the local uid of the resource for which the data and hash should be read from file
     * @param requestId - request identifier for reading the resource data and hash from file
     */
    void onReadResourceFromFileRequest(QString fileStoragePath, QString resourceLocalUid, QUuid requestId);

    /**
     * @brief onOpenResourceRequest slot should be invoked when the temporary file containing the resource data
     * is requested to be opened in some external program for viewing and/or editing; if the resource data
     * hasn't been written into the temporary file yet, it will be written when the slot is called. The resource data
     * in temporary file storage manager would watch for the changes of the opened resource file until the current note
     * in the note editor is changed
     *
     * @param resourceLocalUid      The local uid of the resource corresponding to the temporary file which is requested
     *                              to be opened
     */
    void onOpenResourceRequest(QString resourceLocalUid);

    /**
     * @brief onCurrentNoteChanged - slot which should be called when the current note in the note editor is changed;
     * when the note is changed, this object stops watching for the changes of resource files belonging
     * to the previously edited note in the note editor; it is due to the performance cost and OS limitations
     * for the number of files which can be monitored for changes simultaneously by one process
     */
    void onCurrentNoteChanged(Note note);

    /**
     * @brief onRequestDiagnostics - slot which initiates the collection of diagnostics regarding the internal state of
     * ResourceDataInTemporaryFileStorageManager; intended primarily for troubleshooting purposes
     */
    void onRequestDiagnostics(QUuid requestId);

    // private signals
Q_SIGNALS:
    void findResourceData(const QString & resourceLocalUid);

private Q_SLOTS:
    void onFileChanged(const QString & path);
    void onFileRemoved(const QString & path);

    // Slots for dealing with NoteEditorLocalStorageBroker
    void onFoundResourceData(Resource resource);
    void onFailedToFindResourceData(QString resourceLocalUid, ErrorString errorDescription);

private:
    void createConnections();
    QByteArray calculateHash(const QByteArray & data) const;
    bool checkIfResourceFileExistsAndIsActual(const QString & noteLocalUid, const QString & resourceLocalUid,
                                              const QString & fileStoragePath, const QByteArray & dataHash) const;

    bool updateResourceHash(const QString & resourceLocalUid, const QByteArray & dataHash,
                            const QString & storageFolderPath, int & errorCode, ErrorString & errorDescription);
    void watchResourceFileForChanges(const QString & resourceLocalUid, const QString & fileStoragePath);
    void stopWatchingResourceFile(const QString & filePath);
    void removeStaleResourceFilesFromCurrentNote();

    struct ResultType
    {
        enum type
        {
            Ready = 0,
            Error,
            AsyncPending
        };
    };

    ResultType::type partialUpdateResourceFilesForCurrentNote(const QList<Resource> & previousResources,
                                                              ErrorString & errorDescription);

    /**
     * Callback for writeResourceDataToTemporaryFile which emits noteResourcesPreparationProgress signal with the
     * given progress value
     */
    void emitPartialUpdateResourceFilesForCurrentNoteProgress(const double progress);

    /**
     * Wrapper around emitPartialUpdateResourceFilesForCurrentNoteProgress
     * callback for writeResourceDataToTemporaryFile, normalizes the given
     * progress value which corresponds to a single resource to the progress
     * for all of current note's resources
     */
    class PartialUpdateResourceFilesForCurrentNoteProgressFunctor
    {
    public:
        PartialUpdateResourceFilesForCurrentNoteProgressFunctor(const int resourceIndex, const int numResources,
                                                                ResourceDataInTemporaryFileStorageManager & manager) :
            m_resourceIndex(resourceIndex),
            m_numResources(numResources),
            m_manager(manager)
        {}

        void operator()(const double progress)
        {
            double doneProgress = static_cast<double>(m_resourceIndex) / m_numResources;
            double normalizedProgress = progress / m_numResources;
            m_manager.emitPartialUpdateResourceFilesForCurrentNoteProgress(doneProgress + normalizedProgress);
        }

    private:
        int m_resourceIndex;
        int m_numResources;
        ResourceDataInTemporaryFileStorageManager & m_manager;
    };

    void requestResourceDataFromLocalStorage(const Resource & resource);

    struct ResourceType
    {
        enum type
        {
            Image = 0,
            NonImage
        };
    };

    typedef boost::function<void (const double)> WriteResourceDataCallback;

    bool writeResourceDataToTemporaryFile(const QString & noteLocalUid, const QString & resourceLocalUid,
                                          const QByteArray & data, const QByteArray & dataHash,
                                          const ResourceType::type resourceType,
                                          ErrorString & errorDescription, WriteResourceDataCallback = 0);

private:
    Q_DISABLE_COPY(ResourceDataInTemporaryFileStorageManager)

private:
    QString     m_nonImageResourceFileStorageLocation;
    QString     m_imageResourceFileStorageLocation;

    QScopedPointer<Note>        m_pCurrentNote;

    QSet<QString>               m_resourceLocalUidsPendingFindInLocalStorage;

    QHash<QString, QString>     m_resourceLocalUidByFilePath;
    FileSystemWatcher           m_fileSystemWatcher;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_RESOURCE_DATA_IN_TEMPORARY_FILE_STORAGE_MANAGER_H
