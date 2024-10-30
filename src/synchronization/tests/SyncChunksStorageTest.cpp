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

#include <synchronization/sync_chunks/SyncChunksStorage.h>
#include <synchronization/sync_chunks/Utils.h>

#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/serialization/json/SyncChunk.h>
#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QHash>
#include <QJsonDocument>
#include <QList>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

namespace {

[[nodiscard]] qevercloud::SyncChunk generateSyncChunk(
    qint32 lowUsn, qint32 highUsn)
{
    qevercloud::SyncChunk result;

    constexpr int itemCount = 3;

    for (int i = 0; i < itemCount && lowUsn <= highUsn; ++i) {
        auto notebook =
            qevercloud::NotebookBuilder{}
                .setName(QStringLiteral("Notebook #") + QString::number(i + 1))
                .setUpdateSequenceNum(lowUsn++)
                .setGuid(UidGenerator::Generate())
                .build();

        if (!result.notebooks()) {
            result.setNotebooks(QList<qevercloud::Notebook>{} << notebook);
        }
        else {
            result.mutableNotebooks()->append(notebook);
        }
    }

    for (int i = 0; i < itemCount && lowUsn <= highUsn; ++i) {
        auto tag =
            qevercloud::TagBuilder{}
                .setName(QStringLiteral("Tag #") + QString::number(i + 1))
                .setUpdateSequenceNum(lowUsn++)
                .setGuid(UidGenerator::Generate())
                .build();

        if (!result.tags()) {
            result.setTags(QList<qevercloud::Tag>{} << tag);
        }
        else {
            result.mutableTags()->append(tag);
        }
    }

    for (int i = 0; i < itemCount && lowUsn <= highUsn; ++i) {
        auto savedSearch =
            qevercloud::SavedSearchBuilder{}
                .setName(
                    QStringLiteral("Saved search #") + QString::number(i + 1))
                .setUpdateSequenceNum(lowUsn++)
                .setGuid(UidGenerator::Generate())
                .setQuery(QStringLiteral("query"))
                .build();

        if (!result.searches()) {
            result.setSearches(QList<qevercloud::SavedSearch>{} << savedSearch);
        }
        else {
            result.mutableSearches()->append(savedSearch);
        }
    }

    for (int i = 0; i < itemCount && lowUsn <= highUsn; ++i) {
        auto note =
            qevercloud::NoteBuilder{}
                .setTitle(QStringLiteral("Note #") + QString::number(i + 1))
                .setUpdateSequenceNum(lowUsn++)
                .setGuid(UidGenerator::Generate())
                .build();

        if (!result.notes()) {
            result.setNotes(QList<qevercloud::Note>{} << note);
        }
        else {
            result.mutableNotes()->append(note);
        }
    }

    for (int i = 0; i < itemCount && lowUsn <= highUsn; ++i) {
        auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                  .setUsername(
                                      QStringLiteral("Linked notebook #") +
                                      QString::number(i + 1))
                                  .setUpdateSequenceNum(lowUsn++)
                                  .setGuid(UidGenerator::Generate())
                                  .build();

        if (!result.linkedNotebooks()) {
            result.setLinkedNotebooks(
                QList<qevercloud::LinkedNotebook>{} << linkedNotebook);
        }
        else {
            result.mutableLinkedNotebooks()->append(linkedNotebook);
        }
    }

    for (int i = 0; i < itemCount && lowUsn <= highUsn; ++i) {
        auto resource = qevercloud::ResourceBuilder{}
                            .setGuid(UidGenerator::Generate())
                            .setUpdateSequenceNum(lowUsn++)
                            .build();

        if (!result.resources()) {
            result.setResources(QList<qevercloud::Resource>{} << resource);
        }
        else {
            result.mutableResources()->append(resource);
        }
    }

    result.setChunkHighUSN(lowUsn - 1);
    return result;
}

void checkSyncChunksPersistenceDirIsEmpty(const QDir & dir)
{
    const auto fileEntries =
        dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    EXPECT_TRUE(fileEntries.isEmpty());

    const auto dirEntries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    ASSERT_EQ(dirEntries.size(), 1);

    const auto & frontSubdirInfo = dirEntries[0];
    ASSERT_EQ(frontSubdirInfo.fileName(), QStringLiteral("user_own"));

    QDir frontSubdir{frontSubdirInfo.absoluteFilePath()};
    const auto subdirEntries = frontSubdir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    EXPECT_TRUE(subdirEntries.isEmpty());
}

void checkUserOwnSyncChunksPersistenceIsNotEmpty(const QDir & dir)
{
    const auto dirEntries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    ASSERT_FALSE(dirEntries.isEmpty());

    const auto it = std::find_if(
        dirEntries.constBegin(), dirEntries.constEnd(),
        [](const QFileInfo & fileInfo) {
            return fileInfo.fileName() == QStringLiteral("user_own");
        });
    ASSERT_NE(it, dirEntries.constEnd());

    QDir userOwnSubdir{it->absoluteFilePath()};
    const auto subdirEntries = userOwnSubdir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
    EXPECT_FALSE(subdirEntries.isEmpty());
}

void checkLinkedNotebookSyncChunksPersistenceIsNotEmpty(const QDir & dir)
{
    const auto dirEntries =
        dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    ASSERT_FALSE(dirEntries.isEmpty());

    bool foundLinkedNotebookDir = false;
    for (const QFileInfo & entryInfo: std::as_const(dirEntries)) {
        if (entryInfo.fileName() == QStringLiteral("user_own")) {
            continue;
        }

        foundLinkedNotebookDir = true;

        QDir linkedNotebookSubdir{entryInfo.absoluteFilePath()};
        const auto subdirEntries = linkedNotebookSubdir.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        EXPECT_FALSE(subdirEntries.isEmpty());
    }

    EXPECT_TRUE(foundLinkedNotebookDir);
}

} // namespace

