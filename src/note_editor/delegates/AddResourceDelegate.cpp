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

#include "AddResourceDelegate.h"

#include "../GenericResourceImageManager.h"
#include "../NoteEditorPage.h"
#include "../NoteEditor_p.h"
#include "../ResourceDataInTemporaryFileStorageManager.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Account.h>
#include <quentier/types/Resource.h>
#include <quentier/utility/FileIOProcessorAsync.h>
#include <quentier/utility/Size.h>

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QImage>
#include <QMimeDatabase>

#ifndef QUENTIER_USE_QT_WEB_ENGINE
#include <QWebFrame>
#endif

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditor.page());         \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "AddResourceDelegate",                                             \
            "Can't add attachment: no note editor page"));                     \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

AddResourceDelegate::AddResourceDelegate(
    const QString & filePath, NoteEditorPrivate & noteEditor,
    ResourceDataInTemporaryFileStorageManager * pResourceFileStorageManager,
    FileIOProcessorAsync * pFileIOProcessorAsync,
    GenericResourceImageManager * pGenericResourceImageManager,
    QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor),
    m_pResourceDataInTemporaryFileStorageManager(pResourceFileStorageManager),
    m_pFileIOProcessorAsync(pFileIOProcessorAsync),
    m_genericResourceImageFilePathsByResourceHash(
        genericResourceImageFilePathsByResourceHash),
    m_pGenericResourceImageManager(pGenericResourceImageManager),
    m_filePath(filePath)
{}

AddResourceDelegate::AddResourceDelegate(
    const QByteArray & resourceData, const QString & mimeType,
    NoteEditorPrivate & noteEditor,
    ResourceDataInTemporaryFileStorageManager * pResourceFileStorageManager,
    FileIOProcessorAsync * pFileIOProcessorAsync,
    GenericResourceImageManager * pGenericResourceImageManager,
    QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash) :
    QObject(&noteEditor),
    m_noteEditor(noteEditor),
    m_pResourceDataInTemporaryFileStorageManager(pResourceFileStorageManager),
    m_pFileIOProcessorAsync(pFileIOProcessorAsync),
    m_genericResourceImageFilePathsByResourceHash(
        genericResourceImageFilePathsByResourceHash),
    m_pGenericResourceImageManager(pGenericResourceImageManager),
    m_data(resourceData)
{
    QMimeDatabase mimeDatabase;
    m_resourceMimeType = mimeDatabase.mimeTypeForName(mimeType);

    if (!m_resourceMimeType.isValid()) {
        QNDEBUG(
            "note_editor:delegate",
            "Mime type deduced from the mime type "
                << "name " << mimeType
                << " is invalid, trying to deduce mime type "
                << "from the raw data");

        m_resourceMimeType = mimeDatabase.mimeTypeForData(m_data);

        QNDEBUG(
            "note_editor:delegate",
            "Mime type deduced from the data is "
                << (m_resourceMimeType.isValid() ? "valid" : "invalid"));
    }
}

void AddResourceDelegate::start()
{
    QNDEBUG("note_editor:delegate", "AddResourceDelegate::start");

    if (m_noteEditor.isEditorPageModified()) {
        QObject::connect(
            &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
            &AddResourceDelegate::onOriginalPageConvertedToNote);

        m_noteEditor.convertToNote();
    }
    else {
        doStart();
    }
}

void AddResourceDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(
        &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
        &AddResourceDelegate::onOriginalPageConvertedToNote);

    doStart();
}

