/*
 * Copyright 2016-2025 Dmitry Ivanov
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

#include "GenericResourceImageManager.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>

#include <qevercloud/types/Resource.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <memory>
#include <utility>

namespace quentier {

GenericResourceImageManager::GenericResourceImageManager(QObject * parent) :
    QObject(parent)
{}

void GenericResourceImageManager::setStorageFolderPath(
    const QString & storageFolderPath)
{
    QNDEBUG(
        "note_editor",
        "GenericResourceImageManager::setStorageFolderPath: "
            << storageFolderPath);

    m_storageFolderPath = storageFolderPath;
}

void GenericResourceImageManager::onGenericResourceImageWriteRequest(
    QString noteLocalId, QString resourceLocalId,               // NOLINT
    QByteArray resourceImageData, QString resourceFileSuffix,   // NOLINT
    QByteArray resourceActualHash, QString resourceDisplayName, // NOLINT
    QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "GenericResourceImageManager"
            << "::onGenericResourceImageWriteRequest: note local uid = "
            << noteLocalId << ", resource local id = " << resourceLocalId
            << ", resource actual hash = " << resourceActualHash.toHex()
            << ", request id = " << requestId);

#define RETURN_WITH_ERROR(message)                                             \
    ErrorString errorDescription(message);                                     \
    QNWARNING("note_editor", errorDescription);                                \
    Q_EMIT genericResourceImageWriteReply(                                     \
        /* success = */ false, QByteArray(), QString(), errorDescription,      \
        requestId);                                                            \
    return

    if (Q_UNLIKELY(m_storageFolderPath.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Storage folder path is empty"));
    }

    if (Q_UNLIKELY(noteLocalId.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Note local uid is empty"));
    }

    if (Q_UNLIKELY(resourceLocalId.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Resource local uid is empty"));
    }

    if (Q_UNLIKELY(resourceActualHash.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Resource hash is empty"));
    }

    if (Q_UNLIKELY(resourceFileSuffix.isEmpty())) {
        RETURN_WITH_ERROR(QT_TR_NOOP("Resource image file suffix is empty"));
    }

    const QString resourceFileNameMask =
        resourceLocalId + QStringLiteral("*.") + resourceFileSuffix;
    QDir storageDir{m_storageFolderPath + QStringLiteral("/") + noteLocalId};
    if (Q_UNLIKELY(!storageDir.exists())) {
        if (!storageDir.mkpath(storageDir.absolutePath())) {
            RETURN_WITH_ERROR(
                QT_TR_NOOP("Can't create the folder to store "
                           "the resource image in"));
        }
    }

    const QStringList nameFilter = QStringList{} << resourceFileNameMask;
    QFileInfoList existingResourceImageFileInfos = storageDir.entryInfoList(
        nameFilter, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot);

    bool resourceHashChanged = true;
    const QFileInfo resourceHashFileInfo{
        storageDir.absolutePath() + QStringLiteral("/") + resourceLocalId +
        QStringLiteral(".hash")};
    bool resourceHashFileExists = resourceHashFileInfo.exists();
    if (resourceHashFileExists) {
        if (Q_UNLIKELY(!resourceHashFileInfo.isWritable())) {
            RETURN_WITH_ERROR(QT_TR_NOOP("Resource hash file is not writable"));
        }

        if (resourceHashFileInfo.isReadable()) {
            QFile resourceHashFile{resourceHashFileInfo.absoluteFilePath()};
            Q_UNUSED(resourceHashFile.open(QIODevice::ReadOnly));
            const QByteArray previousResourceHash = resourceHashFile.readAll();

            if (resourceActualHash == previousResourceHash) {
                QNTRACE("note_editor", "Resource hash hasn't changed");
                resourceHashChanged = false;
            }
        }
    }

    bool resourceDisplayNameChanged = false;
    const QFileInfo resourceNameFileInfo{
        storageDir.absolutePath() + QStringLiteral("/") + resourceLocalId +
        QStringLiteral(".name")};

    if (!resourceHashChanged && resourceNameFileInfo.exists()) {
        if (Q_UNLIKELY(!resourceNameFileInfo.isWritable())) {
            RETURN_WITH_ERROR(QT_TR_NOOP("Resource name file is not writable"));
        }

        if (Q_UNLIKELY(!resourceNameFileInfo.isReadable())) {
            QNINFO(
                "note_editor",
                "Helper file with resource name for "
                    << "generic resource image is not readable: "
                    << resourceNameFileInfo.absoluteFilePath()
                    << " which is quite strange...");
            resourceDisplayNameChanged = true;
        }
        else {
            QFile resourceNameFile{resourceNameFileInfo.absoluteFilePath()};
            Q_UNUSED(resourceNameFile.open(QIODevice::ReadOnly));
            const QString previousResourceName =
                QString::fromLocal8Bit(resourceNameFile.readAll());

            if (resourceDisplayName != previousResourceName) {
                QNTRACE(
                    "note_editor",
                    "Resource display name has changed "
                        << "from " << previousResourceName << " to "
                        << resourceDisplayName);
                resourceDisplayNameChanged = true;
            }
        }
    }

    if (!resourceHashChanged && !resourceDisplayNameChanged &&
        !existingResourceImageFileInfos.isEmpty())
    {
        QNDEBUG(
            "note_editor",
            "resource hash and display name haven't "
                << "changed, won't rewrite the resource's image");
        Q_EMIT genericResourceImageWriteReply(
            /* success = */ true, resourceActualHash,
            existingResourceImageFileInfos.front().absoluteFilePath(),
            ErrorString(), requestId);
        return;
    }

    QNTRACE(
        "note_editor",
        "Writing resource image file and helper files with "
            << "hash and display name");

    const QString resourceImageFilePath = storageDir.absolutePath() +
        QStringLiteral("/") + resourceLocalId + QStringLiteral("_") +
        QString::number(QDateTime::currentMSecsSinceEpoch()) +
        QStringLiteral(".") + resourceFileSuffix;

    QFile resourceImageFile{resourceImageFilePath};
    if (Q_UNLIKELY(!resourceImageFile.open(QIODevice::ReadWrite))) {
        RETURN_WITH_ERROR(
            QT_TR_NOOP("Can't open resource image file for writing"));
    }
    resourceImageFile.write(resourceImageData);
    resourceImageFile.close();

    QFile resourceHashFile{resourceHashFileInfo.absoluteFilePath()};
    if (Q_UNLIKELY(!resourceHashFile.open(QIODevice::ReadWrite))) {
        RETURN_WITH_ERROR(
            QT_TR_NOOP("Can't open resource hash file for writing"));
    }
    resourceHashFile.write(resourceActualHash);
    resourceHashFile.close();

    QFile resourceNameFile{resourceNameFileInfo.absoluteFilePath()};
    if (Q_UNLIKELY(!resourceNameFile.open(QIODevice::ReadWrite))) {
        RETURN_WITH_ERROR(
            QT_TR_NOOP("Can't open resource name file for writing"));
    }
    resourceNameFile.write(resourceDisplayName.toLocal8Bit());
    resourceNameFile.close();

    QNTRACE(
        "note_editor",
        "Successfully wrote resource image file and helper "
            << "files with hash and display name for request " << requestId
            << ", resource image file path = " << resourceImageFilePath);

    Q_EMIT genericResourceImageWriteReply(
        /* success = */ true, resourceActualHash, resourceImageFilePath,
        ErrorString(), requestId);

    if (!existingResourceImageFileInfos.isEmpty()) {
        for (const auto & staleResourceImageFileInfo:
             std::as_const(existingResourceImageFileInfos))
        {
            QFile staleResourceImageFile{
                staleResourceImageFileInfo.absoluteFilePath()};

            if (Q_UNLIKELY(!staleResourceImageFile.remove())) {
                QNINFO(
                    "note_editor",
                    "Can't remove stale generic resource "
                        << "image file: "
                        << staleResourceImageFile.errorString()
                        << " (error code = " << staleResourceImageFile.error()
                        << ")");
            }
        }
    }
}

void GenericResourceImageManager::onCurrentNoteChanged(
    qevercloud::Note note) // NOLINT
{
    QNDEBUG(
        "note_editor",
        "GenericResourceImageManager::onCurrentNoteChanged: "
            << "new note local id = " << note.localId()
            << ", previous note local id = "
            << (m_pCurrentNote ? m_pCurrentNote->localId()
                               : QStringLiteral("<null>")));

    if (m_pCurrentNote && (m_pCurrentNote->localId() == note.localId())) {
        QNTRACE(
            "note_editor",
            "The current note is the same, only the note object might have "
                << "changed");
        *m_pCurrentNote = note;
        removeStaleGenericResourceImageFilesFromCurrentNote();
        return;
    }

    removeStaleGenericResourceImageFilesFromCurrentNote();

    if (!m_pCurrentNote) {
        m_pCurrentNote = std::make_unique<qevercloud::Note>(note);
    }
    else {
        *m_pCurrentNote = note;
    }
}

void GenericResourceImageManager::
    removeStaleGenericResourceImageFilesFromCurrentNote()
{
    QNDEBUG(
        "note_editor",
        "GenericResourceImageManager"
            << "::removeStaleGenericResourceImageFilesFromCurrentNote");

    if (!m_pCurrentNote) {
        QNDEBUG("note_editor", "No current note, nothing to do");
        return;
    }

    const QString & noteLocalId = m_pCurrentNote->localId();

    QDir storageDir{m_storageFolderPath + QStringLiteral("/") + noteLocalId};
    if (!storageDir.exists()) {
        QNTRACE(
            "note_editor",
            "Storage dir " << storageDir.absolutePath()
                           << " does not exist, nothing to do");
        return;
    }

    auto resources =
        (m_pCurrentNote->resources() ? *m_pCurrentNote->resources()
                                     : QList<qevercloud::Resource>());

    const QFileInfoList fileInfoList = storageDir.entryInfoList(QDir::Files);

    QNTRACE(
        "note_editor",
        "Will check " << fileInfoList.size()
                      << " generic resource image files for staleness");

    for (const auto & fileInfo: std::as_const(fileInfoList)) {
        const QString filePath = fileInfo.absoluteFilePath();

        const QString fullSuffix = fileInfo.completeSuffix();
        if (fullSuffix == QStringLiteral("hash")) {
            QNTRACE("note_editor", "Skipping .hash helper file " << filePath);
            continue;
        }

        const QString baseName = fileInfo.baseName();
        QNTRACE("note_editor", "Checking file with base name " << baseName);

        const auto resourceIt = std::find_if(
            resources.constBegin(), resources.constEnd(),
            [&baseName](const qevercloud::Resource & resource) {
                return baseName.startsWith(resource.localId());
            });
        if (resourceIt != resources.constEnd()) {
            const auto & resource = *resourceIt;
            if (resource.data() && resource.data()->bodyHash()) {
                QFileInfo helperHashFileInfo{
                    fileInfo.absolutePath() + QStringLiteral("/") +
                    resource.localId() + QStringLiteral(".hash")};

                if (helperHashFileInfo.exists()) {
                    QFile helperHashFile{helperHashFileInfo.absoluteFilePath()};
                    Q_UNUSED(helperHashFile.open(QIODevice::ReadOnly))
                    const QByteArray storedHash = helperHashFile.readAll();
                    if (storedHash == *resource.data()->bodyHash()) {
                        QNTRACE(
                            "note_editor",
                            "Resource file "
                                << filePath
                                << " appears to be still actual, will keep it");
                        continue;
                    }

                    QNTRACE(
                        "note_editor",
                        "The stored hash doesn't match "
                            << "the actual resource data hash: "
                            << "stored = " << storedHash.toHex()
                            << ", actual = "
                            << resource.data()->bodyHash()->toHex());
                }
                else {
                    QNTRACE(
                        "note_editor",
                        "Helper hash file "
                            << helperHashFileInfo.absoluteFilePath()
                            << " does not exist");
                }
            }
            else {
                QNTRACE(
                    "note_editor",
                    "Resource at index "
                        << std::distance(resources.constBegin(), resourceIt)
                        << " doesn't have the data hash, will remove "
                        << "its resource file just in case");
            }
        }

        QNTRACE(
            "note_editor",
            "Found stale generic resource image file " << filePath
                                                       << ", removing it");
        Q_UNUSED(utility::removeFile(filePath))

        // Need to also remove the helper .hash file
        Q_UNUSED(utility::removeFile(
            fileInfo.absolutePath() + QStringLiteral("/") + baseName +
            QStringLiteral(".hash")))
    }
}

} // namespace quentier