class SyncChunksStorageTest : public testing::Test
{
protected:
    void TearDown() override
    {
        QDir dir{m_temporaryDir.path()};
        const auto entries =
            dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

        for (const auto & entry: std::as_const(entries)) {
            if (entry.isDir()) {
                ASSERT_TRUE(removeDir(entry.absoluteFilePath()));
            }
            else {
                ASSERT_TRUE(removeFile(entry.absoluteFilePath()));
            }
        }
    }

protected:
    QTemporaryDir m_temporaryDir;
};

TEST_F(SyncChunksStorageTest, Ctor)
{
    EXPECT_NO_THROW(
        auto storage =
            std::make_shared<SyncChunksStorage>(QDir{m_temporaryDir.path()}));
}

TEST_F(SyncChunksStorageTest, FetchNonexistentUserOwnSyncChunks)
{
    auto storage =
        std::make_shared<SyncChunksStorage>(QDir{m_temporaryDir.path()});

    const auto usnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();
    EXPECT_TRUE(usnsRange.isEmpty());

    const auto syncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_TRUE(syncChunks.isEmpty());
}

TEST_F(SyncChunksStorageTest, FetchNonexistentLinkedNotebookSyncChunks)
{
    auto storage =
        std::make_shared<SyncChunksStorage>(QDir{m_temporaryDir.path()});

    const auto linkedNotebookGuid = UidGenerator::Generate();

    const auto usnsRanges =
        storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
            linkedNotebookGuid);

    EXPECT_TRUE(usnsRanges.isEmpty());

    const auto syncChunks =
        storage->fetchRelevantLinkedNotebookSyncChunks(linkedNotebookGuid, 0);

    EXPECT_TRUE(syncChunks.isEmpty());
}

TEST_F(SyncChunksStorageTest, FetchExistingUserOwnSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};

    QDir ownSubdir{temporaryDir.absoluteFilePath(QStringLiteral("user_own"))};
    temporaryDir.mkpath(ownSubdir.absolutePath());

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> expectedSyncChunks;
    expectedSyncChunks.reserve(syncChunkCount);

    QList<std::pair<qint32, qint32>> expectedUsnsRange;
    expectedUsnsRange.reserve(syncChunkCount);

    for (int i = 0; i < syncChunkCount; ++i) {
        const auto syncChunk = generateSyncChunk(i * 18, (i + 1) * 18 - 1);
        const auto jsonObject = qevercloud::serializeToJson(syncChunk);
        expectedSyncChunks << syncChunk;

        const auto json =
            QJsonDocument(jsonObject).toJson(QJsonDocument::Indented);

        const qint32 lowUsn = i * 18;
        const qint32 highUsn = (i + 1) * 18 - 1;
        expectedUsnsRange << std::make_pair(lowUsn, highUsn);

        const auto fileName = QString::number(lowUsn) + QStringLiteral("_") +
            QString::number(highUsn) + QStringLiteral(".json");

        QFile file{ownSubdir.absoluteFilePath(fileName)};
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(json);
        file.close();
    }

    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    const auto syncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_EQ(syncChunks, expectedSyncChunks);

    const auto usnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();
    EXPECT_EQ(usnsRange.size(), syncChunkCount);
    for (int i = 0; i < syncChunkCount; ++i) {
        EXPECT_EQ(usnsRange[i].first, utils::syncChunkLowUsn(syncChunks[i]));

        ASSERT_TRUE(syncChunks[i].chunkHighUSN());
        EXPECT_EQ(usnsRange[i].second, *syncChunks[i].chunkHighUSN());
    }
}