void AddResourceDelegate::doStart()
{
    QNDEBUG("note_editor:delegate", "AddResourceDelegate::doStart");

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't add attachment: no note is set to the editor"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (m_filePath.isEmpty() && m_data.isEmpty()) {
        ErrorString error(
            QT_TR_NOOP("Can't add attachment: the file path of the data to be "
                       "added is empty and the raw data is empty as well"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }
    else if (m_filePath.isEmpty() && !m_resourceMimeType.isValid()) {
        ErrorString error(
            QT_TR_NOOP("Can't add attachment: the mime type of "
                       "the data to be added is invalid"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    const Account * pAccount = m_noteEditor.accountPtr();

    bool noteHasLimits = pNote->hasNoteLimits();
    if (noteHasLimits) {
        QNTRACE(
            "note_editor:delegate",
            "Note has its own limits, will use "
                << "them to check the number of note resources");

        const auto & limits = pNote->noteLimits();
        if (limits.noteResourceCountMax.isSet() &&
            (limits.noteResourceCountMax.ref() == pNote->numResources()))
        {
            ErrorString error(
                QT_TR_NOOP("Can't add attachment: the note is already "
                           "at max allowed number of attachments"));
            error.details() = QString::number(pNote->numResources());
            Q_EMIT notifyError(error);
            return;
        }
    }
    else if (pAccount) {
        QNTRACE(
            "note_editor:delegate",
            "Note has no limits of its own, will "
                << "use account-wise limits to check the number of note "
                   "resources");

        int numNoteResources = pNote->numResources();
        ++numNoteResources;
        if (numNoteResources > pAccount->noteResourceCountMax()) {
            ErrorString error(
                QT_TR_NOOP("Can't add attachment: the note is already "
                           "at max allowed number of attachments"));
            error.details() = QString::number(numNoteResources - 1);
            Q_EMIT notifyError(error);
            return;
        }
    }
    else {
        QNINFO(
            "note_editor:delegate",
            "No account when adding the resource "
                << "to note, can't check account-wise note limits");
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
    QNDEBUG("note_editor:delegate", "AddResourceDelegate::doStartUsingFile");

    QFileInfo fileInfo(m_filePath);
    if (!fileInfo.isFile()) {
        QNINFO(
            "note_editor:delegate",
            "Detected attempt to drop something "
                << "else rather than file: " << m_filePath);
        return;
    }

    if (!fileInfo.isReadable()) {
        QNINFO(
            "note_editor:delegate",
            "Detected attempt to drop file which "
                << "is not readable: " << m_filePath);
        return;
    }

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't add attachment: no note is set to the editor"));
        QNWARNING("note_editor:delegate", error);
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
        ErrorString error(
            QT_TR_NOOP("Can't add attachment: the mime type of "
                       "the resource file is invalid"));
        error.details() = QStringLiteral("file: ");
        error.details() += m_filePath;
        Q_EMIT notifyError(error);
        return;
    }

    m_readResourceFileRequestId = QUuid::createUuid();

    QObject::connect(
        this, &AddResourceDelegate::readFileData, m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::onReadFileRequest);

    QObject::connect(
        m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::readFileRequestProcessed, this,
        &AddResourceDelegate::onResourceFileRead);

    Q_EMIT readFileData(m_filePath, m_readResourceFileRequestId);
}

void AddResourceDelegate::onResourceFileRead(
    bool success, ErrorString errorDescription, QByteArray data,
    QUuid requestId)
{
    if (requestId != m_readResourceFileRequestId) {
        return;
    }

    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate::onResourceFileRead: "
            << "success = " << (success ? "true" : "false"));

    QObject::disconnect(
        this, &AddResourceDelegate::readFileData, m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::onReadFileRequest);

    QObject::disconnect(
        m_pFileIOProcessorAsync,
        &FileIOProcessorAsync::readFileRequestProcessed, this,
        &AddResourceDelegate::onResourceFileRead);

    if (Q_UNLIKELY(!success)) {
        ErrorString error(
            QT_TR_NOOP("can't read the attachment file contents"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT notifyError(error);
        return;
    }

    QFileInfo fileInfo(m_filePath);

    if (m_resourceMimeType.name().startsWith(QStringLiteral("image/"))) {
        doSaveResourceDataToTemporaryFile(data, fileInfo.fileName());
    }
    else {
        doGenerateGenericResourceImage(data, fileInfo.fileName());
    }
}

void AddResourceDelegate::doStartUsingData()
{
    QNDEBUG("note_editor:delegate", "AddResourceDelegate::doStartUsingData");

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        ErrorString error(
            QT_TR_NOOP("Can't add attachment: no note is set to the editor"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!m_resourceMimeType.isValid())) {
        ErrorString error(QT_TR_NOOP("Can't add attachment: bad mime type"));

        QString mimeTypeName = m_resourceMimeType.name();
        if (!mimeTypeName.isEmpty()) {
            error.details() = QStringLiteral(": ");
            error.details() += mimeTypeName;
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = checkResourceDataSize(
        *pNote, m_noteEditor.accountPtr(), static_cast<qint64>(m_data.size()));

    if (!res) {
        return;
    }

    if (m_resourceMimeType.name().startsWith(QStringLiteral("image/"))) {
        doSaveResourceDataToTemporaryFile(m_data, QString());
    }
    else {
        doGenerateGenericResourceImage(m_data, QString());
    }
}

void AddResourceDelegate::doSaveResourceDataToTemporaryFile(
    const QByteArray & data, QString resourceName)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::doSaveResourceDataToTemporaryFile: resource name = "
            << resourceName);

    const Note * pNote = m_noteEditor.notePtr();
    if (!pNote) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't save the added resource to a temporary file: "
                       "no note is set to the editor"));
        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    if (resourceName.isEmpty()) {
        resourceName = tr("Attachment");
    }

    QByteArray dataHash =
        QCryptographicHash::hash(data, QCryptographicHash::Md5);

    m_resource = m_noteEditor.attachResourceToNote(
        data, dataHash, m_resourceMimeType, resourceName);

    QNTRACE(
        "note_editor:delegate", "Attached resource to note: " << m_resource);

    QString resourceLocalUid = m_resource.localUid();
    if (Q_UNLIKELY(resourceLocalUid.isEmpty())) {
        return;
    }

    // NOTE: only image resources data gets saved to temporary files

    m_saveResourceDataToTemporaryFileRequestId = QUuid::createUuid();

    QObject::connect(
        this, &AddResourceDelegate::saveResourceDataToTemporaryFile,
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            onSaveResourceDataToTemporaryFileRequest);

    QObject::connect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            saveResourceDataToTemporaryFileCompleted,
        this, &AddResourceDelegate::onResourceDataSavedToTemporaryFile);

    QNTRACE(
        "note_editor:delegate",
        "Emitting the request to save "
            << "the dropped/pasted resource to a temporary file: generated "
               "local "
            << "uid = " << resourceLocalUid
            << ", data hash = " << dataHash.toHex()
            << ", request id = " << m_saveResourceDataToTemporaryFileRequestId
            << ", mime type name = " << m_resourceMimeType.name());

    Q_EMIT saveResourceDataToTemporaryFile(
        pNote->localUid(), resourceLocalUid, data, dataHash,
        m_saveResourceDataToTemporaryFileRequestId,
        /* is image = */ true);
}

void AddResourceDelegate::onResourceDataSavedToTemporaryFile(
    QUuid requestId, QByteArray dataHash, ErrorString errorDescription)
{
    if (requestId != m_saveResourceDataToTemporaryFileRequestId) {
        return;
    }

    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::onResourceDataSavedToTemporaryFile: error description = "
            << errorDescription);

    const Note * pNote = m_noteEditor.notePtr();
    if (!pNote) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't set up the image corresponding "
                       "to the resource: no note is set to the editor"));
        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    m_resourceFileStoragePath = ResourceDataInTemporaryFileStorageManager::
        imageResourceFileStorageFolderPath();

    m_resourceFileStoragePath += QStringLiteral("/") + pNote->localUid() +
        QStringLiteral("/") + m_resource.localUid() + QStringLiteral(".dat");

    QObject::disconnect(
        this, &AddResourceDelegate::saveResourceDataToTemporaryFile,
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            onSaveResourceDataToTemporaryFileRequest);

    QObject::disconnect(
        m_pResourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            saveResourceDataToTemporaryFileCompleted,
        this, &AddResourceDelegate::onResourceDataSavedToTemporaryFile);

    if (Q_UNLIKELY(!errorDescription.isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't write the resource data to a temporary file"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING("note_editor:delegate", error);
        m_noteEditor.removeResourceFromNote(m_resource);
        Q_EMIT notifyError(error);
        return;
    }

    if (!m_resource.hasDataHash()) {
        m_resource.setDataHash(dataHash);
        m_noteEditor.replaceResourceInNote(m_resource);
    }

    QNTRACE(
        "note_editor:delegate",
        "Done adding the image resource to "
            << "the note, moving on to adding it to the page");

    insertNewResourceHtml();
}

