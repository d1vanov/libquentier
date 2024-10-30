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

#include "ResourceDataInTemporaryFileStorageManager.h"

#include "NoteEditorLocalStorageBroker.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/Size.h>
#include <quentier/utility/StandardPaths.h>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <utility>

// 4 megabytes
#define RESOURCE_DATA_BATCH_SIZE_IN_BYTES (4194304)

namespace quentier {

ResourceDataInTemporaryFileStorageManager::
    ResourceDataInTemporaryFileStorageManager(QObject * parent) :
    QObject(parent), m_nonImageResourceFileStorageLocation(
                         nonImageResourceFileStorageFolderPath()),
    m_imageResourceFileStorageLocation(imageResourceFileStorageFolderPath())
{
    createConnections();
}

QString ResourceDataInTemporaryFileStorageManager::
    imageResourceFileStorageFolderPath()
{
    return applicationTemporaryStoragePath() +
        QStringLiteral("/resources/image");
}

QString ResourceDataInTemporaryFileStorageManager::
    nonImageResourceFileStorageFolderPath()
{
    return applicationTemporaryStoragePath() +
        QStringLiteral("/resources/non-image");
}

void ResourceDataInTemporaryFileStorageManager::
    onSaveResourceDataToTemporaryFileRequest(
        QString noteLocalId, QString resourceLocalId, QByteArray data, // NOLINT
        QByteArray dataHash, QUuid requestId, bool isImage)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::onSaveResourceDataToTemporaryFileRequest: "
            << "note local id = " << noteLocalId << ", resource local id = "
            << resourceLocalId << ", request id = " << requestId
            << ", data hash = " << dataHash.toHex()
            << ", is image = " << (isImage ? "true" : "false"));

    if (dataHash.isEmpty()) {
        dataHash = calculateHash(data);
    }

    ErrorString errorDescription;
    bool res = writeResourceDataToTemporaryFile(
        noteLocalId, resourceLocalId, data, dataHash,
        (isImage ? ResourceType::Image : ResourceType::NonImage),
        errorDescription);

    if (!res) {
        Q_EMIT saveResourceDataToTemporaryFileCompleted(
            requestId, dataHash, errorDescription);
        return;
    }

    QNDEBUG(
        "note_editor",
        "Successfully wrote resource data to file: " << "resource local id = "
                                                     << resourceLocalId);

    Q_EMIT saveResourceDataToTemporaryFileCompleted(
        requestId, dataHash, ErrorString());
}

void ResourceDataInTemporaryFileStorageManager::onReadResourceFromFileRequest(
    QString fileStoragePath, QString resourceLocalId, // NOLINT
    QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::onReadResourceFromFileRequest: resource local id = "
            << resourceLocalId << ", request id = " << requestId);

    if (Q_UNLIKELY(m_nonImageResourceFileStorageLocation.isEmpty())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Resource file storage location is empty"));

        QNWARNING(
            "note_editor",
            errorDescription << ", resource local id = " << resourceLocalId
                             << ", request id = " << requestId);

        Q_EMIT readResourceFromFileCompleted(
            requestId, QByteArray(), QByteArray(),
            static_cast<int>(Error::NoResourceFileStorageLocation),
            errorDescription);
        return;
    }

    QFile resourceFile(fileStoragePath);
    if (Q_UNLIKELY(!resourceFile.open(QIODevice::ReadOnly))) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't open resource file for reading"));

        errorDescription.details() = resourceFile.errorString();
        int errorCode = resourceFile.error();
        QNWARNING(
            "note_editor",
            errorDescription << ", error code = " << errorCode
                             << ", resource local id = " << resourceLocalId
                             << ", request id = " << requestId);

        Q_EMIT readResourceFromFileCompleted(
            requestId, QByteArray(), QByteArray(), errorCode, errorDescription);
        return;
    }

    QFileInfo resourceFileInfo(fileStoragePath);

    QFile resourceHashFile(
        resourceFileInfo.absolutePath() + QStringLiteral("/") +
        resourceLocalId + QStringLiteral(".hash"));

    if (Q_UNLIKELY(!resourceHashFile.open(QIODevice::ReadOnly))) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't open resource hash file for reading"));

        errorDescription.details() = resourceHashFile.errorString();
        int errorCode = resourceHashFile.error();

        QNWARNING(
            "note_editor",
            errorDescription << ", error code = " << errorCode
                             << ", resource local id = " << resourceLocalId
                             << ", request id = " << requestId);

        Q_EMIT readResourceFromFileCompleted(
            requestId, QByteArray(), QByteArray(), errorCode, errorDescription);
        return;
    }

    const QByteArray data = resourceFile.readAll();
    const QByteArray dataHash = resourceHashFile.readAll();

    QNDEBUG(
        "note_editor", "Successfully read resource data and hash from files");

    Q_EMIT readResourceFromFileCompleted(
        requestId, data, dataHash, 0, ErrorString());
}