TEST_F(SyncChunksStorageTest, FetchExistingLinkedNotebookSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> expectedSyncChunks;
    expectedSyncChunks.reserve(syncChunkCount * linkedNotebookCount);

    QList<std::pair<qint32, qint32>> expectedUsnsRange;
    expectedUsnsRange.reserve(syncChunkCount * linkedNotebookCount);

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        for (int i = 0; i < syncChunkCount; ++i) {
            const auto syncChunk = generateSyncChunk(i * 18, (i + 1) * 18 - 1);
            const auto jsonObject = qevercloud::serializeToJson(syncChunk);
            expectedSyncChunks << syncChunk;

            const auto json =
                QJsonDocument(jsonObject).toJson(QJsonDocument::Indented);

            const qint32 lowUsn = i * 18;
            const qint32 highUsn = (i + 1) * 18 - 1;
            expectedUsnsRange << std::make_pair(lowUsn, highUsn);

            const auto fileName = QString::number(lowUsn) +
                QStringLiteral("_") + QString::number(highUsn) +
                QStringLiteral(".json");

            temporaryDir.mkpath(
                temporaryDir.absoluteFilePath(linkedNotebookGuid));

            QDir linkedNotebookSubdir{
                temporaryDir.absoluteFilePath(linkedNotebookGuid)};

            temporaryDir.mkpath(linkedNotebookSubdir.absolutePath());

            QFile file{linkedNotebookSubdir.absoluteFilePath(fileName)};
            ASSERT_TRUE(file.open(QIODevice::WriteOnly));
            file.write(json);
            file.close();
        }
    }

    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount * linkedNotebookCount);

    QList<std::pair<qint32, qint32>> usnsRange;
    usnsRange.reserve(syncChunkCount * linkedNotebookCount);

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        syncChunks << storage->fetchRelevantLinkedNotebookSyncChunks(
            linkedNotebookGuid, 0);

        usnsRange << storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
            linkedNotebookGuid);
    }

    EXPECT_EQ(syncChunks, expectedSyncChunks);
    EXPECT_EQ(usnsRange, expectedUsnsRange);
}

TEST_F(SyncChunksStorageTest, PutAndFetchUserOwnSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    QList<std::pair<qint32, qint32>> usnsRange;
    usnsRange.reserve(syncChunkCount);

    for (int i = 0; i < syncChunkCount; ++i) {
        const qint32 lowUsn = i * 18;
        const qint32 highUsn = (i + 1) * 18 - 1;
        usnsRange << std::make_pair(lowUsn, highUsn);
        syncChunks << generateSyncChunk(lowUsn, highUsn);
    }

    storage->putUserOwnSyncChunks(syncChunks);

    // Should not flush user own sync chunks to the temporary dir immediately
    checkSyncChunksPersistenceDirIsEmpty(temporaryDir);

    auto fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_EQ(fetchedSyncChunks, syncChunks);

    auto fetchedUsnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();

    EXPECT_EQ(fetchedUsnsRange, usnsRange);

    // Now make sure that flush would actually write the sync chunks to dir
    storage->flush();

    checkUserOwnSyncChunksPersistenceIsNotEmpty(temporaryDir);

    // Also make sure that fetch continues to work after flushing
    fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_EQ(fetchedSyncChunks, syncChunks);

    fetchedUsnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();

    EXPECT_EQ(fetchedUsnsRange, usnsRange);
}

