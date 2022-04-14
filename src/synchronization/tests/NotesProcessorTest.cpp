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

#include <array>

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
            nullptr, m_mockSyncConflictResolver, m_mockNoteFullDataDownloader,
            m_mockNoteStore),
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

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(status.m_totalNewNotes, 0UL);
    EXPECT_EQ(status.m_totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.m_totalExpungedNotes, 0UL);
    EXPECT_TRUE(status.m_notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.m_notesWhichFailedToProcess.isEmpty());
}

class NotesProcessorTestWithLinkedNotebookParam :
    public NotesProcessorTest,
    public testing::WithParamInterface<std::optional<qevercloud::Guid>>
{};

std::array g_test_linked_notebook_guids{
    std::optional<qevercloud::Guid>{},
    std::make_optional<qevercloud::Guid>(UidGenerator::Generate())};

INSTANTIATE_TEST_SUITE_P(
    NotesProcessorTestWithLinkedNotebookParamInstance,
    NotesProcessorTestWithLinkedNotebookParam,
    testing::ValuesIn(g_test_linked_notebook_guids));

TEST_P(NotesProcessorTestWithLinkedNotebookParam, ProcessNotesWithoutConflicts)
{
    const auto linkedNotebookGuid = GetParam();

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

    const auto addContentToNote = [](qevercloud::Note note,
                                     const int index) -> qevercloud::Note {
        note.setContent(
            QString::fromUtf8("<en-note>Hello world from note #%1</en-note>")
                .arg(index));
        return note;
    };

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            using FetchNoteOptions =
                local_storage::ILocalStorage::FetchNoteOptions;
            using FetchNoteOption =
                local_storage::ILocalStorage::FetchNoteOption;

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

            if (linkedNotebookGuid) {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::Yes);
            }
            else {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::No);
            }

            const auto it = std::find_if(
                notes.begin(), notes.end(), [&](const qevercloud::Note & note) {
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
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"Detected note without guid"}});
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

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(status.m_totalNewNotes, static_cast<quint64>(notes.size()));
    EXPECT_EQ(status.m_totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.m_totalExpungedNotes, 0UL);
    EXPECT_TRUE(status.m_notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.m_notesWhichFailedToProcess.isEmpty());
}

TEST_P(
    NotesProcessorTestWithLinkedNotebookParam,
    TolerateFailuresToDownloadFullNoteData)
{
    const auto linkedNotebookGuid = GetParam();

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

    const auto addContentToNote = [](qevercloud::Note note,
                                     const int index) -> qevercloud::Note {
        note.setContent(
            QString::fromUtf8("<en-note>Hello world from note #%1</en-note>")
                .arg(index));
        return note;
    };

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            using FetchNoteOptions =
                local_storage::ILocalStorage::FetchNoteOptions;
            using FetchNoteOption =
                local_storage::ILocalStorage::FetchNoteOption;

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

            if (linkedNotebookGuid) {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::Yes);
            }
            else {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::No);
            }

            const auto it = std::find_if(
                notes.begin(), notes.end(), [&](const qevercloud::Note & note) {
                    return note.guid() && (*note.guid() == noteGuid);
                });
            if (Q_UNLIKELY(it == notes.end())) {
                return threading::makeExceptionalFuture<qevercloud::Note>(
                    RuntimeError{ErrorString{
                        "Detected attempt to download unrecognized note"}});
            }

            const int index =
                static_cast<int>(std::distance(notes.begin(), it));

            if (it->updateSequenceNum().value() == 2) {
                return threading::makeExceptionalFuture<qevercloud::Note>(
                    RuntimeError{
                        ErrorString{"Failed to download full note data"}});
            }

            return threading::makeReadyFuture<qevercloud::Note>(
                addContentToNote(*it, index));
        });

    EXPECT_CALL(*m_mockLocalStorage, putNote)
        .WillRepeatedly([&](const qevercloud::Note & note) {
            if (Q_UNLIKELY(!note.guid())) {
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"Detected note without guid"}});
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

    const QList<qevercloud::Note> expectedProcessedNotes = [&] {
        auto n = notes;
        int i = 0;
        for (auto & note: n) {
            note = addContentToNote(note, i);
            ++i;
        }

        n.removeAt(1);
        return n;
    }();

    ASSERT_EQ(notesPutIntoLocalStorage.size(), expectedProcessedNotes.size());
    for (int i = 0, size = expectedProcessedNotes.size(); i < size; ++i) {
        EXPECT_EQ(notesPutIntoLocalStorage[i], expectedProcessedNotes[i]);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.m_totalNewNotes, static_cast<quint64>(notes.size()));
    EXPECT_EQ(status.m_totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.m_totalExpungedNotes, 0UL);

    ASSERT_EQ(status.m_notesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(status.m_notesWhichFailedToDownload[0].first, notes[1]);

    EXPECT_TRUE(status.m_notesWhichFailedToProcess.isEmpty());
}