void ResourceDataInTemporaryFileStorageManager::onOpenResourceRequest(
    QString resourceLocalId) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::onOpenResourceRequest: resource local id = "
            << resourceLocalId);

    if (Q_UNLIKELY(!m_pCurrentNote)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't open the resource in external editor: internal "
                       "error, no note is set to "
                       "ResourceDataInTemporaryFileStorageManager"));

        errorDescription.details() =
            QStringLiteral("resource local id = ") + resourceLocalId;

        QNWARNING("note_editor", errorDescription);

        Q_EMIT failedToOpenResource(
            resourceLocalId, QString(), errorDescription);

        return;
    }

    const QString noteLocalId = m_pCurrentNote->localId();

    const auto resources =
        (m_pCurrentNote->resources() ? *m_pCurrentNote->resources()
                                     : QList<qevercloud::Resource>());
    const qevercloud::Resource * pResource = nullptr;
    for (const auto & resource: std::as_const(resources)) {
        if (resource.localId() == resourceLocalId) {
            pResource = &resource;
            break;
        }
    }

    if (Q_UNLIKELY(!pResource)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't open the resource in external editor: internal "
                       "error, failed to find the resource within the note"));

        errorDescription.details() =
            QStringLiteral("resource local id = ") + resourceLocalId;

        QNWARNING("note_editor", errorDescription);

        Q_EMIT failedToOpenResource(
            resourceLocalId, noteLocalId, errorDescription);
        return;
    }

    if (Q_UNLIKELY(!pResource->mime())) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't open the resource in external editor: "
                       "resource has no mime type"));

        errorDescription.details() =
            QStringLiteral("resource local id = ") + resourceLocalId;

        QNWARNING(
            "note_editor", errorDescription << ", resource: " << *pResource);

        Q_EMIT failedToOpenResource(
            resourceLocalId, noteLocalId, errorDescription);
        return;
    }

    const QString & mime = *pResource->mime();
    const bool isImageResource = mime.startsWith(QStringLiteral("image"));

    QString fileStoragePath =
        (isImageResource ? m_imageResourceFileStorageLocation
                         : m_nonImageResourceFileStorageLocation);

    fileStoragePath += QStringLiteral("/") + noteLocalId + QStringLiteral("/") +
        resourceLocalId + QStringLiteral(".dat");

    if (pResource->data() && pResource->data()->bodyHash() &&
        checkIfResourceFileExistsAndIsActual(
            noteLocalId, resourceLocalId, fileStoragePath,
            *pResource->data()->bodyHash()))
    {
        QNDEBUG(
            "note_editor",
            "Temporary file for resource local id "
                << resourceLocalId << " already exists and is actual");

        m_resourceLocalIdByFilePath[fileStoragePath] = resourceLocalId;
        watchResourceFileForChanges(resourceLocalId, fileStoragePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileStoragePath));
        Q_EMIT openedResource(resourceLocalId, noteLocalId);
        return;
    }

    if (!pResource->data() || !pResource->data()->body()) {
        Q_UNUSED(
            m_resourceLocalIdsPendingFindInLocalStorageForWritingToFileForOpening
                .insert(resourceLocalId))
        requestResourceDataFromLocalStorage(*pResource);
        return;
    }

    const QByteArray dataHash =
        (pResource->data() && pResource->data()->bodyHash())
        ? *pResource->data()->bodyHash()
        : calculateHash(*pResource->data()->body());

    WriteResourceDataCallback callback =
        OpenResourcePreparationProgressFunctor(resourceLocalId, *this);

    ErrorString errorDescription;
    const bool res = writeResourceDataToTemporaryFile(
        noteLocalId, resourceLocalId, *pResource->data()->body(), dataHash,
        (isImageResource ? ResourceType::Image : ResourceType::NonImage),
        errorDescription, CheckResourceFileActualityOption::On, callback);

    if (!res) {
        Q_EMIT failedToOpenResource(
            resourceLocalId, noteLocalId, errorDescription);
        return;
    }

    watchResourceFileForChanges(resourceLocalId, fileStoragePath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileStoragePath));
    Q_EMIT openedResource(resourceLocalId, noteLocalId);
}

void ResourceDataInTemporaryFileStorageManager::onCurrentNoteChanged(
    qevercloud::Note note)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::onCurrentNoteChanged; new note local id = " << note.localId()
            << ", previous note local id = "
            << (m_pCurrentNote ? m_pCurrentNote->localId()
                               : QStringLiteral("<null>")));

    if (m_pCurrentNote && (m_pCurrentNote->localId() == note.localId())) {
        QNTRACE(
            "note_editor",
            "The current note is the same, only the note "
                << "object might have changed");

        const QList<qevercloud::Resource> previousResources =
            (m_pCurrentNote->resources() ? *m_pCurrentNote->resources()
                                         : QList<qevercloud::Resource>());

        *m_pCurrentNote = note;

        ErrorString errorDescription;
        const ResultType res = partialUpdateResourceFilesForCurrentNote(
            previousResources, errorDescription);
        if (res == ResultType::Error) {
            Q_EMIT noteResourcesPreparationError(
                m_pCurrentNote->localId(), errorDescription);
        }
        else if (res == ResultType::Ready) {
            Q_EMIT noteResourcesReady(m_pCurrentNote->localId());
        }

        return;
    }

    for (const auto it:
         qevercloud::toRange(std::as_const(m_resourceLocalIdByFilePath)))
    {
        m_fileSystemWatcher.removePath(it.key());
        QNTRACE("note_editor", "Stopped watching for file " << it.key());
    }
    m_resourceLocalIdByFilePath.clear();

    if (!m_pCurrentNote) {
        m_pCurrentNote = std::make_unique<qevercloud::Note>(std::move(note));
    }
    else {
        *m_pCurrentNote = std::move(note);
    }

    if (!m_pCurrentNote->resources() || m_pCurrentNote->resources()->empty()) {
        QNTRACE(
            "note_editor",
            "Current note has no resources, emitting noteResourcesReady");
        Q_EMIT noteResourcesReady(m_pCurrentNote->localId());
        return;
    }

    QList<qevercloud::Resource> imageResources;
    const auto resources = *m_pCurrentNote->resources();
    for (const auto & resource: std::as_const(resources)) {
        if (!resource.mime() ||
            !resource.mime()->startsWith(QStringLiteral("image")))
        {
            continue;
        }

        imageResources << resource;
        QNDEBUG(
            "note_editor",
            "Will process image resource with local id " << resource.localId());
    }

    if (imageResources.isEmpty()) {
        Q_EMIT noteResourcesReady(m_pCurrentNote->localId());
        return;
    }

    ErrorString errorDescription;

    const ResultType res =
        putResourcesDataToTemporaryFiles(imageResources, errorDescription);

    if (res == ResultType::Error) {
        Q_EMIT noteResourcesPreparationError(
            m_pCurrentNote->localId(), errorDescription);
    }
    else if (res == ResultType::Ready) {
        Q_EMIT noteResourcesReady(m_pCurrentNote->localId());
    }
}