TEST_F(SyncChunksStorageTest, PutEmptyUserOwnSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    // Should do nothing on empty list of user own sync chunks
    storage->putUserOwnSyncChunks(QList<qevercloud::SyncChunk>{});
    storage->flush();

    checkSyncChunksPersistenceDirIsEmpty(temporaryDir);
}

TEST_F(SyncChunksStorageTest, PutAndFetchLinkedNotebookSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    QHash<qevercloud::Guid, QList<qevercloud::SyncChunk>>
        syncChunksPerLinkedNotebookGuid;

    QList<std::pair<qint32, qint32>> usnsRange;
    usnsRange.reserve(syncChunkCount);

    QHash<qevercloud::Guid, QList<std::pair<qint32, qint32>>>
        usnsRangePerLinkedNotebookGuid;

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        syncChunks.clear();
        usnsRange.clear();
        for (int i = 0; i < syncChunkCount; ++i) {
            const qint32 lowUsn = i * 18;
            const qint32 highUsn = (i + 1) * 18 - 1;
            usnsRange << std::make_pair(lowUsn, highUsn);
            syncChunks << generateSyncChunk(lowUsn, highUsn);
        }
        storage->putLinkedNotebookSyncChunks(linkedNotebookGuid, syncChunks);

        syncChunksPerLinkedNotebookGuid[linkedNotebookGuid] = syncChunks;
        usnsRangePerLinkedNotebookGuid[linkedNotebookGuid] = usnsRange;
    }

    checkSyncChunksPersistenceDirIsEmpty(temporaryDir);

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        {
            const auto it =
                syncChunksPerLinkedNotebookGuid.find(linkedNotebookGuid);

            ASSERT_FALSE(it == syncChunksPerLinkedNotebookGuid.end());

            const auto fetchedSyncChunks =
                storage->fetchRelevantLinkedNotebookSyncChunks(
                    linkedNotebookGuid, 0);

            EXPECT_EQ(fetchedSyncChunks, it.value());
        }

        {
            const auto it =
                usnsRangePerLinkedNotebookGuid.find(linkedNotebookGuid);

            ASSERT_FALSE(it == usnsRangePerLinkedNotebookGuid.end());

            const auto fetchedUsnsRange =
                storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
                    linkedNotebookGuid);

            EXPECT_EQ(fetchedUsnsRange, it.value());
        }
    }

    // Now make sure that flush would actually write the sync chunks to dir
    storage->flush();

    checkLinkedNotebookSyncChunksPersistenceIsNotEmpty(temporaryDir);

    // Also make sure that fetch continues to work after flushing
    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        {
            const auto it =
                syncChunksPerLinkedNotebookGuid.find(linkedNotebookGuid);

            ASSERT_FALSE(it == syncChunksPerLinkedNotebookGuid.end());

            const auto fetchedSyncChunks =
                storage->fetchRelevantLinkedNotebookSyncChunks(
                    linkedNotebookGuid, 0);

            EXPECT_EQ(fetchedSyncChunks, it.value());
        }

        {
            const auto it =
                usnsRangePerLinkedNotebookGuid.find(linkedNotebookGuid);

            ASSERT_FALSE(it == usnsRangePerLinkedNotebookGuid.end());

            const auto fetchedUsnsRange =
                storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
                    linkedNotebookGuid);

            EXPECT_EQ(fetchedUsnsRange, it.value());
        }
    }
}

TEST_F(SyncChunksStorageTest, PutEmptyLinkedNotebookSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    // Should do nothing on empty list of linked notebook sync chunks
    storage->putLinkedNotebookSyncChunks(
        UidGenerator::Generate(), QList<qevercloud::SyncChunk>{});
    storage->flush();

    checkSyncChunksPersistenceDirIsEmpty(temporaryDir);
}