void AddResourceDelegate::onGenericResourceImageSaved(
    bool success, QByteArray resourceImageDataHash, QString filePath,
    ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_saveResourceImageRequestId) {
        return;
    }

    QObject::disconnect(
        this, &AddResourceDelegate::saveGenericResourceImageToFile,
        m_pGenericResourceImageManager,
        &GenericResourceImageManager::onGenericResourceImageWriteRequest);

    QObject::disconnect(
        m_pGenericResourceImageManager,
        &GenericResourceImageManager::genericResourceImageWriteReply, this,
        &AddResourceDelegate::onGenericResourceImageSaved);

    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::onGenericResourceImageSaved: success = "
            << (success ? "true" : "false") << ", file path = " << filePath);

    m_genericResourceImageFilePathsByResourceHash[m_resource.dataHash()] =
        filePath;

    QNDEBUG(
        "note_editor:delegate",
        "Cached generic resource image file path "
            << filePath << " for resource hash "
            << m_resource.dataHash().toHex());

    Q_UNUSED(resourceImageDataHash);

    if (Q_UNLIKELY(!success)) {
        ErrorString error(
            QT_TR_NOOP("Can't write the image representing "
                       "the resource to a temporary file"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING("note_editor:delegate", error);
        m_noteEditor.removeResourceFromNote(m_resource);
        Q_EMIT notifyError(error);
        return;
    }

    insertNewResourceHtml();
}

void AddResourceDelegate::doGenerateGenericResourceImage(
    const QByteArray & data, QString resourceName)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::doGenerateGenericResourceImage");

    const Note * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't set up the image corresponding "
                       "to the resource: no note is set to the editor"));
        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    m_resourceFileStoragePath = ResourceDataInTemporaryFileStorageManager::
        nonImageResourceFileStorageFolderPath();

    m_resourceFileStoragePath += QStringLiteral("/") + pNote->localUid() +
        QStringLiteral("/") + m_resource.localUid() + QStringLiteral(".dat");

    if (resourceName.isEmpty()) {
        resourceName = tr("Attachment");
    }

    QByteArray dataHash =
        QCryptographicHash::hash(data, QCryptographicHash::Md5);

    m_resource = m_noteEditor.attachResourceToNote(
        data, dataHash, m_resourceMimeType, resourceName);

    QString resourceLocalUid = m_resource.localUid();
    if (Q_UNLIKELY(resourceLocalUid.isEmpty())) {
        return;
    }

    QImage resourceImage = m_noteEditor.buildGenericResourceImage(m_resource);

    QByteArray resourceImageData;
    QBuffer buffer(&resourceImageData);
    Q_UNUSED(buffer.open(QIODevice::WriteOnly));
    resourceImage.save(&buffer, "PNG");

    m_saveResourceImageRequestId = QUuid::createUuid();

    QObject::connect(
        this, &AddResourceDelegate::saveGenericResourceImageToFile,
        m_pGenericResourceImageManager,
        &GenericResourceImageManager::onGenericResourceImageWriteRequest);

    QObject::connect(
        m_pGenericResourceImageManager,
        &GenericResourceImageManager::genericResourceImageWriteReply, this,
        &AddResourceDelegate::onGenericResourceImageSaved);

    QNDEBUG(
        "note_editor:delegate",
        "Emitting request to write generic "
            << "resource image for new resource with local uid "
            << m_resource.localUid() << ", request id "
            << m_saveResourceImageRequestId
            << ", note local uid = " << pNote->localUid());

    Q_EMIT saveGenericResourceImageToFile(
        pNote->localUid(), m_resource.localUid(), resourceImageData,
        QStringLiteral("png"), dataHash, m_resourceFileStoragePath,
        m_saveResourceImageRequestId);
}