void ResourceDataInTemporaryFileStorageManager::onRequestDiagnostics(
    QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::onRequestDiagnostics: request id = " << requestId);

    QString diagnostics;
    QTextStream strm{&diagnostics};

    strm << "ResourceDataInTemporaryFileStorageManager diagnostics: {\n";

    strm << "  Resource local ids by file paths: \n";
    for (const auto it:
         qevercloud::toRange(std::as_const(m_resourceLocalIdByFilePath)))
    {
        strm << "    [" << it.key() << "]: " << it.value() << "\n";
    }

    strm << "  Watched files: \n";
    const QStringList watchedFiles = m_fileSystemWatcher.files();
    for (const auto & watchedFile: std::as_const(watchedFiles)) {
        strm << "    " << watchedFile << "\n";
    }

    strm << "}\n";
    strm.flush();

    Q_EMIT diagnosticsCollected(requestId, diagnostics);
}

void ResourceDataInTemporaryFileStorageManager::onFileChanged(
    const QString & path)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager" << "::onFileChanged: "
                                                    << path);

    const auto it = m_resourceLocalIdByFilePath.find(path);

    const QFileInfo resourceFileInfo(path);
    if (!resourceFileInfo.exists()) {
        if (it != m_resourceLocalIdByFilePath.end()) {
            Q_UNUSED(m_resourceLocalIdByFilePath.erase(it));
        }

        m_fileSystemWatcher.removePath(path);
        QNINFO(
            "note_editor",
            "Stopped watching for file " << path << " as it was deleted");

        return;
    }

    if (Q_UNLIKELY(it == m_resourceLocalIdByFilePath.end())) {
        QNWARNING(
            "note_editor",
            "Can't process resource local file change "
                << "properly: can't find resource local id by file path: "
                << path << "; stopped watching for that file's changes");
        m_fileSystemWatcher.removePath(path);
        return;
    }

    ErrorString errorDescription;
    const QByteArray data = readFileContents(path, errorDescription);
    if (!errorDescription.isEmpty()) {
        QNWARNING("note_editor", errorDescription);
        m_fileSystemWatcher.removePath(path);
        return;
    }

    QNTRACE(
        "note_editor",
        "Size of new resource data: " << humanReadableSize(
            static_cast<quint64>(std::max<qsizetype>(data.size(), 0))));

    const QByteArray dataHash = calculateHash(data);

    int errorCode = 0;
    const bool res = updateResourceHashHelperFile(
        it.value(), dataHash, resourceFileInfo.absolutePath(), errorCode,
        errorDescription);

    if (Q_UNLIKELY(!res)) {
        QNWARNING(
            "note_editor",
            "Can't process resource local file change "
                << "properly: can't update the hash for resource file: error "
                   "code = "
                << errorCode << ", error description: " << errorDescription);

        m_fileSystemWatcher.removePath(path);
        return;
    }

    Q_EMIT resourceFileChanged(it.value(), path, data, dataHash);
}

void ResourceDataInTemporaryFileStorageManager::onFileRemoved(
    const QString & path)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager::onFileRemoved: " << path);

    const auto it = m_resourceLocalIdByFilePath.find(path);
    if (it != m_resourceLocalIdByFilePath.end()) {
        Q_UNUSED(m_resourceLocalIdByFilePath.erase(it));
    }
}

void ResourceDataInTemporaryFileStorageManager::onFoundResourceData(
    qevercloud::Resource resource) // NOLINT
{
    const QString & resourceLocalId = resource.localId();

    const auto fit =
        m_resourceLocalIdsPendingFindInLocalStorage.find(resourceLocalId);

    if (fit != m_resourceLocalIdsPendingFindInLocalStorage.end()) {
        QNDEBUG(
            "note_editor",
            "ResourceDataInTemporaryFileStorageManager"
                << "::onFoundResourceData: " << resource);

        m_resourceLocalIdsPendingFindInLocalStorage.erase(fit);

        if (Q_UNLIKELY(!m_pCurrentNote)) {
            QNWARNING(
                "note_editor",
                "Received resource data from the local storage but no note is "
                    << "set to ResourceDataInTemporaryFileStorageManager");
            return;
        }

        const QString noteLocalId = m_pCurrentNote->localId();

        const QByteArray dataHash =
            (resource.data() && resource.data()->bodyHash())
            ? *resource.data()->bodyHash()
            : calculateHash(resource.data().value().body().value());

        ErrorString errorDescription;
        const bool res = writeResourceDataToTemporaryFile(
            noteLocalId, resourceLocalId, *resource.data()->body(), dataHash,
            ResourceType::Image, errorDescription,
            CheckResourceFileActualityOption::Off);

        if (!res) {
            Q_EMIT failedToPutResourceDataIntoTemporaryFile(
                resourceLocalId, noteLocalId, errorDescription);
        }

        if (m_resourceLocalIdsPendingFindInLocalStorage.empty()) {
            QNDEBUG(
                "note_editor",
                "Received and processed all image resources "
                    << "data for the current note, emitting noteResourcesReady "
                    << "signal: note local id = " << noteLocalId);
            Q_EMIT noteResourcesReady(noteLocalId);
        }
        else {
            QNDEBUG(
                "note_editor",
                "Still pending "
                    << m_resourceLocalIdsPendingFindInLocalStorage.size()
                    << " resources data to be found within the local storage");
        }

        return;
    }

    const auto oit =
        m_resourceLocalIdsPendingFindInLocalStorageForWritingToFileForOpening
            .find(resourceLocalId);

    if (oit !=
        m_resourceLocalIdsPendingFindInLocalStorageForWritingToFileForOpening
            .end())
    {
        QNDEBUG(
            "note_editor",
            "ResourceDataInTemporaryFileStorageManager"
                << "::onFoundResourceData (for resource file opening): "
                << resource);

        m_resourceLocalIdsPendingFindInLocalStorageForWritingToFileForOpening
            .erase(oit);

        if (Q_UNLIKELY(!m_pCurrentNote)) {
            QNWARNING(
                "note_editor",
                "Received resource data from the local storage (for resource "
                    << "file opening) but no note is set to "
                    << "ResourceDataInTemporaryFileStorageManager");
            return;
        }

        const QString noteLocalId = m_pCurrentNote->localId();

        QByteArray dataHash = (resource.data() && resource.data()->bodyHash())
            ? *resource.data()->bodyHash()
            : calculateHash(resource.data().value().body().value());

        WriteResourceDataCallback callback =
            OpenResourcePreparationProgressFunctor(resourceLocalId, *this);

        const bool isImageResource =
            (resource.mime() &&
             resource.mime()->startsWith(QStringLiteral("image")));

        const ResourceType resourceType =
            (isImageResource ? ResourceType::Image : ResourceType::NonImage);

        ErrorString errorDescription;
        const bool res = writeResourceDataToTemporaryFile(
            noteLocalId, resourceLocalId, *resource.data()->body(), dataHash,
            resourceType, errorDescription,
            CheckResourceFileActualityOption::Off, callback);

        if (!res) {
            Q_EMIT failedToOpenResource(
                resourceLocalId, noteLocalId, errorDescription);
            return;
        }

        QString fileStoragePath =
            (isImageResource ? m_imageResourceFileStorageLocation
                             : m_nonImageResourceFileStorageLocation);

        fileStoragePath += QStringLiteral("/") + noteLocalId +
            QStringLiteral("/") + resourceLocalId + QStringLiteral(".dat");

        watchResourceFileForChanges(resourceLocalId, fileStoragePath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fileStoragePath));
        Q_EMIT openedResource(resourceLocalId, noteLocalId);
    }
}

