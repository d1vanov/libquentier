/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include <quentier/types/ErrorString.h>
#include <quentier/utility/FileSystemWatcher.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Resource.h>

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <memory>

class QWidget;

namespace quentier {

/**
 * @brief The ResourceDataInTemporaryFileStorageManager class is intended to
 * provide the service of reading and writing the resource data from/to
 * temporary files. The purpose of having a separate class for that is to
 * encapsulate the logics around the checks for resource temporary files
 * existence and actuality and also to make it possible to move all the resource
 * file IO into a separate thread.
 */
class ResourceDataInTemporaryFileStorageManager final : public QObject
{
    Q_OBJECT
public:
    explicit ResourceDataInTemporaryFileStorageManager(
        QObject * parent = nullptr);

    enum class Error
    {
        EmptyLocalId = -1,
        EmptyRequestId = -2,
        EmptyData = -3,
        NoResourceFileStorageLocation = -4
    };

    [[nodiscard]] static QString imageResourceFileStorageFolderPath();
    [[nodiscard]] static QString nonImageResourceFileStorageFolderPath();

Q_SIGNALS:
    void saveResourceDataToTemporaryFileCompleted(
        QUuid requestId, QByteArray dataHash, ErrorString errorDescription);

    void readResourceFromFileCompleted(
        QUuid requestId, QByteArray data, QByteArray dataHash, int errorCode,
        ErrorString errorDescription);

    void resourceFileChanged(
        QString resourceLocalId, QString fileStoragePath,
        QByteArray resourceData, QByteArray resourceDataHash);

    // 1) Signals notifying about the state after changing the current note

    /**
     * @brief failedToPutResourceDataIntoTemporaryFile signal is emitted when
     * the resource data of some image resource failed to be written into a
     * temporary file for the sake of safe display within note editor's page
     *
     * @param resourceLocalId       The local id of the resource which data was
     *                              not successfully written into a temporary
     *                              file
     * @param noteLocalId           The local id of the note one of which
     *                              resource's data was not successfully
     *                              written into a temporary file
     * @param errorDescription      The textual description of the error which
     *                              occurred on attempt to write resource data
     *                              to a temporary file
     */
    void failedToPutResourceDataIntoTemporaryFile(
        QString resourceLocalId, QString noteLocalId,
        ErrorString errorDescription);

    /**
     * @brief noteResourcesPreparationProgress signal is emitted to notify the
     * client about the progress in putting the note's image resources into
     * temporary files for the sake of their safe display within note editor's
     * page.
     *
     * @param progress              Progress value, between 0 and 1
     * @param noteLocalId           The local id of the note which resources
     *                              preparation progress is being notified about
     */
    void noteResourcesPreparationProgress(
        double progress, QString noteLocalId);

    /**
     * @brief noteResourcesPreparationError signal is emitted when some error
     * occurs which leads to incorrect or incomplete preparation of note's image
     * resources for note editor page's loading
     *
     * @param noteLocalId           The local id of the note which resources
     *                              preparation errored
     * @param errorDescription      The textual description of the error
     */
    void noteResourcesPreparationError(
        QString noteLocalId, ErrorString errorDescription);

    /**
     * @brief noteResourcesReady signal is emitted when all image resources for
     * the current note were put into temporary files so the note editor's page
     * can safely load them
     *
     * @param noteLocalId           The local id of the note all of which
     *                              image resources were successfully put into
     *                              temporary files
     */
    void noteResourcesReady(QString noteLocalId);

    // 2) Signals notifying about the state of open resource operation

    /**
     * @brief openResourcePreparationProgress signal is emitted to notify the
     * client about the progress in preparing the resource file for being
     * opened in some external program for viewing/editing
     *
     * @param progress              Progress value, between 0 and 1
     * @param resourceLocalId       The local id of the resource which
     *                              temporary file is being prepared for being
     *                              opened
     * @param noteLocalId           The local id of the note which resource
     *                              temporary file is being prepared for being
     *                              opened
     */
    void openResourcePreparationProgress(
        double progress, QString resourceLocalId, QString noteLocalId);

    /**
     * @brief failedToOpenResource signal is emitted if opening the temporary
     * file containing the resource data failed
     *
     * @param resourceLocalId       The local id of the resource which
     *                              temporary file failed to be opened
     * @param noteLocalId           The local id of the note which resource
     *                              temporary file failed to be opened
     */
    void failedToOpenResource(
        QString resourceLocalId, QString noteLocalId,
        ErrorString errorDescription);

    /**
     * @brief openedResource signal is emitted after the successful opening of
     * the resource in the external program for viewing/editing
     *
     * @param resourceLocalId       The local id of the resource which
     *                              temporary file was opened
     * @param noteLocalId           The local id of the note which
     *                              resource temporary file was opened
     */
    void openedResource(QString resourceLocalId, QString noteLocalId);

