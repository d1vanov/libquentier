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

#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ADD_RESOURCE_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ADD_RESOURCE_DELEGATE_H

#include "JsResultCallbackFunctor.hpp"

#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>
#include <quentier/types/Resource.h>

#include <QByteArray>
#include <QHash>
#include <QMimeType>
#include <QObject>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Account)
QT_FORWARD_DECLARE_CLASS(FileIOProcessorAsync)
QT_FORWARD_DECLARE_CLASS(GenericResourceImageManager)
QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)
QT_FORWARD_DECLARE_CLASS(ResourceDataInTemporaryFileStorageManager)

/**
 * The AddResourceDelegate class wraps a series of asynchronous actions required
 * for adding a resource to the note
 */
class Q_DECL_HIDDEN AddResourceDelegate final : public QObject
{
    Q_OBJECT
public:
    /**
     * The constructor of AddResourceDelegate class accepting the resource to be
     * added as a file path - it is presumed that the resource data to be added
     * to the note is located in the file with the given path.
     *
     * @param filePath                      The absolute path to the file in
     *                                      which the resource data is located
     * @param noteEditor                    The note editor holding the note to
     *                                      which the resource is to be added
     * @param pResourceDataManager          The pointer to
     *                                      ResourceDataInTemporaryFileStorageManager
     *                                      which might be required for storing
     *                                      the new resource's data in a
     *                                      temporary file for the sake of note
     *                                      editor page's convenience; it is
     *                                      only actually required if the added
     *                                      resource is an image
     * @param pFileIOThreadWorker           The pointer to FileIOProcessorAsync
     *                                      worker performing the actual IO of
     *                                      file data
     * @param pGenericResourceImageManager  The pointer to
     * GenericResourceImageManager required for composing the generic resource
     * image for QWebEngine-based backend of NoteEditor
     * @param genericResourceImageFilePathsByResourceHash   The hash container
     *                                                      storing generic
     *                                                      resource image file
     *                                                      paths by resource
     *                                                      hash
     */
    explicit AddResourceDelegate(
        const QString & filePath, NoteEditorPrivate & noteEditor,
        ResourceDataInTemporaryFileStorageManager * pResourceDataManager,
        FileIOProcessorAsync * pFileIOThreadWorker,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> &
            genericResourceImageFilePathsByResourceHash);

    /**
     * The constructor of AddResourceDelegate class accepting the actual
     * resource data to be inserted into the note.
     *
     * @param resourceData                  The resource data to be added to
     *                                      the note as a new resource
     * @param mimeType                      The mime type of resource data
     * @param noteEditor                    The note editor holding the note to
     *                                      which the resource is to be added
     * @param pResourceDataManager          The pointer to
     *                                      ResourceDataInTemporaryFileStorageManager
     *                                      which might be required for storing
     *                                      the new resource's data in a
     *                                      temporary file for the sake of note
     *                                      editor page's convenience; it is
     *                                      only actually required if the added
     *                                      resource is an image
     * @param pFileIOThreadWorker           The pointer to FileIOProcessorAsync
     *                                      worker performing the actual IO of
     *                                      file data
     * @param pGenericResourceImageManager  The pointer to
     * GenericResourceImageManager required for composing the generic resource
     * image for QWebEngine-based backend of NoteEditor
     * @param genericResourceImageFilePathsByResourceHash   The hash container
     *                                                      storing generic
     *                                                      resource file paths
     *                                                      by resource hash
     */
    explicit AddResourceDelegate(
        const QByteArray & resourceData, const QString & mimeType,
        NoteEditorPrivate & noteEditor,
        ResourceDataInTemporaryFileStorageManager * pResourceDataManager,
        FileIOProcessorAsync * pFileIOThreadWorker,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> &
            genericResourceImageFilePathsByResourceHash);

    void start();

Q_SIGNALS:
    void finished(Resource addedResource, QString resourceFileStoragePath);
    void notifyError(ErrorString error);

    // private signals
    void readFileData(QString filePath, QUuid requestId);

    void saveResourceDataToTemporaryFile(
        QString noteLocalUid, QString resourceLocalUid, QByteArray data,
        QByteArray dataHash, QUuid requestId, bool isImage);

    void writeFile(QString filePath, QByteArray data, QUuid requestId);

    void saveGenericResourceImageToFile(
        QString noteLocalUid, QString resourceLocalUid, QByteArray data,
        QString fileSuffix, QByteArray dataHash, QString fileStoragePath,
        QUuid requestId);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);

    void onResourceFileRead(
        bool success, ErrorString errorDescription, QByteArray data,
        QUuid requestId);

    void onResourceDataSavedToTemporaryFile(
        QUuid requestId, QByteArray dataHash, ErrorString errorDescription);

    void onGenericResourceImageSaved(
        bool success, QByteArray resourceImageDataHash, QString filePath,
        ErrorString errorDescription, QUuid requestId);

    void onNewResourceHtmlInserted(const QVariant & data);

private:
    void doStart();
    void doStartUsingFile();
    void doStartUsingData();

    void doSaveResourceDataToTemporaryFile(
        const QByteArray & data, QString resourceName);

    void doGenerateGenericResourceImage(
        const QByteArray & data, QString resourceName);

    void insertNewResourceHtml();

    bool checkResourceDataSize(
        const Note & note, const Account * pAccount, const qint64 size);

private:
    typedef JsResultCallbackFunctor<AddResourceDelegate> JsCallback;

private:
    NoteEditorPrivate & m_noteEditor;
    ResourceDataInTemporaryFileStorageManager *
        m_pResourceDataInTemporaryFileStorageManager;
    FileIOProcessorAsync * m_pFileIOProcessorAsync;

    QHash<QByteArray, QString> & m_genericResourceImageFilePathsByResourceHash;
    GenericResourceImageManager * m_pGenericResourceImageManager;
    QUuid m_saveResourceImageRequestId;

    /**
     * The resource to be added to the note is either stored in some external
     * file or is inserted into the note as a raw data; so if m_filePath is not
     * empty, it is used as a source of the new resource's data; otherwise
     * m_data is used instead
     */
    const QString m_filePath;
    QByteArray m_data;

    QMimeType m_resourceMimeType;

    Resource m_resource;
    QString m_resourceFileStoragePath;

    QUuid m_readResourceFileRequestId;
    QUuid m_saveResourceDataToTemporaryFileRequestId;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ADD_RESOURCE_DELEGATE_H