void ResourceDataInTemporaryFileStorageManager::onFailedToFindResourceData(
    QString resourceLocalId, ErrorString errorDescription) // NOLINT
{
    const auto fit =
        m_resourceLocalIdsPendingFindInLocalStorage.find(resourceLocalId);
    if (fit != m_resourceLocalIdsPendingFindInLocalStorage.end()) {
        QNDEBUG(
            "note_editor",
            "ResourceDataInTemporaryFileStorageManager"
                << "::onFailedToFindResourceData: resource local id = "
                << resourceLocalId
                << ", error description = " << errorDescription);

        m_resourceLocalIdsPendingFindInLocalStorage.erase(fit);

        if (Q_UNLIKELY(!m_pCurrentNote)) {
            QNWARNING(
                "note_editor",
                "Received failure to locate resource data within the local "
                    << "storage but no note is set to "
                    << "ResourceDataInTemporaryFileStorageManager");
            return;
        }

        const QString noteLocalId = m_pCurrentNote->localId();
        Q_EMIT failedToPutResourceDataIntoTemporaryFile(
            resourceLocalId, noteLocalId, errorDescription);

        if (m_resourceLocalIdsPendingFindInLocalStorage.empty()) {
            Q_EMIT noteResourcesReady(noteLocalId);
        }
        else {
            QNDEBUG(
                "note_editor",
                "Still pending "
                    << m_resourceLocalIdsPendingFindInLocalStorage.size()
                    << " resources data to be found within the local storage");
        }

        return;
    }

    const auto oit =
        m_resourceLocalIdsPendingFindInLocalStorageForWritingToFileForOpening
            .find(resourceLocalId);
    if (oit !=
        m_resourceLocalIdsPendingFindInLocalStorageForWritingToFileForOpening
            .end())
    {
        QNDEBUG(
            "note_editor",
            "ResourceDataInTemporaryFileStorageManager"
                << "::onFailedToFindResourceData (for resource file "
                << "opening): resource local id = " << resourceLocalId
                << ", error description = " << errorDescription);

        m_resourceLocalIdsPendingFindInLocalStorage.erase(oit);

        if (Q_UNLIKELY(!m_pCurrentNote)) {
            QNWARNING(
                "note_editor",
                "Received failure to locate resource data within the local "
                    << "storage (for resource file opening) but no note is set "
                    << "to ResourceDataInTemporaryFileStorageManager");

            return;
        }

        const QString noteLocalId = m_pCurrentNote->localId();

        Q_EMIT failedToOpenResource(
            resourceLocalId, noteLocalId, errorDescription);
        return;
    }
}

void ResourceDataInTemporaryFileStorageManager::createConnections()
{
    QObject::connect(
        &m_fileSystemWatcher, &FileSystemWatcher::fileChanged, this,
        &ResourceDataInTemporaryFileStorageManager::onFileChanged);

    auto & noteEditorLocalStorageBroker =
        NoteEditorLocalStorageBroker::instance();

    QObject::connect(
        this, &ResourceDataInTemporaryFileStorageManager::findResourceData,
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::findResourceData);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::foundResourceData, this,
        &ResourceDataInTemporaryFileStorageManager::onFoundResourceData);

    QObject::connect(
        &noteEditorLocalStorageBroker,
        &NoteEditorLocalStorageBroker::failedToFindResourceData, this,
        &ResourceDataInTemporaryFileStorageManager::onFailedToFindResourceData);
}

QByteArray ResourceDataInTemporaryFileStorageManager::calculateHash(
    const QByteArray & data) const
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5);
}