TEST_P(
    NotesProcessorTestWithLinkedNotebookParam,
    TolerateFailuresToFindNoteByGuidInLocalStorage)
{
    const auto linkedNotebookGuid = GetParam();

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

    const auto addContentToNote = [](qevercloud::Note note,
                                     const int index) -> qevercloud::Note {
        note.setContent(
            QString::fromUtf8("<en-note>Hello world from note #%1</en-note>")
                .arg(index));
        return note;
    };

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            using FetchNoteOptions =
                local_storage::ILocalStorage::FetchNoteOptions;
            using FetchNoteOption =
                local_storage::ILocalStorage::FetchNoteOption;

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

            if (guid == notes[1].guid().value()) {
                return threading::makeExceptionalFuture<
                    std::optional<qevercloud::Note>>(RuntimeError{ErrorString{
                    "Failed to find note by guid in the local storage"}});
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

            if (linkedNotebookGuid) {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::Yes);
            }
            else {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::No);
            }

            const auto it = std::find_if(
                notes.begin(), notes.end(), [&](const qevercloud::Note & note) {
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
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"Detected note without guid"}});
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

    const QList<qevercloud::Note> expectedProcessedNotes = [&] {
        auto n = notes;
        int i = 0;
        for (auto & note: n) {
            note = addContentToNote(note, i);
            ++i;
        }

        n.removeAt(1);
        return n;
    }();

    ASSERT_EQ(notesPutIntoLocalStorage.size(), expectedProcessedNotes.size());
    for (int i = 0, size = expectedProcessedNotes.size(); i < size; ++i) {
        EXPECT_EQ(notesPutIntoLocalStorage[i], expectedProcessedNotes[i]);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(
        status.m_totalNewNotes,
        static_cast<quint64>(expectedProcessedNotes.size()));

    EXPECT_EQ(status.m_totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.m_totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.m_notesWhichFailedToDownload.isEmpty());

    ASSERT_EQ(status.m_notesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(status.m_notesWhichFailedToProcess[0].first, notes[1]);
}

TEST_P(
    NotesProcessorTestWithLinkedNotebookParam,
    TolerateFailuresToPutNoteIntoLocalStorage)
{
    const auto linkedNotebookGuid = GetParam();

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

    const auto addContentToNote = [](qevercloud::Note note,
                                     const int index) -> qevercloud::Note {
        note.setContent(
            QString::fromUtf8("<en-note>Hello world from note #%1</en-note>")
                .arg(index));
        return note;
    };

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            using FetchNoteOptions =
                local_storage::ILocalStorage::FetchNoteOptions;
            using FetchNoteOption =
                local_storage::ILocalStorage::FetchNoteOption;

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

            if (linkedNotebookGuid) {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::Yes);
            }
            else {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::No);
            }

            const auto it = std::find_if(
                notes.begin(), notes.end(), [&](const qevercloud::Note & note) {
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
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"Detected note without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*note.guid()));

            if (note.guid() == notes[1].guid()) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Failed to put note into local storage"}});
            }

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

    const QList<qevercloud::Note> expectedProcessedNotes = [&] {
        auto n = notes;
        int i = 0;
        for (auto & note: n) {
            note = addContentToNote(note, i);
            ++i;
        }

        n.removeAt(1);
        return n;
    }();

    ASSERT_EQ(notesPutIntoLocalStorage.size(), expectedProcessedNotes.size());
    for (int i = 0, size = expectedProcessedNotes.size(); i < size; ++i) {
        EXPECT_EQ(notesPutIntoLocalStorage[i], expectedProcessedNotes[i]);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.m_totalNewNotes, static_cast<quint64>(notes.size()));
    EXPECT_EQ(status.m_totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.m_totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.m_notesWhichFailedToDownload.isEmpty());

    ASSERT_EQ(status.m_notesWhichFailedToProcess.size(), 1);

    EXPECT_EQ(
        status.m_notesWhichFailedToProcess[0].first,
        addContentToNote(notes[1], 1));
}