    // 3) Auxiliary signals

    /**
     * @brief diagnosticsCollected signal is emitted in response to the previous
     * invocation of onRequestDiagnostics slot
     *
     * @param requestId             The identifier of the request to collect
     *                              the diagnostic
     * @param diagnostics           Collected diagnostics
     */
    void diagnosticsCollected(QUuid requestId, QString diagnostics);

public Q_SLOTS:
    /**
     * @brief onSaveResourceDataToTemporaryFileRequest - slot being called when
     * the resource data needs to be saved to a temporary file; the method would
     * also check that the already existing file (if any) is actual. If so, it
     * would return successfully without doing any IO.
     *
     * @param noteLocalId           The local id of the note to which
     *                              the resource belongs
     * @param resourceLocalId       The local id of the resource for which
     *                              the data is written to file
     * @param data                  The resource data to be written to file
     * @param dataHash              The hash of the resource data; if it's
     *                              empty, it would be calculated by the method
     *                              itself
     * @param requestId             Request identifier for writing the data to
     *                              file
     * @param isImage               Indicates whether the resource is the image
     *                              which can be displayed inline
     *                              in the note editor page
     */
    void onSaveResourceDataToTemporaryFileRequest(
        QString noteLocalId, QString resourceLocalId, QByteArray data,
        QByteArray dataHash, QUuid requestId, bool isImage);

    /**
     * @brief onReadResourceFromFileRequest - slot being called when
     * the resource data and hash need to be read from local file
     *
     * @param fileStoragePath       The path at which the resource is stored
     * @param resourceLocalId       The local id of the resource for which
     *                              the data and hash should be read from file
     * @param requestId             Request identifier for reading the resource
     *                              data and hash from file
     */
    void onReadResourceFromFileRequest(
        QString fileStoragePath, QString resourceLocalId, QUuid requestId);

    /**
     * @brief onOpenResourceRequest slot should be invoked when the temporary
     * file containing the resource data is requested to be opened in some
     * external program for viewing and/or editing.
     *
     * If the resource data hasn't been written into the temporary file yet, it
     * will be written when the slot is invoked. The resource data in temporary
     * file storage manager would watch for the changes of the opened resource
     * file until the current note in the note editor is changed
     *
     * @param resourceLocalId       The local id of the resource corresponding
     *                              to the temporary file which is requested
     *                              to be opened
     */
    void onOpenResourceRequest(QString resourceLocalId);

    /**
     * @brief onCurrentNoteChanged is a slot which should be called when
     * the current note in the note editor is changed.
     *
     * When the note is changed, this object stops watching for the changes of
     * resource files belonging to the previously edited note in the note
     * editor; it is due to the performance cost and OS limitations for
     * the number of files which can be monitored for changes simultaneously by
     * one process
     */
    void onCurrentNoteChanged(qevercloud::Note note);

    /**
     * @brief onRequestDiagnostics is a slot which initiates the collection of
     * diagnostics regarding the internal state of
     * ResourceDataInTemporaryFileStorageManager.
     *
     * This slot is intended primarily for troubleshooting purposes
     */
    void onRequestDiagnostics(QUuid requestId);

    // private signals
Q_SIGNALS:
    void findResourceData(const QString & resourceLocalId);

private Q_SLOTS:
    void onFileChanged(const QString & path);
    void onFileRemoved(const QString & path);

    // Slots for dealing with NoteEditorLocalStorageBroker

    void onFoundResourceData(qevercloud::Resource resource);

    void onFailedToFindResourceData(
        QString resourceLocalId, ErrorString errorDescription);

private:
    void createConnections();
    [[nodiscard]] QByteArray calculateHash(const QByteArray & data) const;

    [[nodiscard]] bool checkIfResourceFileExistsAndIsActual(
        const QString & noteLocalId, const QString & resourceLocalId,
        const QString & fileStoragePath, const QByteArray & dataHash) const;

    [[nodiscard]] bool updateResourceHashHelperFile(
        const QString & resourceLocalId, const QByteArray & dataHash,
        const QString & storageFolderPath, int & errorCode,
        ErrorString & errorDescription);

    void watchResourceFileForChanges(
        const QString & resourceLocalId, const QString & fileStoragePath);

    void stopWatchingResourceFile(const QString & filePath);
    void removeStaleResourceFilesFromCurrentNote();

    enum class ResultType
    {
        Ready = 0,
        Error,
        AsyncPending
    };