bool ResourceDataInTemporaryFileStorageManager::
    checkIfResourceFileExistsAndIsActual(
        const QString & noteLocalId, const QString & resourceLocalId,
        const QString & fileStoragePath, const QByteArray & dataHash) const
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::checkIfResourceFileExistsAndIsActual: note local id = "
            << noteLocalId << ", resource local id = " << resourceLocalId
            << ", data hash = " << dataHash.toHex());

    if (Q_UNLIKELY(fileStoragePath.isEmpty())) {
        QNWARNING("note_editor", "Resource file storage location is empty");
        return false;
    }

    const QFileInfo resourceFileInfo(fileStoragePath);
    if (!resourceFileInfo.exists()) {
        QNTRACE(
            "note_editor",
            "Resource file for note local id "
                << noteLocalId << " and resource local id " << resourceLocalId
                << " does not exist");
        return false;
    }

    const QFileInfo resourceHashFileInfo(
        resourceFileInfo.absolutePath() + QStringLiteral("/") +
        resourceFileInfo.baseName() + QStringLiteral(".hash"));

    if (!resourceHashFileInfo.exists()) {
        QNTRACE(
            "note_editor",
            "Resource hash file for note local id "
                << noteLocalId << " and resource local id " << resourceLocalId
                << " does not exist");
        return false;
    }

    QFile resourceHashFile(resourceHashFileInfo.absoluteFilePath());
    if (!resourceHashFile.open(QIODevice::ReadOnly)) {
        QNWARNING("note_editor", "Can't open resource hash file for reading");
        return false;
    }

    const QByteArray storedHash = resourceHashFile.readAll();
    if (storedHash != dataHash) {
        QNTRACE(
            "note_editor",
            "Resource must be stale, the stored hash "
                << storedHash.toHex() << " does not match the actual hash "
                << dataHash.toHex());
        return false;
    }

    QNDEBUG("note_editor", "Resource file exists and is actual");
    return true;
}

bool ResourceDataInTemporaryFileStorageManager::updateResourceHashHelperFile(
    const QString & resourceLocalId, const QByteArray & dataHash,
    const QString & storageFolderPath, int & errorCode,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::updateResourceHashHelperFile: resource local id = "
            << resourceLocalId << ", data hash = " << dataHash.toHex()
            << ", storage folder path = " << storageFolderPath);

    QFile file(
        storageFolderPath + QStringLiteral("/") + resourceLocalId +
        QStringLiteral(".hash"));

    if (Q_UNLIKELY(!file.open(QIODevice::WriteOnly))) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't open the file with resource's hash for writing"));
        errorDescription.details() = file.errorString();
        errorCode = file.error();
        return false;
    }

    const qint64 writeRes = file.write(dataHash);
    if (Q_UNLIKELY(writeRes < 0)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't write resource data hash to the separate file"));
        errorDescription.details() = file.errorString();
        errorCode = file.error();
        return false;
    }

    file.close();
    return true;
}

void ResourceDataInTemporaryFileStorageManager::watchResourceFileForChanges(
    const QString & resourceLocalId, const QString & fileStoragePath)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::watchResourceFileForChanges: resource local id = "
            << resourceLocalId << ", file storage path = " << fileStoragePath);

    m_fileSystemWatcher.addPath(fileStoragePath);

    QNINFO(
        "note_editor", "Start watching for resource file " << fileStoragePath);
}

void ResourceDataInTemporaryFileStorageManager::stopWatchingResourceFile(
    const QString & filePath)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::stopWatchingResourceFile: " << filePath);

    const auto it = m_resourceLocalIdByFilePath.find(filePath);
    if (it == m_resourceLocalIdByFilePath.end()) {
        QNTRACE("note_editor", "File is not being watched, nothing to do");
        return;
    }

    m_fileSystemWatcher.removePath(filePath);
    QNTRACE("note_editor", "Stopped watching for file");
}

void ResourceDataInTemporaryFileStorageManager::
    removeStaleResourceFilesFromCurrentNote()
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::removeStaleResourceFilesFromCurrentNote");

    if (!m_pCurrentNote) {
        QNDEBUG("note_editor", "No current note, nothing to do");
        return;
    }

    const QString & noteLocalId = m_pCurrentNote->localId();

    const auto resources =
        (m_pCurrentNote->resources() ? *m_pCurrentNote->resources()
                                     : QList<qevercloud::Resource>());

    QFileInfoList fileInfoList;

    QDir imageResourceFilesFolder(
        m_imageResourceFileStorageLocation + QStringLiteral("/") +
        m_pCurrentNote->localId());

    if (imageResourceFilesFolder.exists()) {
        fileInfoList = imageResourceFilesFolder.entryInfoList(QDir::Files);
        QNTRACE(
            "note_editor",
            "Found " << fileInfoList.size()
                     << " files wihin the image resource files folder "
                     << "for note with local id " << m_pCurrentNote->localId());
    }

    QDir genericResourceImagesFolder(
        m_nonImageResourceFileStorageLocation + QStringLiteral("/") +
        m_pCurrentNote->localId());

    if (genericResourceImagesFolder.exists()) {
        const QFileInfoList genericResourceImageFileInfos =
            genericResourceImagesFolder.entryInfoList(QDir::Files);

        const auto numGenericResourceImageFileInfos =
            genericResourceImageFileInfos.size();

        QNTRACE(
            "note_editor",
            "Found " << numGenericResourceImageFileInfos
                     << " files within the generic resource files "
                     << "folder for note with local id "
                     << m_pCurrentNote->localId());

        fileInfoList.append(genericResourceImageFileInfos);
    }

    QNTRACE(
        "note_editor",
        "Total " << fileInfoList.size() << " files to check for staleness");

    for (const auto & fileInfo: std::as_const(fileInfoList)) {
        const QString filePath = fileInfo.absoluteFilePath();

        if (fileInfo.isSymLink()) {
            QNTRACE("note_editor", "Removing symlink file without any checks");
            stopWatchingResourceFile(filePath);
            Q_UNUSED(removeFile(filePath))
            continue;
        }

        const QString fullSuffix = fileInfo.completeSuffix();
        if (fullSuffix == QStringLiteral("hash")) {
            QNTRACE("note_editor", "Skipping .hash helper file " << filePath);
            continue;
        }

        const QString baseName = fileInfo.baseName();
        QNTRACE("note_editor", "Checking file with base name " << baseName);

        auto resourceIt = std::find_if(
            resources.constBegin(), resources.constEnd(),
            [&baseName](const qevercloud::Resource & resource) {
                return baseName.startsWith(resource.localId());
            });
        if (resourceIt != resources.constEnd()) {
            const auto & resource = *resourceIt;
            if (resource.data() && resource.data()->bodyHash()) {
                const bool actual = checkIfResourceFileExistsAndIsActual(
                    noteLocalId, resource.localId(), filePath,
                    *resource.data()->bodyHash());

                if (actual) {
                    QNTRACE(
                        "note_editor",
                        "The resource file "
                            << filePath << " is still actual, will keep it");
                    continue;
                }
            }
            else {
                QNTRACE(
                    "note_editor",
                    "Resource at index "
                        << std::distance(resources.constBegin(), resourceIt)
                        << " doesn't have the data hash, will "
                        << "remove its resource file just in case");
            }
        }

        QNTRACE(
            "note_editor",
            "Found stale resource file " << filePath << ", removing it");

        stopWatchingResourceFile(filePath);
        Q_UNUSED(removeFile(filePath))

        // Need to also remove the helper .hash file
        stopWatchingResourceFile(filePath);
        Q_UNUSED(removeFile(
            fileInfo.absolutePath() + QStringLiteral("/") + baseName +
            QStringLiteral(".hash")));
    }
}

