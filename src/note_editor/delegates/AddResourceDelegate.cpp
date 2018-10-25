/*
 * Copyright 2016 Dmitry Ivanov
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

#include "AddResourceDelegate.h"
#include "../NoteEditor_p.h"
#include "../NoteEditorPage.h"
#include "../GenericResourceImageManager.h"
#include "../ResourceDataInTemporaryFileStorageManager.h"
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Resource.h>
#include <quentier/types/Account.h>
#include <QImage>
#include <QBuffer>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QCryptographicHash>

#ifndef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebFrame>
#endif

namespace quentier {

#define GET_PAGE() \
    NoteEditorPage * page = qobject_cast<NoteEditorPage*>(m_noteEditor.page()); \
    if (Q_UNLIKELY(!page)) { \
        ErrorString error(QT_TRANSLATE_NOOP("AddResourceDelegate", "Can't add attachment: no note editor page")); \
        QNWARNING(error); \
        Q_EMIT notifyError(error); \
        return; \
    }

AddResourceDelegate::AddResourceDelegate(const QString & filePath, NoteEditorPrivate & noteEditor,
                                         ResourceDataInTemporaryFileStorageManager * pResourceFileStorageManager,
                                         FileIOProcessorAsync * pFileIOProcessorAsync,
                                         GenericResourceImageManager * pGenericResourceImageManager,
                                         QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor),
    m_pResourceDataInTemporaryFileStorageManager(pResourceFileStorageManager),
    m_pFileIOProcessorAsync(pFileIOProcessorAsync),
    m_genericResourceImageFilePathsByResourceHash(genericResourceImageFilePathsByResourceHash),
    m_pGenericResourceImageManager(pGenericResourceImageManager),
    m_saveResourceImageRequestId(),
    m_filePath(filePath),
    m_data(),
    m_resourceMimeType(),
    m_resource(),
    m_resourceFileStoragePath(),
    m_readResourceFileRequestId(),
    m_saveResourceToStorageRequestId()
{}

AddResourceDelegate::AddResourceDelegate(const QByteArray & resourceData,
                                         const QString & mimeType, NoteEditorPrivate & noteEditor,
                                         ResourceDataInTemporaryFileStorageManager * pResourceFileStorageManager,
                                         FileIOProcessorAsync * pFileIOProcessorAsync,
                                         GenericResourceImageManager * pGenericResourceImageManager,
                                         QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor),
    m_pResourceDataInTemporaryFileStorageManager(pResourceFileStorageManager),
    m_pFileIOProcessorAsync(pFileIOProcessorAsync),
    m_genericResourceImageFilePathsByResourceHash(genericResourceImageFilePathsByResourceHash),
    m_pGenericResourceImageManager(pGenericResourceImageManager),
    m_saveResourceImageRequestId(),
    m_filePath(),
    m_data(resourceData),
    m_resourceMimeType(),
    m_resource(),
    m_resourceFileStoragePath(),
    m_readResourceFileRequestId(),
    m_saveResourceToStorageRequestId()
{
    QMimeDatabase mimeDatabase;
    m_resourceMimeType = mimeDatabase.mimeTypeForName(mimeType);

    if (!m_resourceMimeType.isValid())
    {
        QNDEBUG(QStringLiteral("The mime type deduced from the mime type name ") << mimeType
                << QStringLiteral(" is invalid, trying to deduce the mime type from the raw data"));

        m_resourceMimeType = mimeDatabase.mimeTypeForData(m_data);

        QNDEBUG(QStringLiteral("Mime type deduced from the data is ")
                << (m_resourceMimeType.isValid() ? QStringLiteral("valid") : QStringLiteral("invalid")));
    }
}

void AddResourceDelegate::start()
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::start"));

    if (m_noteEditor.isModified()) {
        QObject::connect(&m_noteEditor, QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                         this, QNSLOT(AddResourceDelegate,onOriginalPageConvertedToNote,Note));
        m_noteEditor.convertToNote();
    }
    else {
        doStart();
    }
}

void AddResourceDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::onOriginalPageConvertedToNote"));

    Q_UNUSED(note)

    QObject::disconnect(&m_noteEditor, QNSIGNAL(NoteEditorPrivate,convertedToNote,Note),
                        this, QNSLOT(AddResourceDelegate,onOriginalPageConvertedToNote,Note));

    doStart();
}

void AddResourceDelegate::doStart()
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::doStart"));

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        ErrorString error(QT_TR_NOOP("Can't add attachment: no note is set to the editor"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    if (m_filePath.isEmpty() && m_data.isEmpty())
    {
        ErrorString error(QT_TR_NOOP("Can't add attachment: the file path of the data to be added is empty "
                                     "and the raw data is empty as well"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }
    else if (m_filePath.isEmpty() && !m_resourceMimeType.isValid())
    {
        ErrorString error(QT_TR_NOOP("Can't add attachment: the mime type of the data to be added is invalid"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    const Account * pAccount = m_noteEditor.accountPtr();

    bool noteHasLimits = pNote->hasNoteLimits();
    if (noteHasLimits)
    {
        QNTRACE(QStringLiteral("Note has its own limits, will use them to check the number of note resources"));

        const qevercloud::NoteLimits & limits = pNote->noteLimits();
        if (limits.noteResourceCountMax.isSet() && (limits.noteResourceCountMax.ref() == pNote->numResources()))
        {
            ErrorString error(QT_TR_NOOP("Can't add attachment: the note is already at max allowed number of attachments"));
            error.details() = QString::number(pNote->numResources());
            Q_EMIT notifyError(error);
            return;
        }
    }
    else if (pAccount)
    {
        QNTRACE(QStringLiteral("Note has no limits of its own, will use the account-wise limits to check the number of note resources"));

        int numNoteResources = pNote->numResources();
        ++numNoteResources;
        if (numNoteResources > pAccount->noteResourceCountMax()) {
            ErrorString error(QT_TR_NOOP("Can't add attachment: the note is already at max allowed number of attachments"));
            error.details() = QString::number(numNoteResources - 1);
            Q_EMIT notifyError(error);
            return;
        }
    }
    else
    {
        QNINFO(QStringLiteral("No account when adding the resource to note, can't check account-wise note limits"));
    }

    if (!m_filePath.isEmpty()) {
        doStartUsingFile();
    }
    else {
        doStartUsingData();
    }
}

void AddResourceDelegate::doStartUsingFile()
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::doStartUsingFile"));

    QFileInfo fileInfo(m_filePath);
    if (!fileInfo.isFile()) {
        QNINFO(QStringLiteral("Detected attempt to drop something else rather than file: ") << m_filePath);
        return;
    }

    if (!fileInfo.isReadable()) {
        QNINFO(QStringLiteral("Detected attempt to drop file which is not readable: ") << m_filePath);
        return;
    }

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        ErrorString error(QT_TR_NOOP("Can't add attachment: no note is set to the editor"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    const Account * pAccount = m_noteEditor.accountPtr();
    const qint64 fileSize = fileInfo.size();
    if (!checkResourceDataSize(*pNote, pAccount, fileSize)) {
        return;
    }

    QMimeDatabase mimeDatabase;
    m_resourceMimeType = mimeDatabase.mimeTypeForFile(fileInfo);
    if (Q_UNLIKELY(!m_resourceMimeType.isValid())) {
        ErrorString error(QT_TR_NOOP("Can't add attachment: the mime type of the resource file is invalid"));
        error.details() = QStringLiteral("file: ");
        error.details() += m_filePath;
        Q_EMIT notifyError(error);
        return;
    }

    m_readResourceFileRequestId = QUuid::createUuid();

    QObject::connect(this, QNSIGNAL(AddResourceDelegate,readFileData,QString,QUuid),
                     m_pFileIOProcessorAsync, QNSLOT(FileIOProcessorAsync,onReadFileRequest,QString,QUuid));
    QObject::connect(m_pFileIOProcessorAsync, QNSIGNAL(FileIOProcessorAsync,readFileRequestProcessed,bool,ErrorString,QByteArray,QUuid),
                     this, QNSLOT(AddResourceDelegate,onResourceFileRead,bool,ErrorString,QByteArray,QUuid));

    Q_EMIT readFileData(m_filePath, m_readResourceFileRequestId);
}

void AddResourceDelegate::onResourceFileRead(bool success, ErrorString errorDescription, QByteArray data, QUuid requestId)
{
    if (requestId != m_readResourceFileRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("AddResourceDelegate::onResourceFileRead: success = ")
            << (success ? QStringLiteral("true") : QStringLiteral("false")));

    QObject::disconnect(this, QNSIGNAL(AddResourceDelegate,readFileData,QString,QUuid),
                        m_pFileIOProcessorAsync, QNSLOT(FileIOProcessorAsync,onReadFileRequest,QString,QUuid));
    QObject::disconnect(m_pFileIOProcessorAsync, QNSIGNAL(FileIOProcessorAsync,readFileRequestProcessed,bool,ErrorString,QByteArray,QUuid),
                        this, QNSLOT(AddResourceDelegate,onResourceFileRead,bool,ErrorString,QByteArray,QUuid));

    if (Q_UNLIKELY(!success)) {
        ErrorString error(QT_TR_NOOP("can't read the contents of the attachment file"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT notifyError(error);
        return;
    }

    QFileInfo fileInfo(m_filePath);
    doSaveResourceToStorage(data, fileInfo.fileName());
}

void AddResourceDelegate::doStartUsingData()
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::doStartUsingData"));

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        ErrorString error(QT_TR_NOOP("Can't add attachment: no note is set to the editor"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!m_resourceMimeType.isValid()))
    {
        ErrorString error(QT_TR_NOOP("Can't add attachment: bad mime type"));

        QString mimeTypeName = m_resourceMimeType.name();
        if (!mimeTypeName.isEmpty()) {
            error.details() = QStringLiteral(": ");
            error.details() += mimeTypeName;
        }

        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    const Account * pAccount = m_noteEditor.accountPtr();
    if (!checkResourceDataSize(*pNote, pAccount, static_cast<qint64>(m_data.size()))) {
        return;
    }

    doSaveResourceToStorage(m_data, QString());
}

void AddResourceDelegate::doSaveResourceToStorage(const QByteArray & data, QString resourceName)
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::doSaveResourceToStorage: resource name = ") << resourceName);

    const Note * pNote = m_noteEditor.notePtr();
    if (!pNote) {
        ErrorString errorDescription(QT_TR_NOOP("Can't save the added resource to local file: no note is set to the editor"));
        QNWARNING(errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    if (resourceName.isEmpty()) {
        resourceName = tr("Attachment");
    }

    QByteArray dataHash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    m_resource = m_noteEditor.attachResourceToNote(data, dataHash, m_resourceMimeType, resourceName);
    QString resourceLocalUid = m_resource.localUid();
    if (Q_UNLIKELY(resourceLocalUid.isEmpty())) {
        return;
    }

    bool isImage = m_resourceMimeType.name().startsWith(QStringLiteral("image/"));
    if (isImage) {
        m_resourceFileStoragePath = m_noteEditor.imageResourcesStoragePath();
    }
    else {
        m_resourceFileStoragePath = m_noteEditor.resourceLocalFileStoragePath();
    }

    m_resourceFileStoragePath += QStringLiteral("/") + pNote->localUid() + QStringLiteral("/") + resourceLocalUid;

    QString fileInfoSuffix;
    if (!m_filePath.isEmpty()) {
        QFileInfo fileInfo(m_filePath);
        fileInfoSuffix = fileInfo.completeSuffix();
    }

    if (fileInfoSuffix.isEmpty())
    {
        const QStringList suffixes = m_resourceMimeType.suffixes();
        if (!suffixes.isEmpty()) {
            fileInfoSuffix = suffixes.front();
        }
    }

    m_saveResourceToStorageRequestId = QUuid::createUuid();

    QObject::connect(this, QNSIGNAL(AddResourceDelegate,saveResourceToStorage,QString,QString,QByteArray,QByteArray,QString,QUuid,bool),
                     m_pResourceDataInTemporaryFileStorageManager, QNSLOT(ResourceDataInTemporaryFileStorageManager,onWriteResourceToFileRequest,QString,QString,QByteArray,QByteArray,QString,QUuid,bool));
    QObject::connect(m_pResourceDataInTemporaryFileStorageManager, QNSIGNAL(ResourceDataInTemporaryFileStorageManager,writeResourceToFileCompleted,QUuid,QByteArray,QString,int,ErrorString),
                     this, QNSLOT(AddResourceDelegate,onResourceSavedToStorage,QUuid,QByteArray,QString,int,ErrorString));

    QNTRACE(QStringLiteral("Emitting the request to save the dropped/pasted resource to local file storage: generated local uid = ")
            << resourceLocalUid << QStringLiteral(", data hash = ") << dataHash.toHex() << QStringLiteral(", request id = ")
            << m_saveResourceToStorageRequestId << QStringLiteral(", mime type name = ") << m_resourceMimeType.name());
    Q_EMIT saveResourceToStorage(pNote->localUid(), resourceLocalUid, data, dataHash, fileInfoSuffix,
                                 m_saveResourceToStorageRequestId, isImage);
}

void AddResourceDelegate::onResourceSavedToStorage(QUuid requestId, QByteArray dataHash,
                                                   QString fileStoragePath, int errorCode,
                                                   ErrorString errorDescription)
{
    if (requestId != m_saveResourceToStorageRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("AddResourceDelegate::onResourceSavedToStorage: error code = ") << errorCode
            << QStringLiteral(", file storage path = ") << fileStoragePath << QStringLiteral(", error description = ")
            << errorDescription);

    m_resourceFileStoragePath = fileStoragePath;

    QObject::disconnect(this, QNSIGNAL(AddResourceDelegate,saveResourceToStorage,QString,QString,QByteArray,QByteArray,QString,QUuid,bool),
                        m_pResourceDataInTemporaryFileStorageManager, QNSLOT(ResourceDataInTemporaryFileStorageManager,onWriteResourceToFileRequest,QString,QString,QByteArray,QByteArray,QString,QUuid,bool));
    QObject::disconnect(m_pResourceDataInTemporaryFileStorageManager, QNSIGNAL(ResourceDataInTemporaryFileStorageManager,writeResourceToFileCompleted,QUuid,QByteArray,QString,int,QString),
                        this, QNSLOT(AddResourceDelegate,onResourceSavedToStorage,QUuid,QByteArray,QString,int,QString));

    if (Q_UNLIKELY(errorCode != 0)) {
        ErrorString error(QT_TR_NOOP("Can't write the resource to local file"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        m_noteEditor.removeResourceFromNote(m_resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (!m_resource.hasDataHash()) {
        m_resource.setDataHash(dataHash);
        m_noteEditor.replaceResourceInNote(m_resource);
    }

    if (m_resourceMimeType.name().startsWith(QStringLiteral("image/"))) {
        QNTRACE(QStringLiteral("Done adding the image resource to the note, moving on to adding it to the page"));
        insertNewResourceHtml();
        return;
    }

    // Otherwise need to build the image for the generic resource
    const Note * pNote = m_noteEditor.notePtr();
    if (!pNote) {
        errorDescription.setBase(QT_TR_NOOP("Can't set up the image corresponding to the resource: "
                                            "no note is set to the editor"));
        QNWARNING(errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    QImage resourceImage = m_noteEditor.buildGenericResourceImage(m_resource);

    QByteArray resourceImageData;
    QBuffer buffer(&resourceImageData);
    Q_UNUSED(buffer.open(QIODevice::WriteOnly));
    resourceImage.save(&buffer, "PNG");

    m_saveResourceImageRequestId = QUuid::createUuid();

    QObject::connect(this, QNSIGNAL(AddResourceDelegate,saveGenericResourceImageToFile,QString,QString,QByteArray,QString,QByteArray,QString,QUuid),
                     m_pGenericResourceImageManager, QNSLOT(GenericResourceImageManager,onGenericResourceImageWriteRequest,QString,QString,QByteArray,QString,QByteArray,QString,QUuid));
    QObject::connect(m_pGenericResourceImageManager, QNSIGNAL(GenericResourceImageManager,genericResourceImageWriteReply,bool,QByteArray,QString,ErrorString,QUuid),
                     this, QNSLOT(AddResourceDelegate,onGenericResourceImageSaved,bool,QByteArray,QString,ErrorString,QUuid));

    QNDEBUG(QStringLiteral("Emitting request to write generic resource image for new resource with local uid ")
            << m_resource.localUid() << QStringLiteral(", request id ") << m_saveResourceImageRequestId
            << QStringLiteral(", note local uid = ") << pNote->localUid());
    Q_EMIT saveGenericResourceImageToFile(pNote->localUid(), m_resource.localUid(), resourceImageData, QStringLiteral("png"),
                                          dataHash, m_resourceFileStoragePath, m_saveResourceImageRequestId);
}

void AddResourceDelegate::onGenericResourceImageSaved(bool success, QByteArray resourceImageDataHash,
                                                      QString filePath, ErrorString errorDescription,
                                                      QUuid requestId)
{
    if (requestId != m_saveResourceImageRequestId) {
        return;
    }

    QObject::disconnect(this, QNSIGNAL(AddResourceDelegate,saveGenericResourceImageToFile,QString,QString,QByteArray,QByteArray,QString,QUuid),
                        m_pGenericResourceImageManager, QNSLOT(GenericResourceImageManager,onGenericResourceImageWriteRequest,QString,QString,QByteArray,QByteArray,QString,QUuid));
    QObject::disconnect(m_pGenericResourceImageManager, QNSIGNAL(GenericResourceImageManager,genericResourceImageWriteReply,bool,QByteArray,QString,ErrorString,QUuid),
                        this, QNSLOT(AddResourceDelegate,onGenericResourceImageSaved,bool,QByteArray,QString,ErrorString,QUuid));

    QNDEBUG(QStringLiteral("AddResourceDelegate::onGenericResourceImageSaved: success = ")
            << (success ? QStringLiteral("true") : QStringLiteral("false"))
            << QStringLiteral(", file path = ") << filePath);

    m_genericResourceImageFilePathsByResourceHash[m_resource.dataHash()] = filePath;
    QNDEBUG(QStringLiteral("Cached generic resource image file path ") << filePath << QStringLiteral(" for resource hash ")
            << m_resource.dataHash().toHex());

    Q_UNUSED(resourceImageDataHash);

    if (Q_UNLIKELY(!success)) {
        ErrorString error(QT_TR_NOOP("Can't write the image representing the resource to local file"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        m_noteEditor.removeResourceFromNote(m_resource);
        Q_EMIT notifyError(error);
        return;
    }

    insertNewResourceHtml();
}

void AddResourceDelegate::insertNewResourceHtml()
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::insertNewResourceHtml"));

    ErrorString errorDescription;
    QString resourceHtml = ENMLConverter::resourceHtml(m_resource, errorDescription);
    if (Q_UNLIKELY(resourceHtml.isEmpty())) {
        ErrorString error(QT_TR_NOOP("Can't compose the html representation of the attachment"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING(error);
        m_noteEditor.removeResourceFromNote(m_resource);
        Q_EMIT notifyError(error);
        return;
    }

    QNTRACE(QStringLiteral("Resource html: ") << resourceHtml);

    GET_PAGE()
    page->executeJavaScript(QStringLiteral("resourceManager.addResource('") + resourceHtml + QStringLiteral("');"),
                            JsCallback(*this, &AddResourceDelegate::onNewResourceHtmlInserted));
}

void AddResourceDelegate::onNewResourceHtmlInserted(const QVariant & data)
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::onNewResourceHtmlInserted"));

    QMap<QString,QVariant> resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(QT_TR_NOOP("Can't parse the result of new resource html insertion from JavaScript"));
        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res)
    {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(QT_TR_NOOP("Can't parse the error of new resource html insertion from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't insert resource html into the note editor"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING(error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished(m_resource, m_resourceFileStoragePath);
}

bool AddResourceDelegate::checkResourceDataSize(const Note & note, const Account * pAccount, const qint64 size)
{
    QNDEBUG(QStringLiteral("AddResourceDelegate::checkResourceDataSize: size = ") << size);

    bool noteHasLimits = note.hasNoteLimits();
    if (noteHasLimits)
    {
        const qevercloud::NoteLimits & limits = note.noteLimits();
        bool violatesNoteResourceSizeMax = limits.resourceSizeMax.isSet() && (size > limits.resourceSizeMax.ref());

        if (Q_UNLIKELY(violatesNoteResourceSizeMax))
        {
            ErrorString error(QT_TR_NOOP("Can't add attachment: the resource to be added "
                                         "is too large, max resource size allowed is"));
            error.details() = humanReadableSize(static_cast<quint64>(note.noteLimits().resourceSizeMax.ref()));
            Q_EMIT notifyError(error);
            return false;
        }

        const qint64 previousNoteSize = m_noteEditor.noteSize();
        bool violatesNoteSizeMax = limits.noteSizeMax.isSet() && limits.noteSizeMax.ref() > (previousNoteSize + size);
        if (violatesNoteSizeMax)
        {
            ErrorString error(QT_TR_NOOP("Can't add attachment: the addition of the resource "
                                         "would violate the max resource size which is"));
            error.details() = humanReadableSize(static_cast<quint64>(note.noteLimits().noteSizeMax.ref()));
            Q_EMIT notifyError(error);
            return false;
        }
    }
    else if (pAccount)
    {
        bool violatesNoteResourceSizeMax = (size > pAccount->resourceSizeMax());

        if (Q_UNLIKELY(violatesNoteResourceSizeMax))
        {
            ErrorString error(QT_TR_NOOP("Can't add attachment: the resource is too large, "
                                         "max resource size allowed is"));
            error.details() = humanReadableSize(static_cast<quint64>(pAccount->resourceSizeMax()));
            Q_EMIT notifyError(error);
            return false;
        }
    }

    return true;
}

} // namespace quentier