TEST_F(
    SyncChunksStorageTest,
    ProtectFromPuttingUserOwnSyncChunksWithInconsistentUsnsOrder)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    QList<std::pair<qint32, qint32>> usnsRange;
    usnsRange.reserve(syncChunkCount);

    for (int i = 0; i < syncChunkCount; ++i) {
        const qint32 lowUsn = i * 18;
        const qint32 highUsn = (i + 1) * 18 - 1;
        usnsRange << std::make_pair(lowUsn, highUsn);
        syncChunks << generateSyncChunk(lowUsn, highUsn);
    }

    storage->putUserOwnSyncChunks(syncChunks);

    const auto newPutSyncChunks = QList<qevercloud::SyncChunk>{}
        << syncChunks[2];

    storage->putUserOwnSyncChunks(newPutSyncChunks);

    // Should not flush user own sync chunks to the temporary dir immediately
    checkSyncChunksPersistenceDirIsEmpty(temporaryDir);

    const auto fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_TRUE(fetchedSyncChunks.isEmpty());

    const auto fetchedUsnsRange =
        storage->fetchUserOwnSyncChunksLowAndHighUsns();

    EXPECT_TRUE(fetchedUsnsRange.isEmpty());

    // Now try to flush and ensure that nothing was really flushed to dir
    storage->flush();

    checkSyncChunksPersistenceDirIsEmpty(temporaryDir);
}

TEST_F(
    SyncChunksStorageTest,
    ProtectFromPuttingLinkedNotebookSyncChunksWithInconsistentUsnsOrder)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    QList<std::pair<qint32, qint32>> usnsRange;
    usnsRange.reserve(syncChunkCount);

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        syncChunks.clear();
        usnsRange.clear();
        for (int i = 0; i < syncChunkCount; ++i) {
            const qint32 lowUsn = i * 18;
            const qint32 highUsn = (i + 1) * 18 - 1;
            usnsRange << std::make_pair(lowUsn, highUsn);
            syncChunks << generateSyncChunk(lowUsn, highUsn);
        }
        storage->putLinkedNotebookSyncChunks(linkedNotebookGuid, syncChunks);
    }

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        const auto newPutSyncChunks = QList<qevercloud::SyncChunk>{}
            << generateSyncChunk(36, 53);

        storage->putLinkedNotebookSyncChunks(
            linkedNotebookGuid, newPutSyncChunks);

        const auto fetchedSyncChunks =
            storage->fetchRelevantLinkedNotebookSyncChunks(
                linkedNotebookGuid, 0);

        EXPECT_TRUE(fetchedSyncChunks.isEmpty());

        const auto fetchedUsnsRange =
            storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
                linkedNotebookGuid);

        EXPECT_TRUE(fetchedUsnsRange.isEmpty());
    }
}

TEST_F(
    SyncChunksStorageTest,
    FetchUserOwnSyncChunksConsideringAfterUsnMatchingSyncChunkBoundary)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);
    for (int i = 0; i < syncChunkCount; ++i) {
        syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
    }

    storage->putUserOwnSyncChunks(syncChunks);
    const auto fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(17);
    syncChunks.removeAt(0);
    EXPECT_EQ(fetchedSyncChunks, syncChunks);
}

TEST_F(
    SyncChunksStorageTest,
    FetchUserOwnSyncChunksConsideringAfterUsnNotMatchingSyncChunkBoundary)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);
    for (int i = 0; i < syncChunkCount; ++i) {
        syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
    }

    storage->putUserOwnSyncChunks(syncChunks);

    const qint32 afterUsn = 7;
    const auto fetchedSyncChunks =
        storage->fetchRelevantUserOwnSyncChunks(afterUsn);

    auto & firstSyncChunk = syncChunks[0];

    const auto removeLowUsnItems = [&](auto & items) {
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

    ASSERT_TRUE(firstSyncChunk.notes());
    removeLowUsnItems(*firstSyncChunk.mutableNotes());
    if (firstSyncChunk.notes()->isEmpty()) {
        firstSyncChunk.setNotes(std::nullopt);
    }

    ASSERT_TRUE(firstSyncChunk.notebooks());
    removeLowUsnItems(*firstSyncChunk.mutableNotebooks());
    if (firstSyncChunk.notebooks()->isEmpty()) {
        firstSyncChunk.setNotebooks(std::nullopt);
    }

    ASSERT_TRUE(firstSyncChunk.tags());
    removeLowUsnItems(*firstSyncChunk.mutableTags());
    if (firstSyncChunk.tags()->isEmpty()) {
        firstSyncChunk.setTags(std::nullopt);
    }

    ASSERT_TRUE(firstSyncChunk.searches());
    removeLowUsnItems(*firstSyncChunk.mutableSearches());
    if (firstSyncChunk.searches()->isEmpty()) {
        firstSyncChunk.setSearches(std::nullopt);
    }

    ASSERT_TRUE(firstSyncChunk.resources());
    removeLowUsnItems(*firstSyncChunk.mutableResources());
    if (firstSyncChunk.resources()->isEmpty()) {
        firstSyncChunk.setResources(std::nullopt);
    }

    ASSERT_TRUE(firstSyncChunk.linkedNotebooks());
    removeLowUsnItems(*firstSyncChunk.mutableLinkedNotebooks());
    if (firstSyncChunk.linkedNotebooks()->isEmpty()) {
        firstSyncChunk.setLinkedNotebooks(std::nullopt);
    }

    EXPECT_EQ(fetchedSyncChunks, syncChunks);
}

