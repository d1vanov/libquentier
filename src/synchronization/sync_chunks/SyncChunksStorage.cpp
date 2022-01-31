/*
 * Copyright 2022 Dmitry Ivanov
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

#include "SyncChunksStorage.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/serialization/json/SyncChunk.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

namespace quentier::synchronization {

namespace {

[[nodiscard]] std::optional<std::pair<qint32, qint32>>
    splitSyncChunkFileNameIntoUsns(const QString & syncChunkFileName)
{
    const QStringList parts = syncChunkFileName.split(
        QChar::fromLatin1('_'),
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        Qt::SkipEmptyParts);
#else
        QString::SkipEmptyParts);
#endif

    if (parts.size() != 2) {
        return std::nullopt;
    }

    bool conversionResult = false;
    qint32 usnFrom = parts[0].toInt(&conversionResult);
    if (!conversionResult) {
        return std::nullopt;
    }

    conversionResult = false;
    qint32 usnTo = parts[1].toInt(&conversionResult);
    if (!conversionResult) {
        return std::nullopt;
    }

    return std::make_pair(usnFrom, usnTo);
}

[[nodiscard]] std::optional<qevercloud::SyncChunk> deserializeSyncChunk(
    const QString & filePath)
{
    QFile file{filePath};
    if (Q_UNLIKELY(!file.open(QIODevice::ReadOnly))) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "Failed to open serialized sync chunk file: " << filePath);
        return std::nullopt;
    }

    const QByteArray contents = file.readAll();
    file.close();

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(contents, &error);
    if (Q_UNLIKELY(doc.isNull())) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "Failed to parse serialized sync chunk from file to json document: "
                << error.errorString() << "; file: " << filePath);
        return std::nullopt;
    }

    if (Q_UNLIKELY(!doc.isObject())) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "Cannot parse serialized sync chunk: json is not an object; file: "
                << filePath);
        return std::nullopt;
    }

    const QJsonObject obj = doc.object();
    qevercloud::SyncChunk syncChunk;
    if (!qevercloud::deserializeFromJson(obj, syncChunk)) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "Failed to deserialize sync chunk from json object, file: "
                << filePath);
    }

    return syncChunk;
}

[[nodiscard]] QList<qevercloud::SyncChunk> fetchRelevantSyncChunks(
    const QDir & dir,
    const qint32 afterUsn)
{
    QList<qevercloud::SyncChunk> result;

    const auto storedSyncChunkFileInfos =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    const auto tryToDeserializeSyncChunk = [&](const QString & filePath)
    {
        auto syncChunk = deserializeSyncChunk(filePath);
        if (syncChunk) {
            result << *syncChunk;
        }
        else {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to deserialize one of stored sync chunks: "
                << filePath);
        }
    };

    for (const auto & fileInfo: qAsConst(storedSyncChunkFileInfos)) {
        if (Q_UNLIKELY(!fileInfo.isReadable())) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Detected unreadable sync chunk file: "
                    << fileInfo.absoluteFilePath());
            continue;
        }

        if (afterUsn == 0) {
            tryToDeserializeSyncChunk(fileInfo.absoluteFilePath());
            continue;
        }

        const auto usns = splitSyncChunkFileNameIntoUsns(fileInfo.fileName());
        if (!usns) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to parse usns from sync chunk file name: "
                    << fileInfo.fileName());
        }

        if (usns->first > afterUsn) {
            tryToDeserializeSyncChunk(fileInfo.absoluteFilePath());
        }
    }

    return result;
}

[[nodiscard]] std::optional<qint32> syncChunkLowUsn(
    const qevercloud::SyncChunk & syncChunk)
{
    std::optional<qint32> lowUsn;

    const auto checkLowUsn = [&](const auto & items)
    {
        for (const auto & item: qAsConst(items)) {
            if (item.updateSequenceNum() &&
                (!lowUsn || *lowUsn > *item.updateSequenceNum()))
            {
                lowUsn = *item.updateSequenceNum();
            }
        }
    };

    if (syncChunk.notes()) {
        checkLowUsn(*syncChunk.notes());
    }

    if (syncChunk.notebooks()) {
        checkLowUsn(*syncChunk.notebooks());
    }

    if (syncChunk.tags()) {
        checkLowUsn(*syncChunk.tags());
    }

    if (syncChunk.searches()) {
        checkLowUsn(*syncChunk.searches());
    }

    if (syncChunk.resources()) {
        checkLowUsn(*syncChunk.resources());
    }

    if (syncChunk.linkedNotebooks()) {
        checkLowUsn(*syncChunk.linkedNotebooks());
    }

    return lowUsn;
}

void putSyncChunks(
    const QDir & dir, const QList<qevercloud::SyncChunk> & syncChunks)
{
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        const auto lowUsn = syncChunkLowUsn(syncChunk);
        const auto highUsn = syncChunk.chunkHighUSN();
        if (!lowUsn || !highUsn) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to fetch low and/or high USN for sync chunk: "
                    << syncChunk);
            continue;
        }

        const QString fileName = [&]
        {
            QString fileName;
            QTextStream strm{&fileName};

            strm << *lowUsn;
            strm << "_";
            strm << *highUsn;
            return fileName;
        }();

        QFile syncChunkFile{dir.absoluteFilePath(fileName)};
        if (!syncChunkFile.open(QIODevice::WriteOnly)) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to open file to save sync chunk into: "
                    << dir.absoluteFilePath(fileName));
            continue;
        }

        const QJsonObject obj = qevercloud::serializeToJson(syncChunk);
        QJsonDocument doc;
        doc.setObject(obj);

        syncChunkFile.write(doc.toJson(QJsonDocument::Indented));
        syncChunkFile.close();
    }
}

} // namespace

SyncChunksStorage::SyncChunksStorage(const QDir & rootDir) :
    m_rootDir{rootDir}, m_userOwnSyncChunksDir{m_rootDir.absoluteFilePath(
                            QStringLiteral("user_own"))}
{
    const QFileInfo rootDirInfo{m_rootDir.absolutePath()};

    if (Q_UNLIKELY(!rootDirInfo.isReadable())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SyncChunksStorage",
            "SyncChunksStorage requires a readable root dir")}};
    }

    if (Q_UNLIKELY(!rootDirInfo.isWritable())) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::SyncChunksStorage",
            "SyncChunksStorage requires a writable root dir")}};
    }

    if (!m_userOwnSyncChunksDir.exists()) {
        if (Q_UNLIKELY(!m_userOwnSyncChunksDir.mkpath(
                m_userOwnSyncChunksDir.absolutePath())))
        {
            throw RuntimeError{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SyncChunksStorage",
                "Cannot create dir for temporary storage of user own sync "
                "chunks")}};
        }
    }
    else {
        const QFileInfo userOwnSyncChunksDirInfo{
            m_userOwnSyncChunksDir.absolutePath()};

        if (Q_UNLIKELY(!userOwnSyncChunksDirInfo.isReadable())) {
            throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SyncChunksStorage",
                "Dir for temporary storage of user own sync chunks is not "
                "readable")}};
        }

        if (Q_UNLIKELY(!userOwnSyncChunksDirInfo.isWritable())) {
            throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
                "synchronization::SyncChunksStorage",
                "Dir for temporary storage of user own sync chunks is not "
                "writable")}};
        }
    }
}

QList<qevercloud::SyncChunk> SyncChunksStorage::fetchRelevantUserOwnSyncChunks(
    qint32 afterUsn) const
{
    return fetchRelevantSyncChunks(m_userOwnSyncChunksDir, afterUsn);
}

QList<qevercloud::SyncChunk>
    SyncChunksStorage::fetchRelevantLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid, qint32 afterUsn) const
{
    const QDir linkedNotebookDir{
        m_rootDir.absoluteFilePath(linkedNotebookGuid)};

    const QFileInfo linkedNotebookDirInfo{linkedNotebookDir.absolutePath()};
    if (!linkedNotebookDirInfo.exists()) {
        return {};
    }

    if (!linkedNotebookDirInfo.isDir() || !linkedNotebookDirInfo.isReadable()) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "What is supposed to be a dir for linked notebook sync chunks "
                << "temporary storage is either not a dir or not a readable "
                << "dir: " << linkedNotebookDirInfo.absolutePath());
        return {};
    }

    return fetchRelevantSyncChunks(linkedNotebookDir, afterUsn);
}

void SyncChunksStorage::putUserOwnSyncChunks(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    putSyncChunks(m_userOwnSyncChunksDir, syncChunks);
}

void SyncChunksStorage::putLinkedNotebookSyncChunks(
    const qevercloud::Guid & linkedNotebookGuid,
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    const QDir linkedNotebookDir{
        m_rootDir.absoluteFilePath(linkedNotebookGuid)};

    const QFileInfo linkedNotebookDirInfo{linkedNotebookDir.absolutePath()};
    if (!linkedNotebookDirInfo.exists()) {
        if (!m_rootDir.mkpath(linkedNotebookDir.absolutePath())) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to create dir to store linked notebook sync chunks: "
                    << linkedNotebookDir.absolutePath());
            return;
        }
    }
    else if (!linkedNotebookDirInfo.isWritable()) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "Dir to store linked notebook sync chunks is not writable: "
                << linkedNotebookDir.absolutePath());
        return;
    }

    putSyncChunks(linkedNotebookDir, syncChunks);
}

void SyncChunksStorage::clearUserOwnSyncChunks()
{
    // TODO: implement
}

void SyncChunksStorage::clearLinkedNotebookSyncChunks(
    const qevercloud::Guid & linkedNotebookGuid)
{
    // TODO: implement
    Q_UNUSED(linkedNotebookGuid)
}

void SyncChunksStorage::clearAllSyncChunks()
{
    // TODO: implement
}

} // namespace quentier::synchronization