TEST_P(
    NotesProcessorTestWithLinkedNotebookParam,
    TolerateFailuresToResolveNoteConflicts)
{
    const auto linkedNotebookGuid = GetParam();

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

    const auto addContentToNote = [](qevercloud::Note note,
                                     const int index) -> qevercloud::Note {
        note.setContent(
            QString::fromUtf8("<en-note>Hello world from note #%1</en-note>")
                .arg(index));
        return note;
    };

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            using FetchNoteOptions =
                local_storage::ILocalStorage::FetchNoteOptions;
            using FetchNoteOption =
                local_storage::ILocalStorage::FetchNoteOption;

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

            if (guid == notes[1].guid().value()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Note>>(notes[1]);
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

            if (linkedNotebookGuid) {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::Yes);
            }
            else {
                EXPECT_EQ(
                    includeNoteLimitsOption,
                    INoteFullDataDownloader::IncludeNoteLimits::No);
            }

            const auto it = std::find_if(
                notes.begin(), notes.end(), [&](const qevercloud::Note & note) {
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
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"Detected note without guid"}});
            }

            EXPECT_TRUE(triedGuids.contains(*note.guid()));

            if (note.guid() == notes[1].guid()) {
                return threading::makeExceptionalFuture<void>(RuntimeError{
                    ErrorString{"Failed to put note into local storage"}});
            }

            notesPutIntoLocalStorage << note;
            return threading::makeReadyFuture();
        });

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveNoteConflict)
        .WillOnce([&](const qevercloud::Note & theirs,
                      const qevercloud::Note & mine) mutable {
            EXPECT_EQ(theirs, notes[1]);
            EXPECT_EQ(mine, notes[1]);
            return threading::makeExceptionalFuture<
                ISyncConflictResolver::NoteConflictResolution>(
                RuntimeError{ErrorString{"Failed to resolve notes conflict"}});
        });

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotes(notes).build();

    const auto notesProcessor = std::make_shared<NotesProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_mockNoteFullDataDownloader, m_mockNoteStore);

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    const QList<qevercloud::Note> expectedProcessedNotes = [&] {
        auto n = notes;
        int i = 0;
        for (auto & note: n) {
            note = addContentToNote(note, i);
            ++i;
        }

        n.removeAt(1);
        return n;
    }();

    ASSERT_EQ(notesPutIntoLocalStorage.size(), expectedProcessedNotes.size());
    for (int i = 0, size = expectedProcessedNotes.size(); i < size; ++i) {
        EXPECT_EQ(notesPutIntoLocalStorage[i], expectedProcessedNotes[i]);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(
        status.m_totalNewNotes,
        static_cast<quint64>(expectedProcessedNotes.size()));

    EXPECT_EQ(status.m_totalUpdatedNotes, 1UL);
    EXPECT_EQ(status.m_totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.m_notesWhichFailedToDownload.isEmpty());

    ASSERT_EQ(status.m_notesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(status.m_notesWhichFailedToProcess[0].first, notes[1]);
}

TEST_F(NotesProcessorTest, ProcessExpungedNotes)
{
    const auto expungedNoteGuids = QList<qevercloud::Guid>{}
        << UidGenerator::Generate() << UidGenerator::Generate()
        << UidGenerator::Generate();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setExpungedNotes(expungedNoteGuids)
               .build();

    const auto notesProcessor = std::make_shared<NotesProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_mockNoteFullDataDownloader, m_mockNoteStore);

    QList<qevercloud::Guid> processedNoteGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & noteGuid) {
            processedNoteGuids << noteGuid;
            return threading::makeReadyFuture();
        });

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedNoteGuids, expungedNoteGuids);
}

TEST_F(NotesProcessorTest, FilterOutExpungedNotesFromSyncChunkNotes)
{
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

    const auto expungedNoteGuids = [&] {
        QList<qevercloud::Guid> guids;
        guids.reserve(notes.size());
        for (const auto & note: qAsConst(notes)) {
            guids << note.guid().value();
        }
        return guids;
    }();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setNotes(notes)
               .setExpungedNotes(expungedNoteGuids)
               .build();

    const auto notesProcessor = std::make_shared<NotesProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_mockNoteFullDataDownloader, m_mockNoteStore);

    QList<qevercloud::Guid> processedNoteGuids;
    EXPECT_CALL(*m_mockLocalStorage, expungeNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & noteGuid) {
            processedNoteGuids << noteGuid;
            return threading::makeReadyFuture();
        });

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedNoteGuids, expungedNoteGuids);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(status.m_totalNewNotes, 0UL);
    EXPECT_EQ(status.m_totalUpdatedNotes, 0UL);
    EXPECT_EQ(
        status.m_totalExpungedNotes,
        static_cast<quint64>(expungedNoteGuids.size()));
    EXPECT_TRUE(status.m_notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.m_notesWhichFailedToProcess.isEmpty());
}

} // namespace quentier::synchronization::tests