TEST_F(
    SyncChunksStorageTest,
    FetchLinkedNotebookSyncChunksConsideringAfterUsnMatchingSyncChunkBoundary)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    QHash<qevercloud::Guid, QList<qevercloud::SyncChunk>>
        syncChunksPerLinkedNotebookGuid;

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        syncChunks.clear();
        for (int i = 0; i < syncChunkCount; ++i) {
            syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
        }
        storage->putLinkedNotebookSyncChunks(linkedNotebookGuid, syncChunks);

        syncChunksPerLinkedNotebookGuid[linkedNotebookGuid] = syncChunks;
    }

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        const auto it =
            syncChunksPerLinkedNotebookGuid.find(linkedNotebookGuid);

        ASSERT_FALSE(it == syncChunksPerLinkedNotebookGuid.end());

        const auto fetchedSyncChunks =
            storage->fetchRelevantLinkedNotebookSyncChunks(
                linkedNotebookGuid, 17);

        syncChunks = it.value();
        syncChunks.removeAt(0);
        EXPECT_EQ(fetchedSyncChunks, syncChunks);
    }
}

TEST_F(
    SyncChunksStorageTest,
    FetchLinkedNotebookSyncChunksConsideringAfterUsnNotMatchingSyncChunkBoundary)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    QHash<qevercloud::Guid, QList<qevercloud::SyncChunk>>
        syncChunksPerLinkedNotebookGuid;

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        syncChunks.clear();
        for (int i = 0; i < syncChunkCount; ++i) {
            syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
        }
        storage->putLinkedNotebookSyncChunks(linkedNotebookGuid, syncChunks);

        syncChunksPerLinkedNotebookGuid[linkedNotebookGuid] = syncChunks;
    }

    const qint32 afterUsn = 7;

    const auto removeLowUsnItems = [&](auto & items) {
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

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        const auto it =
            syncChunksPerLinkedNotebookGuid.find(linkedNotebookGuid);

        ASSERT_FALSE(it == syncChunksPerLinkedNotebookGuid.end());

        syncChunks = it.value();
        ASSERT_FALSE(syncChunks.isEmpty());

        auto & firstSyncChunk = syncChunks[0];

        ASSERT_TRUE(firstSyncChunk.notes());
        removeLowUsnItems(*firstSyncChunk.mutableNotes());
        if (firstSyncChunk.notes()->isEmpty()) {
            firstSyncChunk.setNotes(std::nullopt);
        }

        ASSERT_TRUE(firstSyncChunk.notebooks());
        removeLowUsnItems(*firstSyncChunk.mutableNotebooks());
        if (firstSyncChunk.notebooks()->isEmpty()) {
            firstSyncChunk.setNotebooks(std::nullopt);
        }

        ASSERT_TRUE(firstSyncChunk.tags());
        removeLowUsnItems(*firstSyncChunk.mutableTags());
        if (firstSyncChunk.tags()->isEmpty()) {
            firstSyncChunk.setTags(std::nullopt);
        }

        ASSERT_TRUE(firstSyncChunk.searches());
        removeLowUsnItems(*firstSyncChunk.mutableSearches());
        if (firstSyncChunk.searches()->isEmpty()) {
            firstSyncChunk.setSearches(std::nullopt);
        }

        ASSERT_TRUE(firstSyncChunk.resources());
        removeLowUsnItems(*firstSyncChunk.mutableResources());
        if (firstSyncChunk.resources()->isEmpty()) {
            firstSyncChunk.setResources(std::nullopt);
        }

        ASSERT_TRUE(firstSyncChunk.linkedNotebooks());
        removeLowUsnItems(*firstSyncChunk.mutableLinkedNotebooks());
        if (firstSyncChunk.linkedNotebooks()->isEmpty()) {
            firstSyncChunk.setLinkedNotebooks(std::nullopt);
        }

        const auto fetchedSyncChunks =
            storage->fetchRelevantLinkedNotebookSyncChunks(
                linkedNotebookGuid, afterUsn);

        EXPECT_EQ(fetchedSyncChunks, syncChunks);
    }
}

