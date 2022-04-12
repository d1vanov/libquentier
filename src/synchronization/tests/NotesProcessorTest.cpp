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

#include <synchronization/processors/NotesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

// NOTE: strange but with these headers moved higher, above "quentier/" ones,
// build fails. Probably something related to #pragma once not being perfectly
// implemented in the compiler.
#include <synchronization/tests/mocks/MockINoteFullDataDownloader.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::ReturnRef;
using testing::StrictMock;

class NotesProcessorTest : public testing::Test
{
protected:
    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();

    const std::shared_ptr<mocks::MockINoteFullDataDownloader>
        m_mockNoteFullDataDownloader =
            std::make_shared<StrictMock<mocks::MockINoteFullDataDownloader>>();

    const std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();
};

TEST_F(NotesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver,
            m_mockNoteFullDataDownloader, m_mockNoteStore));
}

TEST_F(NotesProcessorTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            nullptr, m_mockSyncConflictResolver,
            m_mockNoteFullDataDownloader, m_mockNoteStore),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, CtorNullSyncConflictResolver)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, nullptr, m_mockNoteFullDataDownloader,
            m_mockNoteStore),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, CtorNullNoteFullDataDownloader)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver, nullptr,
            m_mockNoteStore),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, CtorNullNoteStore)
{
    EXPECT_THROW(
        const auto notesProcessor = std::make_shared<NotesProcessor>(
            m_mockLocalStorage, m_mockSyncConflictResolver,
            m_mockNoteFullDataDownloader, nullptr),
        InvalidArgument);
}

TEST_F(NotesProcessorTest, ProcessSyncChunksWithoutNotesToProcess)
{
    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.build();

    const auto notesProcessor = std::make_shared<NotesProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_mockNoteFullDataDownloader, m_mockNoteStore);

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());
}

TEST_F(NotesProcessorTest, ProcessNotesWithoutConflicts)
{
    const auto linkedNotebookGuid =
        std::make_optional<qevercloud::Guid>(UidGenerator::Generate());

    EXPECT_CALL(*m_mockNoteStore, linkedNotebookGuid)
        .WillRepeatedly(ReturnRef(linkedNotebookGuid));

    const auto notebookGuid = UidGenerator::Generate();

    const auto notes = QList<qevercloud::Note>{}
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(1)
               .setTitle(QStringLiteral("Note #1"))
               .build()
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(2)
               .setTitle(QStringLiteral("Note #2"))
               .build()
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(3)
               .setTitle(QStringLiteral("Note #3"))
               .build()
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(4)
               .setTitle(QStringLiteral("Note #4"))
               .build();

    QList<qevercloud::Note> notesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addContentToNote =
        [](qevercloud::Note note, const int index) -> qevercloud::Note
        {
            note.setContent(QString::fromUtf8(
                "<en-note>Hello world from note #%1</en-note>").arg(index));
            return note;
        };

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            using FetchNoteOptions = local_storage::ILocalStorage::FetchNoteOptions;
            using FetchNoteOption = local_storage::ILocalStorage::FetchNoteOption;

            EXPECT_EQ(
                fetchNoteOptions,
                FetchNoteOptions{} | FetchNoteOption::WithResourceMetadata);

            EXPECT_FALSE(triedGuids.contains(guid));
            triedGuids.insert(guid);

            const auto it = std::find_if(
                notesPutIntoLocalStorage.constBegin(),
                notesPutIntoLocalStorage.constEnd(),
                [&](const qevercloud::Note & note) {
                    return note.guid() && (*note.guid() == guid);
                });
            if (it != notesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Note>>(*it);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt);
        });

    EXPECT_CALL(*m_mockNoteFullDataDownloader, downloadFullNoteData)
        .WillRepeatedly([&](qevercloud::Guid noteGuid,
                            const INoteFullDataDownloader::IncludeNoteLimits
                                includeNoteLimitsOption,
                            const qevercloud::IRequestContextPtr & ctx) {
            Q_UNUSED(ctx)

            EXPECT_EQ(
                includeNoteLimitsOption,
                INoteFullDataDownloader::IncludeNoteLimits::Yes);

            const auto it = std::find_if(
                notes.begin(),
                notes.end(),
                [&](const qevercloud::Note & note) {
                    return note.guid() && (*note.guid() == noteGuid);
                });
            if (Q_UNLIKELY(it == notes.end())) {
                return threading::makeExceptionalFuture<qevercloud::Note>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized note"}});
            }

            const int index =
                static_cast<int>(std::distance(notes.begin(), it));

            return threading::makeReadyFuture<qevercloud::Note>(
                addContentToNote(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putNote)
        .WillRepeatedly([&](const qevercloud::Note & note) {
            if (Q_UNLIKELY(!note.guid())) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Detected note without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*note.guid()));

            notesPutIntoLocalStorage << note;
            return threading::makeReadyFuture();
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotes(notes).build();

    const auto notesProcessor = std::make_shared<NotesProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_mockNoteFullDataDownloader, m_mockNoteStore);

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    ASSERT_EQ(notesPutIntoLocalStorage.size(), notes.size());
    for (int i = 0, size = notes.size(); i < size; ++i) {
        const auto noteWithContent = addContentToNote(notes[i], i);
        EXPECT_EQ(notesPutIntoLocalStorage[i], noteWithContent);
    }
}

} // namespace quentier::synchronization::tests
