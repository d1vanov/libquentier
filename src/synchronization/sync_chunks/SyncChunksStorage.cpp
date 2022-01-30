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

#include <QFileInfo>

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
    // TODO: implement
    Q_UNUSED(filePath)
    return std::nullopt;
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
    // TODO: implement
    Q_UNUSED(syncChunks)
}

void SyncChunksStorage::putLinkedNotebookSyncChunks(
    const qevercloud::Guid & linkedNotebookGuid,
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    // TODO: implement
    Q_UNUSED(linkedNotebookGuid)
    Q_UNUSED(syncChunks)
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