    /**
     * Compares the list of resources from the previous version of current note
     * with the actual list of current note's resources, removes stale resource
     * data temporary files and puts the data of new and updated resources into
     * temporary data files. The process might be asynchronous as some resources
     * might not have binary data set which means it needs to be queried from
     * the local storage.
     */
    [[nodiscard]] ResultType partialUpdateResourceFilesForCurrentNote(
        const QList<qevercloud::Resource> & previousResources,
        ErrorString & errorDescription);

    /**
     * Callback for writeResourceDataToTemporaryFile which emits
     * noteResourcesPreparationProgress signal with the given progress value
     */
    void emitPartialUpdateResourceFilesForCurrentNoteProgress(double progress);

    /**
     * Wrapper around emitPartialUpdateResourceFilesForCurrentNoteProgress
     * callback for writeResourceDataToTemporaryFile, normalizes the given
     * progress value which corresponds to a single resource to the progress
     * for all of current note's resources
     */
    class PartialUpdateResourceFilesForCurrentNoteProgressFunctor
    {
    public:
        PartialUpdateResourceFilesForCurrentNoteProgressFunctor(
            const int resourceIndex, const int numResources,
            ResourceDataInTemporaryFileStorageManager & manager) :
            m_resourceIndex(resourceIndex),
            m_numResources(numResources), m_manager(manager)
        {}

        void operator()(const double progress)
        {
            double doneProgress =
                static_cast<double>(m_resourceIndex) / m_numResources;
            double normalizedProgress = progress / m_numResources;
            m_manager.emitPartialUpdateResourceFilesForCurrentNoteProgress(
                doneProgress + normalizedProgress);
        }

    private:
        int m_resourceIndex;
        int m_numResources;
        ResourceDataInTemporaryFileStorageManager & m_manager;
    };

    /**
     * Writes binary data of passed in resources to temporary files unless such
     * files already exist and are actual. The process might be asynchronous as
     * some resources might not have binary data set which means it needs
     * to be queried from the local storage.
     */
    [[nodiscard]] ResultType putResourcesDataToTemporaryFiles(
        const QList<qevercloud::Resource> & resources, ErrorString & errorDescription);

    /**
     * Callback for writeResourceDataToTemporaryFile which emits
     * openResourcePreparationProgress with the given progress value for
     * the given resource
     */
    void emitOpenResourcePreparationProgress(
        double progress, const QString & resourceLocalId);

    /**
     * Wrapper around emitOpenResourcePreparationProgress
     * callback for writeResourceDataToTemporaryFile, simply passes through
     * the local uid of the prepared resource
     */
    class OpenResourcePreparationProgressFunctor
    {
    public:
        OpenResourcePreparationProgressFunctor(
            QString resourceLocalId,
            ResourceDataInTemporaryFileStorageManager & manager) :
            m_resourceLocalId(std::move(resourceLocalId)),
            m_manager(manager)
        {}

        void operator()(const double progress)
        {
            m_manager.emitOpenResourcePreparationProgress(
                progress, m_resourceLocalId);
        }

    private:
        QString m_resourceLocalId;
        ResourceDataInTemporaryFileStorageManager & m_manager;
    };

    void requestResourceDataFromLocalStorage(
        const qevercloud::Resource & resource);

    enum class ResourceType
    {
        Image = 0,
        NonImage
    };

    enum class CheckResourceFileActualityOption
    {
        On = 0,
        Off
    };

    using WriteResourceDataCallback = std::function<void(const double)>;

    [[nodiscard]] bool writeResourceDataToTemporaryFile(
        const QString & noteLocalId, const QString & resourceLocalId,
        const QByteArray & data, const QByteArray & dataHash,
        ResourceType resourceType, ErrorString & errorDescription,
        CheckResourceFileActualityOption checkActualityOption =
            CheckResourceFileActualityOption::On,
        const WriteResourceDataCallback & callback = {});

private:
    Q_DISABLE_COPY(ResourceDataInTemporaryFileStorageManager)

private:
    QString m_nonImageResourceFileStorageLocation;
    QString m_imageResourceFileStorageLocation;

    std::unique_ptr<qevercloud::Note> m_pCurrentNote;

    /**
     * Local uids of image resources from current note which are pending full
     * binary data extraction from local storage for writing to temporary files
     * for the sake of note editor page loading
     */
    QSet<QString> m_resourceLocalIdsPendingFindInLocalStorage;

    /**
     * Local uids of resources from current note which are pending full binary
     * data extraction from local storage for writing to temporary files because
     * these resources were required to be opened in external program
     */
    QSet<QString>
        m_resourceLocalIdsPendingFindInLocalStorageForWritingToFileForOpening;

    QHash<QString, QString> m_resourceLocalIdByFilePath;
    FileSystemWatcher m_fileSystemWatcher;
};

} // namespace quentier
