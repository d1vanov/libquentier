/*
 * Copyright 2022-2024 Dmitry Ivanov
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
#include "Utils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Runnable.h>
#include <quentier/utility/FileSystem.h>

#include <qevercloud/serialization/json/SyncChunk.h>
#include <qevercloud/utility/ToRange.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QReadLocker>
#include <QTextStream>
#include <QThreadPool>
#include <QWriteLocker>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

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

[[nodiscard]] QList<std::pair<qint32, qint32>> detectSyncChunkUsns(
    const QDir & dir)
{
    QList<std::pair<qint32, qint32>> result;

    const auto storedSyncChunkFileInfos =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    for (const auto & fileInfo: std::as_const(storedSyncChunkFileInfos)) {
        if (Q_UNLIKELY(!fileInfo.isReadable())) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Detected unreadable sync chunk file: "
                    << fileInfo.absoluteFilePath());
            continue;
        }

        const auto usns = splitSyncChunkFileNameIntoUsns(fileInfo.baseName());
        if (!usns) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Detected sync chunk file with wrong name pattern: "
                    << fileInfo.absoluteFilePath());
        }

        result << *usns;
    }

    std::sort(result.begin(), result.end());
    return result;
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

void filterLowUsnsForSyncChunk(
    const qint32 afterUsn, qevercloud::SyncChunk & syncChunk)
{
    const auto filterLowUsnItems = [&](auto & items) {
        for (auto it = items.begin(); it != items.end();) {
            if (it->updateSequenceNum() &&
                (*it->updateSequenceNum() <= afterUsn))
            {
                it = items.erase(it);
                continue;
            }

            ++it;
        }
    };

    if (syncChunk.notes()) {
        filterLowUsnItems(*syncChunk.mutableNotes());
        if (syncChunk.notes()->isEmpty()) {
            syncChunk.setNotes(std::nullopt);
        }
    }

    if (syncChunk.notebooks()) {
        filterLowUsnItems(*syncChunk.mutableNotebooks());
        if (syncChunk.notebooks()->isEmpty()) {
            syncChunk.setNotebooks(std::nullopt);
        }
    }

    if (syncChunk.tags()) {
        filterLowUsnItems(*syncChunk.mutableTags());
        if (syncChunk.tags()->isEmpty()) {
            syncChunk.setTags(std::nullopt);
        }
    }

    if (syncChunk.searches()) {
        filterLowUsnItems(*syncChunk.mutableSearches());
        if (syncChunk.searches()->isEmpty()) {
            syncChunk.setSearches(std::nullopt);
        }
    }

    if (syncChunk.resources()) {
        filterLowUsnItems(*syncChunk.mutableResources());
        if (syncChunk.resources()->isEmpty()) {
            syncChunk.setResources(std::nullopt);
        }
    }

    if (syncChunk.linkedNotebooks()) {
        filterLowUsnItems(*syncChunk.mutableLinkedNotebooks());
        if (syncChunk.linkedNotebooks()->isEmpty()) {
            syncChunk.setLinkedNotebooks(std::nullopt);
        }
    }
}

[[nodiscard]] QList<qevercloud::SyncChunk> fetchRelevantSyncChunks(
    const QDir & dir, const qint32 afterUsn)
{
    QList<qevercloud::SyncChunk> result;

    const auto storedSyncChunkFileInfos =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    const auto tryToDeserializeSyncChunk = [&](const QString & filePath,
                                               qint32 lowUsn) {
        auto syncChunk = deserializeSyncChunk(filePath);
        if (syncChunk) {
            if (afterUsn != 0 && lowUsn <= afterUsn) {
                filterLowUsnsForSyncChunk(afterUsn, *syncChunk);
            }

            result << *syncChunk;
        }
        else {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to deserialize one of stored sync chunks: "
                    << filePath);
        }
    };

    for (const auto & fileInfo: std::as_const(storedSyncChunkFileInfos)) {
        if (Q_UNLIKELY(!fileInfo.isReadable())) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Detected unreadable sync chunk file: "
                    << fileInfo.absoluteFilePath());
            continue;
        }

        const auto usns = splitSyncChunkFileNameIntoUsns(fileInfo.baseName());
        if (!usns) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to parse usns from sync chunk file name: "
                    << fileInfo.fileName());
        }

        if (afterUsn == 0 || usns->second > afterUsn) {
            tryToDeserializeSyncChunk(fileInfo.absoluteFilePath(), usns->first);
        }
    }

    return result;
}

void flushSyncChunk(
    const QDir & dir, const qevercloud::SyncChunk & syncChunk,
    const qint32 lowUsn, const qint32 highUsn)
{
    const QString fileName = [&] {
        QString fileName;
        QTextStream strm{&fileName};

        strm << lowUsn;
        strm << "_";
        strm << highUsn;
        return fileName;
    }();

    QFile syncChunkFile{dir.absoluteFilePath(fileName)};
    if (!syncChunkFile.open(QIODevice::WriteOnly)) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "Failed to open file to save sync chunk into: "
                << dir.absoluteFilePath(fileName));
        return;
    }

    const QJsonObject obj = qevercloud::serializeToJson(syncChunk);
    QJsonDocument doc;
    doc.setObject(obj);

    syncChunkFile.write(doc.toJson(QJsonDocument::Indented));
    syncChunkFile.close();
}

void removeDirWithLog(const QString & dirPath)
{
    if (!removeDir(dirPath)) {
        QNWARNING(
            "synchronization::SyncChunksStorage",
            "Failed to remove dir with contents: " << dirPath);
    }
}

} // namespace

SyncChunksStorage::SyncChunksStorage(const QDir & rootDir) :
    m_rootDir{rootDir},
    // clang-format off
    m_userOwnSyncChunksDir{
        m_rootDir.absoluteFilePath(QStringLiteral("user_own"))}
// clang-format on
{
    if (!m_rootDir.exists()) {
        if (Q_UNLIKELY(!m_rootDir.mkpath(m_rootDir.absolutePath()))) {
            throw RuntimeError{ErrorString{QStringLiteral(
                "Cannot create root dir for temporary sync chunks storage")}};
        }
    }
    else {
        const QFileInfo rootDirInfo{m_rootDir.absolutePath()};
        if (Q_UNLIKELY(!rootDirInfo.isReadable())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "SyncChunksStorage requires readable root dir")}};
        }

        if (Q_UNLIKELY(!rootDirInfo.isWritable())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "SyncChunksStorage requires writable root dir")}};
        }
    }

    if (!m_userOwnSyncChunksDir.exists()) {
        if (Q_UNLIKELY(!m_userOwnSyncChunksDir.mkpath(
                m_userOwnSyncChunksDir.absolutePath())))
        {
            throw RuntimeError{ErrorString{QStringLiteral(
                "Cannot create dir for temporary storage of user own sync "
                "chunks")}};
        }
    }
    else {
        const QFileInfo userOwnSyncChunksDirInfo{
            m_userOwnSyncChunksDir.absolutePath()};

        if (Q_UNLIKELY(!userOwnSyncChunksDirInfo.isReadable())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "Dir for temporary storage of user own sync chunks is not "
                "readable")}};
        }

        if (Q_UNLIKELY(!userOwnSyncChunksDirInfo.isWritable())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "Dir for temporary storage of user own sync chunks is not "
                "writable")}};
        }
    }

    initLowAndHighUsnsLists();
}

QList<std::pair<qint32, qint32>>
    SyncChunksStorage::fetchUserOwnSyncChunksLowAndHighUsns() const
{
    initLowAndHighUsnsLists();

    const QReadLocker locker{&m_dataLock};
    return m_userOwnSyncChunkLowAndHighUsns;
}

QList<std::pair<qint32, qint32>>
    SyncChunksStorage::fetchLinkedNotebookSyncChunksLowAndHighUsns(
        const qevercloud::Guid & linkedNotebookGuid) const
{
    initLowAndHighUsnsLists();

    const QReadLocker locker{&m_dataLock};

    const auto it =
        m_linkedNotebookSyncChunkLowAndHighUsns.find(linkedNotebookGuid);
    if (it != m_linkedNotebookSyncChunkLowAndHighUsns.end()) {
        return it.value();
    }

    return {};
}

QList<qevercloud::SyncChunk> SyncChunksStorage::fetchRelevantUserOwnSyncChunks(
    qint32 afterUsn) const
{
    initLowAndHighUsnsLists();

    const QReadLocker locker{&m_dataLock};

    auto result = fetchRelevantSyncChunks(m_userOwnSyncChunksDir, afterUsn);

    if (m_userOwnSyncChunksPendingPersistence.isEmpty()) {
        return result;
    }

    // It is guaranteed that not yet flushed user own sync chunks would not
    // interleave in their USN ranges with actually persisted sync chunks.
    appendPendingSyncChunks(
        m_userOwnSyncChunksPendingPersistence, afterUsn, result);

    return result;
}

QList<qevercloud::SyncChunk>
    SyncChunksStorage::fetchRelevantLinkedNotebookSyncChunks(
        const qevercloud::Guid & linkedNotebookGuid, qint32 afterUsn) const
{
    initLowAndHighUsnsLists();

    const QReadLocker locker{&m_dataLock};

    const QDir linkedNotebookDir{
        m_rootDir.absoluteFilePath(linkedNotebookGuid)};

    QList<qevercloud::SyncChunk> result;
    const QFileInfo linkedNotebookDirInfo{linkedNotebookDir.absolutePath()};
    if (linkedNotebookDirInfo.exists() && linkedNotebookDirInfo.isDir() &&
        linkedNotebookDirInfo.isReadable())
    {
        result = fetchRelevantSyncChunks(linkedNotebookDir, afterUsn);
    }

    const auto it =
        m_linkedNotebookSyncChunksPendingPersistence.find(linkedNotebookGuid);
    if (it == m_linkedNotebookSyncChunksPendingPersistence.end()) {
        return result;
    }

    const auto & linkedNotebookSyncChunksPendingPersistence = it.value();
    if (linkedNotebookSyncChunksPendingPersistence.isEmpty()) {
        return result;
    }

    // It is guaranteed that not yet flushed user own sync chunks would not
    // interleave in their USN ranges with actually persisted sync chunks.
    appendPendingSyncChunks(
        linkedNotebookSyncChunksPendingPersistence, afterUsn, result);

    return result;
}

void SyncChunksStorage::putUserOwnSyncChunks(
    QList<qevercloud::SyncChunk> syncChunks)
{
    initLowAndHighUsnsLists();

    const QWriteLocker locker{&m_dataLock};

    auto syncChunksInfo = toSyncChunksInfo(std::move(syncChunks));
    auto usns = toUsns(syncChunksInfo);
    m_userOwnSyncChunksPendingPersistence << syncChunksInfo;

    if (!m_userOwnSyncChunkLowAndHighUsns.isEmpty()) {
        const auto & lastExistingUsnRange =
            m_userOwnSyncChunkLowAndHighUsns.constLast();

        for (const auto & usnRange: std::as_const(usns)) {
            if (usnRange.first <= lastExistingUsnRange.second) {
                // At least one of new sync chunks put to the storage has
                // USN range which is interleaving with existing stored sync
                // chunks; the storage doesn't allow that, hence will clear
                // all stored user's own sync chunks
                clearUserOwnSyncChunksImpl();
                return;
            }
        }
    }

    m_userOwnSyncChunkLowAndHighUsns << usns;
    std::sort(
        m_userOwnSyncChunkLowAndHighUsns.begin(),
        m_userOwnSyncChunkLowAndHighUsns.end());
}

void SyncChunksStorage::putLinkedNotebookSyncChunks(
    const qevercloud::Guid & linkedNotebookGuid,
    QList<qevercloud::SyncChunk> syncChunks)
{
    initLowAndHighUsnsLists();

    const QWriteLocker locker{&m_dataLock};

    auto syncChunksInfo = toSyncChunksInfo(std::move(syncChunks));
    auto usns = toUsns(syncChunksInfo);

    auto & syncChunksPendingPersistence =
        m_linkedNotebookSyncChunksPendingPersistence[linkedNotebookGuid];
    syncChunksPendingPersistence << syncChunksInfo;

    if (Q_UNLIKELY(usns.isEmpty())) {
        return;
    }

    auto & lowAndHighUsnsData =
        m_linkedNotebookSyncChunkLowAndHighUsns[linkedNotebookGuid];

    if (!lowAndHighUsnsData.isEmpty()) {
        const auto & lastExistingUsnRange = lowAndHighUsnsData.constLast();

        for (const auto & usnRange: std::as_const(usns)) {
            if (usnRange.first <= lastExistingUsnRange.second) {
                // At least one of new sync chunks put to the storage has
                // USN range which is interleaving with existing stored sync
                // chunks; the storage doesn't allow that, hence will clear
                // all stored sync chunks for this linked notebook
                clearLinkedNotebookSyncChunksImpl(linkedNotebookGuid);
                return;
            }
        }
    }

    lowAndHighUsnsData << usns;
    std::sort(lowAndHighUsnsData.begin(), lowAndHighUsnsData.end());
}

void SyncChunksStorage::clearUserOwnSyncChunks()
{
    const QWriteLocker locker{&m_dataLock};
    clearUserOwnSyncChunksImpl();
}

void SyncChunksStorage::clearUserOwnSyncChunksImpl()
{
    m_userOwnSyncChunksPendingPersistence.clear();

    const auto userOwnSyncChunks = m_userOwnSyncChunksDir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto & userOwnSyncChunk: userOwnSyncChunks) {
        if (userOwnSyncChunk.isDir()) {
            removeDirWithLog(userOwnSyncChunk.absoluteFilePath());
        }
        else if (!removeFile(userOwnSyncChunk.absoluteFilePath())) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to remove sync chunk file: "
                    << userOwnSyncChunk.absoluteFilePath());
        }
    }

    m_userOwnSyncChunkLowAndHighUsns.clear();
}

void SyncChunksStorage::clearLinkedNotebookSyncChunks(
    const qevercloud::Guid & linkedNotebookGuid)
{
    const QWriteLocker locker{&m_dataLock};
    clearLinkedNotebookSyncChunksImpl(linkedNotebookGuid);
}

void SyncChunksStorage::clearLinkedNotebookSyncChunksImpl(
    const qevercloud::Guid & linkedNotebookGuid)
{
    m_linkedNotebookSyncChunksPendingPersistence.remove(linkedNotebookGuid);
    removeDirWithLog(m_rootDir.absoluteFilePath(linkedNotebookGuid));

    const auto it =
        m_linkedNotebookSyncChunkLowAndHighUsns.find(linkedNotebookGuid);

    if (it != m_linkedNotebookSyncChunkLowAndHighUsns.end()) {
        m_linkedNotebookSyncChunkLowAndHighUsns.erase(it);
    }
}

void SyncChunksStorage::clearAllSyncChunks()
{
    const QWriteLocker locker{&m_dataLock};

    m_userOwnSyncChunksPendingPersistence.clear();
    m_linkedNotebookSyncChunksPendingPersistence.clear();
    m_userOwnSyncChunkLowAndHighUsns.clear();
    m_linkedNotebookSyncChunkLowAndHighUsns.clear();

    const auto entries = m_rootDir.entryInfoList(
        QDir::Filters{} | QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs);

    for (const auto & entry: std::as_const(entries)) {
        if (entry.isDir()) {
            removeDirWithLog(entry.absoluteFilePath());
        }
        else {
            Q_UNUSED(removeFile(entry.absoluteFilePath()))
        }
    }
}

void SyncChunksStorage::flush()
{
    const QWriteLocker locker{&m_dataLock};

    for (const auto & syncChunkInfo:
         std::as_const(m_userOwnSyncChunksPendingPersistence))
    {
        flushSyncChunk(
            m_userOwnSyncChunksDir, syncChunkInfo.m_syncChunk,
            syncChunkInfo.m_lowUsn, syncChunkInfo.m_highUsn);
    }
    m_userOwnSyncChunksPendingPersistence.clear();

    for (const auto it: qevercloud::toRange(
             std::as_const(m_linkedNotebookSyncChunksPendingPersistence)))
    {
        const auto & linkedNotebookGuid = it.key();

        const QDir linkedNotebookDir{
            m_rootDir.absoluteFilePath(linkedNotebookGuid)};

        const QFileInfo linkedNotebookDirInfo{linkedNotebookDir.absolutePath()};
        if (!linkedNotebookDirInfo.exists()) {
            if (!m_rootDir.mkpath(linkedNotebookDir.absolutePath())) {
                QNWARNING(
                    "synchronization::SyncChunksStorage",
                    "Failed to create dir to store linked notebook sync "
                    "chunks: "
                        << linkedNotebookDir.absolutePath());
                continue;
            }
        }
        else if (!linkedNotebookDirInfo.isWritable()) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Dir to store linked notebook sync chunks is not writable: "
                    << linkedNotebookDir.absolutePath());
            continue;
        }

        for (const auto & syncChunkInfo: std::as_const(it.value())) {
            flushSyncChunk(
                linkedNotebookDir, syncChunkInfo.m_syncChunk,
                syncChunkInfo.m_lowUsn, syncChunkInfo.m_highUsn);
        }
    }
    m_linkedNotebookSyncChunksPendingPersistence.clear();
}

QList<SyncChunksStorage::SyncChunkInfo> SyncChunksStorage::toSyncChunksInfo(
    QList<qevercloud::SyncChunk> syncChunks) const
{
    QList<SyncChunkInfo> syncChunksInfo;
    syncChunksInfo.reserve(syncChunks.size());
    for (auto & syncChunk: syncChunks) {
        const auto highUsn = syncChunk.chunkHighUSN();
        const auto lowUsn =
            (highUsn ? utils::syncChunkLowUsn(syncChunk) : std::nullopt);

        if (Q_UNLIKELY(!(lowUsn && highUsn))) {
            QNWARNING(
                "synchronization::SyncChunksStorage",
                "Failed to fetch low and/or high USN for sync chunk: "
                    << syncChunk);
            continue;
        }

        syncChunksInfo << SyncChunkInfo{
            std::move(syncChunk), *lowUsn, *highUsn};
    }

    return syncChunksInfo;
}

QList<std::pair<qint32, qint32>> SyncChunksStorage::toUsns(
    const QList<SyncChunkInfo> & syncChunksInfo) const
{
    QList<std::pair<qint32, qint32>> usns;
    usns.reserve(syncChunksInfo.size());
    for (const auto & syncChunkInfo: syncChunksInfo) {
        usns << std::make_pair(syncChunkInfo.m_lowUsn, syncChunkInfo.m_highUsn);
    }
    return usns;
}

void SyncChunksStorage::appendPendingSyncChunks(
    const QList<SyncChunkInfo> & syncChunksInfo, qint32 afterUsn,
    QList<qevercloud::SyncChunk> & result) const
{
    for (const auto & syncChunkInfo: std::as_const(syncChunksInfo)) {
        if (syncChunkInfo.m_highUsn <= afterUsn) {
            continue;
        }

        if (afterUsn != 0 && syncChunkInfo.m_lowUsn <= afterUsn) {
            qevercloud::SyncChunk copy{syncChunkInfo.m_syncChunk};
            filterLowUsnsForSyncChunk(afterUsn, copy);
            result << copy;
        }
        else {
            result << syncChunkInfo.m_syncChunk;
        }
    }
}

void SyncChunksStorage::initLowAndHighUsnsLists() const
{
    if (m_initializedLowAndHighUsns.load(std::memory_order_acquire)) {
        return;
    }

    const QWriteLocker locker{&m_dataLock};

    m_userOwnSyncChunkLowAndHighUsns =
        detectSyncChunkUsns(m_userOwnSyncChunksDir);

    const auto entries =
        m_rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    m_linkedNotebookSyncChunkLowAndHighUsns.reserve(
        std::max<decltype(entries.size())>(entries.size() - 1, 0));

    for (const auto & entry: std::as_const(entries)) {
        if (entry.absoluteFilePath() == m_userOwnSyncChunksDir.absolutePath()) {
            continue;
        }

        auto linkedNotebookGuid = entry.fileName();

        auto linkedNotebookSyncChunkUsns =
            detectSyncChunkUsns(entry.absoluteFilePath());

        if (!linkedNotebookSyncChunkUsns.isEmpty()) {
            m_linkedNotebookSyncChunkLowAndHighUsns[linkedNotebookGuid] =
                std::move(linkedNotebookSyncChunkUsns);
        }
    }

    m_initializedLowAndHighUsns.store(true, std::memory_order_release);
}

} // namespace quentier::synchronization