ResourceDataInTemporaryFileStorageManager::ResultType
    ResourceDataInTemporaryFileStorageManager::
        partialUpdateResourceFilesForCurrentNote(
            const QList<qevercloud::Resource> & previousResources,
            ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::partialUpdateResourceFilesForCurrentNote");

    if (Q_UNLIKELY(!m_pCurrentNote)) {
        QNDEBUG("note_editor", "No current note, nothing to do");
        return ResultType::Ready;
    }

    QList<qevercloud::Resource> newAndUpdatedResources;
    QStringList removedAndStaleResourceLocalIds;

    const auto resources =
        (m_pCurrentNote->resources() ? *m_pCurrentNote->resources()
                                     : QList<qevercloud::Resource>());

    for (const auto & resource: std::as_const(resources)) {
        const QString & resourceLocalId = resource.localId();

        QNTRACE(
            "note_editor",
            "Examining resource with local id " << resourceLocalId);

        const qevercloud::Resource * pPreviousResource = nullptr;
        for (const auto & previousResource: std::as_const(previousResources)) {
            if (previousResource.localId() == resourceLocalId) {
                pPreviousResource = &previousResource;
                break;
            }
        }

        if (!pPreviousResource) {
            QNTRACE(
                "note_editor",
                "No previous resource, considering the resource new: "
                    << "local id = " << resourceLocalId);

            if (!resource.mime() ||
                !resource.mime()->startsWith(QStringLiteral("image")))
            {
                QNTRACE(
                    "note_editor",
                    "Resource has no mime type or mime type is not an image "
                        << "one, won't add the resource to the list of new "
                        << "ones");
            }
            else {
                newAndUpdatedResources << resource;
            }

            continue;
        }

        QNTRACE(
            "note_editor",
            "Previous resource's data size = "
                << ((pPreviousResource->data() &&
                     pPreviousResource->data()->size())
                        ? *pPreviousResource->data()->size()
                        : 0)
                << ", updated resource's data size = "
                << ((resource.data() && resource.data()->size())
                        ? *resource.data()->size()
                        : 0)
                << "; previous resource's data hash = "
                << ((pPreviousResource->data() &&
                     pPreviousResource->data()->bodyHash())
                        ? pPreviousResource->data()->bodyHash()->toHex()
                        : QByteArray())
                << ", updated resource's data hash = "
                << ((resource.data() && resource.data()->bodyHash())
                        ? resource.data()->bodyHash()->toHex()
                        : QByteArray()));

        const bool dataHashIsDifferent =
            (!pPreviousResource->data() ||
             !pPreviousResource->data()->bodyHash() || !resource.data() ||
             !resource.data()->bodyHash() ||
             (resource.data()->bodyHash() !=
              pPreviousResource->data()->bodyHash()));

        const bool dataSizeIsDifferent =
            (!pPreviousResource->data() || !pPreviousResource->data()->size() ||
             !resource.data() || !resource.data()->size() ||
             (resource.data()->size() != pPreviousResource->data()->size()));

        if (dataHashIsDifferent || dataSizeIsDifferent) {
            QNTRACE(
                "note_editor",
                "Different or missing data hash or size, "
                    << "considering the resource updated: local id = "
                    << resourceLocalId);

            if (!resource.mime() ||
                !resource.mime()->startsWith(QStringLiteral("image")))
            {
                QNTRACE(
                    "note_editor",
                    "Resource has no mime type or mime type is not an image "
                        << "one, will remove the resource instead "
                        << "of adding it to the list of updated resources");
                removedAndStaleResourceLocalIds << resourceLocalId;
            }
            else {
                newAndUpdatedResources << resource;
            }

            continue;
        }
    }

    for (const auto & previousResource: std::as_const(previousResources)) {
        const QString & resourceLocalId = previousResource.localId();

        const qevercloud::Resource * pResource = nullptr;
        for (auto uit = resources.constBegin(), uend = resources.constEnd();
             uit != uend; ++uit)
        {
            const auto & resource = *uit;
            if (resource.localId() == resourceLocalId) {
                pResource = &resource;
                break;
            }
        }

        if (!pResource) {
            QNTRACE(
                "note_editor",
                "Found no resource with local id "
                    << resourceLocalId << " within the list of new/updated "
                    << "resources, considering it stale");

            removedAndStaleResourceLocalIds << resourceLocalId;
        }
    }

    const QString noteLocalId = m_pCurrentNote->localId();

    QStringList dirsToCheck;
    dirsToCheck.reserve(2);

    dirsToCheck
        << (m_imageResourceFileStorageLocation + QStringLiteral("/") +
            noteLocalId);

    dirsToCheck
        << (m_nonImageResourceFileStorageLocation + QStringLiteral("/") +
            noteLocalId);

    for (const auto & dirPath: std::as_const(dirsToCheck)) {
        QDir dir{dirPath};
        if (!dir.exists()) {
            continue;
        }

        QDirIterator dirIterator{dir};
        while (dirIterator.hasNext()) {
            QString entry = dirIterator.next();
            QFileInfo entryInfo{entry};
            if (!entryInfo.isFile()) {
                continue;
            }

            for (const auto & localId:
                 std::as_const(removedAndStaleResourceLocalIds))
            {
                if (!entry.startsWith(localId) ||
                    (entryInfo.completeSuffix() == (QStringLiteral("hash"))))
                {
                    continue;
                }

                stopWatchingResourceFile(dir.absoluteFilePath(entry));

                if (!removeFile(dir.absoluteFilePath(entry))) {
                    errorDescription.setBase(
                        QT_TR_NOOP("Failed to remove stale temporary resource "
                                   "file"));

                    errorDescription.details() =
                        QDir::toNativeSeparators(dir.absoluteFilePath(entry));

                    QNWARNING("note_editor", errorDescription);
                    return ResultType::Error;
                }

                const QString hashFile =
                    entryInfo.baseName() + QStringLiteral(".hash");

                const QFileInfo hashFileInfo(dir.absoluteFilePath(hashFile));
                if (hashFileInfo.exists() &&
                    !removeFile(hashFileInfo.absoluteFilePath()))
                {
                    errorDescription.setBase(QT_TR_NOOP(
                        "Failed to remove stale temporary resource's "
                        "helper .hash file"));

                    errorDescription.details() =
                        QDir::toNativeSeparators(dir.absoluteFilePath(entry));

                    QNWARNING("note_editor", errorDescription);
                    return ResultType::Error;
                }
            }
        }
    }

    return putResourcesDataToTemporaryFiles(
        newAndUpdatedResources, errorDescription);
}