TEST_F(SyncChunksStorageTest, ClearUserOwnSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);
    for (int i = 0; i < syncChunkCount; ++i) {
        syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
    }

    storage->putUserOwnSyncChunks(syncChunks);
    auto fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_FALSE(fetchedSyncChunks.isEmpty());

    auto fetchedUsnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();
    EXPECT_FALSE(fetchedUsnsRange.isEmpty());

    storage->clearUserOwnSyncChunks();
    fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_TRUE(fetchedSyncChunks.isEmpty());

    fetchedUsnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();
    EXPECT_TRUE(fetchedUsnsRange.isEmpty());
}

TEST_F(SyncChunksStorageTest, ClearLinkedNotebookSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        syncChunks.clear();
        for (int i = 0; i < syncChunkCount; ++i) {
            syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
        }
        storage->putLinkedNotebookSyncChunks(linkedNotebookGuid, syncChunks);
    }

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        auto fetchedSyncChunks = storage->fetchRelevantLinkedNotebookSyncChunks(
            linkedNotebookGuid, 0);

        EXPECT_FALSE(fetchedSyncChunks.isEmpty());

        auto fetchedUsnsRange =
            storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
                linkedNotebookGuid);

        EXPECT_FALSE(fetchedUsnsRange.isEmpty());

        storage->clearLinkedNotebookSyncChunks(linkedNotebookGuid);

        fetchedSyncChunks = storage->fetchRelevantLinkedNotebookSyncChunks(
            linkedNotebookGuid, 0);

        EXPECT_TRUE(fetchedSyncChunks.isEmpty());

        fetchedUsnsRange = storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
            linkedNotebookGuid);

        EXPECT_TRUE(fetchedUsnsRange.isEmpty());
    }
}

TEST_F(SyncChunksStorageTest, ClearAllSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    auto storage = std::make_shared<SyncChunksStorage>(temporaryDir);

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount);

    for (int i = 0; i < syncChunkCount; ++i) {
        syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
    }

    storage->putUserOwnSyncChunks(syncChunks);

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        syncChunks.clear();
        for (int i = 0; i < syncChunkCount; ++i) {
            syncChunks << generateSyncChunk(i * 18, (i + 1) * 18 - 1);
        }
        storage->putLinkedNotebookSyncChunks(linkedNotebookGuid, syncChunks);
    }

    auto fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_FALSE(fetchedSyncChunks.isEmpty());

    auto fetchedUsnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();
    EXPECT_FALSE(fetchedUsnsRange.isEmpty());

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        fetchedSyncChunks = storage->fetchRelevantLinkedNotebookSyncChunks(
            linkedNotebookGuid, 0);

        EXPECT_FALSE(fetchedSyncChunks.isEmpty());

        fetchedUsnsRange = storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
            linkedNotebookGuid);

        EXPECT_FALSE(fetchedUsnsRange.isEmpty());
    }

    storage->clearAllSyncChunks();

    fetchedSyncChunks = storage->fetchRelevantUserOwnSyncChunks(0);
    EXPECT_TRUE(fetchedSyncChunks.isEmpty());

    fetchedUsnsRange = storage->fetchUserOwnSyncChunksLowAndHighUsns();
    EXPECT_TRUE(fetchedUsnsRange.isEmpty());

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        fetchedSyncChunks = storage->fetchRelevantLinkedNotebookSyncChunks(
            linkedNotebookGuid, 0);

        EXPECT_TRUE(fetchedSyncChunks.isEmpty());

        fetchedUsnsRange = storage->fetchLinkedNotebookSyncChunksLowAndHighUsns(
            linkedNotebookGuid);

        EXPECT_TRUE(fetchedUsnsRange.isEmpty());
    }
}

} // namespace quentier::synchronization::tests
