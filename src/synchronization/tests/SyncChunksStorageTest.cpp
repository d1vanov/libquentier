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

#include <synchronization/sync_chunks/SyncChunksStorage.h>

#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/serialization/json/SyncChunk.h>
#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QJsonDocument>
#include <QList>
#include <QTemporaryDir>

#include <gtest/gtest.h>

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
                .setGuid(UidGenerator::Generate())
                .build();

        if (!result.notes()) {
            result.setNotes(QList<qevercloud::Note>{} << note);
        }
        else {
            result.mutableNotes()->append(note);
        }
    }

    return result;
}

} // namespace

class SyncChunksStorageTest : public testing::Test
{
protected:
    void TearDown() override
    {
        QDir dir{m_temporaryDir.path()};
        const auto entries = dir.entryInfoList(QDir::NoDotAndDotDot);
        for (const auto & entry: qAsConst(entries)) {
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

TEST_F(SyncChunksStorageTest, FetchNonexistentUserOwnSyncChunks)
{
    SyncChunksStorage storage{QDir{m_temporaryDir.path()}};
    const auto syncChunks = storage.fetchRelevantUserOwnSyncChunks(0);
    EXPECT_TRUE(syncChunks.empty());
}

TEST_F(SyncChunksStorageTest, FetchNonexistentLinkedNotebookSyncChunks)
{
    SyncChunksStorage storage{QDir{m_temporaryDir.path()}};
    const auto syncChunks = storage.fetchRelevantLinkedNotebookSyncChunks(
        UidGenerator::Generate(), 0);

    EXPECT_TRUE(syncChunks.empty());
}

TEST_F(SyncChunksStorageTest, FetchExistingUserOwnSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    SyncChunksStorage storage{temporaryDir};

    QDir ownSubdir{temporaryDir.absoluteFilePath(QStringLiteral("user_own"))};

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> expectedSyncChunks;
    expectedSyncChunks.reserve(syncChunkCount);
    for (int i = 0; i < syncChunkCount; ++i) {
        const auto syncChunk = generateSyncChunk(i * 12, (i + 1) * 12);
        const auto jsonObject = qevercloud::serializeToJson(syncChunk);
        expectedSyncChunks << syncChunk;

        const auto json =
            QJsonDocument(jsonObject).toJson(QJsonDocument::Indented);

        const auto fileName = QString::number(i * 12) + QStringLiteral("_") +
            QString::number((i + 1) * 12) + QStringLiteral(".json");

        QFile file{ownSubdir.absoluteFilePath(fileName)};
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(json);
        file.close();
    }

    const auto syncChunks = storage.fetchRelevantUserOwnSyncChunks(0);
    EXPECT_EQ(syncChunks, expectedSyncChunks);
}

TEST_F(SyncChunksStorageTest, FetchExistingLinkedNotebookSyncChunks)
{
    QDir temporaryDir{m_temporaryDir.path()};
    SyncChunksStorage storage{temporaryDir};

    constexpr int linkedNotebookCount = 3;
    QList<qevercloud::Guid> linkedNotebookGuids;
    linkedNotebookGuids.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebookGuids << UidGenerator::Generate();
    }

    constexpr int syncChunkCount = 3;
    QList<qevercloud::SyncChunk> expectedSyncChunks;
    expectedSyncChunks.reserve(syncChunkCount * linkedNotebookCount);

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        for (int i = 0; i < syncChunkCount; ++i) {
            const auto syncChunk = generateSyncChunk(i * 12, (i + 1) * 12);
            const auto jsonObject = qevercloud::serializeToJson(syncChunk);
            expectedSyncChunks << syncChunk;

            const auto json =
                QJsonDocument(jsonObject).toJson(QJsonDocument::Indented);

            const auto fileName = QString::number(i * 12) + QStringLiteral("_") +
                QString::number((i + 1) * 12) + QStringLiteral(".json");

            temporaryDir.mkpath(
                temporaryDir.absoluteFilePath(linkedNotebookGuid));

            QDir linkedNotebookSubdir{
                temporaryDir.absoluteFilePath(linkedNotebookGuid)};

            QFile file{linkedNotebookSubdir.absoluteFilePath(fileName)};
            ASSERT_TRUE(file.open(QIODevice::WriteOnly));
            file.write(json);
            file.close();
        }
    }

    QList<qevercloud::SyncChunk> syncChunks;
    syncChunks.reserve(syncChunkCount * linkedNotebookCount);
    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        syncChunks << storage.fetchRelevantLinkedNotebookSyncChunks(
            linkedNotebookGuid, 0);
    }

    EXPECT_EQ(syncChunks, expectedSyncChunks);
}

} // namespace quentier::synchronization::tests