void ResourceDataInTemporaryFileStorageManager::
    emitPartialUpdateResourceFilesForCurrentNoteProgress(const double progress)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::emitPartialUpdateResourceFilesForCurrentNoteProgress: "
            << "progress = " << progress);

    if (Q_UNLIKELY(!m_pCurrentNote)) {
        QNWARNING(
            "note_editor",
            "Detected attempt to emit partial update "
                << "resource files for current note progress but no current "
                << "note is set; progress = " << progress);
        return;
    }

    Q_EMIT noteResourcesPreparationProgress(
        progress, m_pCurrentNote->localId());
}

ResourceDataInTemporaryFileStorageManager::ResultType
    ResourceDataInTemporaryFileStorageManager::putResourcesDataToTemporaryFiles(
        const QList<qevercloud::Resource> & resources,
        ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::putResourcesDataToTemporaryFiles: " << resources.size()
            << " resources");

    if (Q_UNLIKELY(!m_pCurrentNote)) {
        errorDescription.setBase(QT_TR_NOOP(
            "Can't put resources data into temporary files: internal "
            "error, no current note is set to "
            "ResourceDataInTemporaryFileStorageManager"));
        QNWARNING("note_editor", errorDescription);
        return ResultType::Error;
    }

    std::size_t numResourcesPendingDataFromLocalStorage = 0;
    const auto numNewAndUpdatedResources = resources.size();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    Q_ASSERT(numNewAndUpdatedResources <= std::numeric_limits<int>::max());
#endif

    int newOrUpdatedResourceIndex = 0;
    for (const auto & resource: std::as_const(resources)) {
        if (!resource.data() || !resource.data()->body()) {
            Q_UNUSED(m_resourceLocalIdsPendingFindInLocalStorage.insert(
                resource.localId()))
            requestResourceDataFromLocalStorage(resource);
            ++numResourcesPendingDataFromLocalStorage;
            continue;
        }

        const QByteArray dataHash =
            (resource.data() && resource.data()->bodyHash())
            ? *resource.data()->bodyHash()
            : calculateHash(*resource.data()->body());

        WriteResourceDataCallback callback =
            PartialUpdateResourceFilesForCurrentNoteProgressFunctor(
                newOrUpdatedResourceIndex,
                static_cast<int>(numNewAndUpdatedResources), *this);

        const bool res = writeResourceDataToTemporaryFile(
            m_pCurrentNote->localId(), resource.localId(),
            *resource.data()->body(), dataHash, ResourceType::Image,
            errorDescription, CheckResourceFileActualityOption::On, callback);

        if (!res) {
            Q_EMIT failedToPutResourceDataIntoTemporaryFile(
                resource.localId(), m_pCurrentNote->localId(),
                errorDescription);
        }

        ++newOrUpdatedResourceIndex;
    }

    if (numResourcesPendingDataFromLocalStorage > 0) {
        return ResultType::AsyncPending;
    }

    return ResultType::Ready;
}

void ResourceDataInTemporaryFileStorageManager::
    emitOpenResourcePreparationProgress(
        const double progress, const QString & resourceLocalId)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::emitOpenResourcePreparationProgress: resource local id = "
            << resourceLocalId << ", progress = " << progress);

    if (Q_UNLIKELY(!m_pCurrentNote)) {
        QNWARNING(
            "note_editor",
            "Detected attempt to emit open resource "
                << "preparation progress but no current note is set; resource "
                << "local id = " << resourceLocalId
                << ", progress = " << progress);
        return;
    }

    Q_EMIT openResourcePreparationProgress(
        progress, resourceLocalId, m_pCurrentNote->localId());
}