void AddResourceDelegate::insertNewResourceHtml()
{
    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::insertNewResourceHtml");

    ErrorString errorDescription;
    QString resourceHtml =
        ENMLConverter::resourceHtml(m_resource, errorDescription);

    if (Q_UNLIKELY(resourceHtml.isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't compose the html representation of "
                       "the attachment"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING("note_editor:delegate", error);
        m_noteEditor.removeResourceFromNote(m_resource);
        Q_EMIT notifyError(error);
        return;
    }

    QNTRACE("note_editor:delegate", "Resource html: " << resourceHtml);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("resourceManager.addResource('") + resourceHtml +
            QStringLiteral("');"),
        JsCallback(*this, &AddResourceDelegate::onNewResourceHtmlInserted));
}

void AddResourceDelegate::onNewResourceHtmlInserted(const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::onNewResourceHtmlInserted");

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of new resource "
                       "html insertion from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of new resource html "
                           "insertion from JavaScript"));
        }
        else {
            error.setBase(
                QT_TR_NOOP("Can't insert resource html into the note editor"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished(m_resource, m_resourceFileStoragePath);
}

bool AddResourceDelegate::checkResourceDataSize(
    const Note & note, const Account * pAccount, const qint64 size)
{
    QNDEBUG(
        "note_editor:delegate",
        "AddResourceDelegate"
            << "::checkResourceDataSize: size = "
            << humanReadableSize(
                   static_cast<quint64>(std::max(size, qint64(0)))));

    bool noteHasLimits = note.hasNoteLimits();
    if (noteHasLimits) {
        const auto & limits = note.noteLimits();
        bool violatesNoteResourceSizeMax = limits.resourceSizeMax.isSet() &&
            (size > limits.resourceSizeMax.ref());

        if (Q_UNLIKELY(violatesNoteResourceSizeMax)) {
            ErrorString error(
                QT_TR_NOOP("Can't add attachment: the resource to be added is "
                           "too large, max resource size allowed is"));
            error.details() = humanReadableSize(
                static_cast<quint64>(note.noteLimits().resourceSizeMax.ref()));
            Q_EMIT notifyError(error);
            return false;
        }

        const qint64 previousNoteSize = m_noteEditor.noteSize();
        bool violatesNoteSizeMax = limits.noteSizeMax.isSet() &&
            (limits.noteSizeMax.ref() > (previousNoteSize + size));

        if (violatesNoteSizeMax) {
            ErrorString error(QT_TR_NOOP(
                "Can't add attachment: the addition of the resource :"
                "would violate the max resource size which is"));
            error.details() = humanReadableSize(
                static_cast<quint64>(note.noteLimits().noteSizeMax.ref()));
            Q_EMIT notifyError(error);
            return false;
        }
    }
    else if (pAccount) {
        bool violatesNoteResourceSizeMax = (size > pAccount->resourceSizeMax());

        if (Q_UNLIKELY(violatesNoteResourceSizeMax)) {
            ErrorString error(
                QT_TR_NOOP("Can't add attachment: the resource is "
                           "too large, max resource size allowed is"));
            error.details() = humanReadableSize(
                static_cast<quint64>(pAccount->resourceSizeMax()));
            Q_EMIT notifyError(error);
            return false;
        }
    }

    return true;
}

} // namespace quentier