void ResourceDataInTemporaryFileStorageManager::
    requestResourceDataFromLocalStorage(const qevercloud::Resource & resource)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::requestResourceDataFromLocalStorage: resource local id = "
            << resource.localId());

    Q_EMIT findResourceData(resource.localId());
}

bool ResourceDataInTemporaryFileStorageManager::
    writeResourceDataToTemporaryFile(
        const QString & noteLocalId, const QString & resourceLocalId,
        const QByteArray & data, const QByteArray & dataHash,
        const ResourceType resourceType, ErrorString & errorDescription,
        const CheckResourceFileActualityOption checkActualityOption,
        const WriteResourceDataCallback & callback)
{
    QNDEBUG(
        "note_editor",
        "ResourceDataInTemporaryFileStorageManager"
            << "::writeResourceDataToTemporaryFile: note local id = "
            << noteLocalId << ", resource local id = " << resourceLocalId);

    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected attempt to write resource data for empty "
                       "note local id to local file"));
        QNWARNING("note_editor", errorDescription);
        return false;
    }

    if (Q_UNLIKELY(resourceLocalId.isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected attempt to write data for empty resource "
                       "local id to local file"));
        QNWARNING(
            "note_editor",
            errorDescription << ", note local id = " << noteLocalId);
        return false;
    }

    if (Q_UNLIKELY(data.isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected attempt to write empty "
                       "resource data to local file"));
        QNWARNING(
            "note_editor",
            errorDescription << ", note local id = " << noteLocalId
                             << ", resource local id = " << resourceLocalId);
        return false;
    }

    QString fileStoragePath =
        (resourceType == ResourceType::Image
             ? m_imageResourceFileStorageLocation
             : m_nonImageResourceFileStorageLocation);

    fileStoragePath += QStringLiteral("/") + noteLocalId + QStringLiteral("/") +
        resourceLocalId + QStringLiteral(".dat");

    const QFileInfo fileStoragePathInfo{fileStoragePath};
    QDir fileStorageDir{fileStoragePathInfo.absoluteDir()};
    if (!fileStorageDir.exists()) {
        if (!fileStorageDir.mkpath(fileStorageDir.absolutePath())) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't create folder to write "
                           "the resource into"));

            QNWARNING(
                "note_editor",
                errorDescription
                    << ", note local id = " << noteLocalId
                    << ", resource local id = " << resourceLocalId);
            return false;
        }
    }

    if (checkActualityOption == CheckResourceFileActualityOption::On) {
        const bool actual = checkIfResourceFileExistsAndIsActual(
            noteLocalId, resourceLocalId, fileStoragePath,
            (dataHash.isEmpty() ? calculateHash(data) : dataHash));

        if (actual) {
            QNTRACE(
                "note_editor",
                "Skipping writing the resource to file as it is not necessary, "
                    << "the file already exists and is actual");
            return true;
        }
    }

    QFile file{fileStoragePath};
    if (Q_UNLIKELY(!file.open(QIODevice::WriteOnly))) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't open resource file for writing"));

        errorDescription.details() = file.errorString();
        const int errorCode = file.error();
        QNWARNING(
            "note_editor",
            errorDescription << ", error code = " << errorCode
                             << ", note local id = " << noteLocalId
                             << ", resource local id = " << resourceLocalId);

        return false;
    }

    if (!callback || (data.size() <= RESOURCE_DATA_BATCH_SIZE_IN_BYTES)) {
        const qint64 writeRes = file.write(data);
        if (Q_UNLIKELY(writeRes < 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("Can't write data to resource file"));

            errorDescription.details() = file.errorString();
            const int errorCode = file.error();

            QNWARNING(
                "note_editor",
                errorDescription
                    << ", error code = " << errorCode
                    << ", note local id = " << noteLocalId
                    << ", resource local id = " << resourceLocalId);
            return false;
        }
    }
    else {
        const char * rawData = data.constData();
        size_t offset = 0;
        double progress = 0;
        while (true) {
            const qint64 writeRes =
                file.write(rawData, RESOURCE_DATA_BATCH_SIZE_IN_BYTES);

            if (Q_UNLIKELY(writeRes < 0)) {
                errorDescription.setBase(
                    QT_TR_NOOP("Can't write data to resource file"));

                errorDescription.details() = file.errorString();
                const int errorCode = file.error();

                QNWARNING(
                    "note_editor",
                    errorDescription
                        << ", error code = " << errorCode
                        << ", note local id = " << noteLocalId
                        << ", resource local id = " << resourceLocalId);
                return false;
            }

            offset += static_cast<size_t>(writeRes);
            if (offset >= static_cast<size_t>(data.size())) {
                break;
            }

            rawData += writeRes;

            if (callback) {
                progress = static_cast<double>(offset) /
                    static_cast<double>(data.size());
                callback(progress);
            }
        }
    }

    file.close();

    m_resourceLocalIdByFilePath[fileStoragePath] = resourceLocalId;

    int errorCode = 0;
    const bool res = updateResourceHashHelperFile(
        resourceLocalId, dataHash, fileStoragePathInfo.absolutePath(),
        errorCode, errorDescription);

    if (Q_UNLIKELY(!res)) {
        QNWARNING(
            "note_editor",
            errorDescription << ", error code = " << errorCode
                             << ", resource local id = " << resourceLocalId);
        return false;
    }

    QNDEBUG(
        "note_editor",
        "Successfully wrote resource data to file: "
            << "resource local id = " << resourceLocalId
            << ", file path = " << fileStoragePath);

    return true;
}

} // namespace quentier
