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

#include <qevercloud/exceptions/builders/EDAMSystemExceptionBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

#include <array>
#include <utility>

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

struct NotesProcessorCallback final : public INotesProcessor::ICallback
{
    void onProcessedNote(
        const qevercloud::Guid & noteGuid,
        qint32 noteUpdateSequenceNum) noexcept override
    {
        m_processedNoteGuidsAndUsns[noteGuid] = noteUpdateSequenceNum;
    }

    void onExpungedNote(const qevercloud::Guid & noteGuid) noexcept override
    {
        m_expungedNoteGuids.insert(noteGuid);
    }

    void onFailedToExpungeNote(
        const qevercloud::Guid & noteGuid,
        const QException & e) noexcept override
    {
        m_guidsWhichFailedToExpunge
            << std::make_pair(noteGuid, std::shared_ptr<QException>(e.clone()));
    }

    void onNoteFailedToDownload(
        const qevercloud::Note & note,
        const QException & e) noexcept override
    {
        m_notesWhichFailedToDownload
            << std::make_pair(note, std::shared_ptr<QException>(e.clone()));
    }

    void onNoteFailedToProcess(
        const qevercloud::Note & note,
        const QException & e) noexcept override
    {
        m_notesWhichFailedToProcess
            << std::make_pair(note, std::shared_ptr<QException>(e.clone()));
    }

    void onNoteProcessingCancelled(
        const qevercloud::Note & note) noexcept override
    {
        m_cancelledNotes << note;
    }

    QHash<qevercloud::Guid, qint32> m_processedNoteGuidsAndUsns;
    QSet<qevercloud::Guid> m_expungedNoteGuids;

    using GuidWithException =
        std::pair<qevercloud::Guid, std::shared_ptr<QException>>;

    using NoteWithException =
        std::pair<qevercloud::Note, std::shared_ptr<QException>>;

    QList<GuidWithException> m_guidsWhichFailedToExpunge;
    QList<NoteWithException> m_notesWhichFailedToDownload;
    QList<NoteWithException> m_notesWhichFailedToProcess;
    QList<qevercloud::Note> m_cancelledNotes;
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

    const auto callback = std::make_shared<NotesProcessorCallback>();

    auto future = notesProcessor->processNotes(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(status.totalNewNotes, 0UL);
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);
    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());
    EXPECT_TRUE(status.processedNoteGuidsAndUsns.isEmpty());
    EXPECT_TRUE(status.cancelledNoteGuidsAndUsns.isEmpty());
    EXPECT_TRUE(status.expungedNoteGuids.isEmpty());

    EXPECT_TRUE(callback->m_notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_guidsWhichFailedToExpunge.isEmpty());
    EXPECT_TRUE(callback->m_processedNoteGuidsAndUsns.isEmpty());
    EXPECT_TRUE(callback->m_cancelledNotes.isEmpty());
    EXPECT_TRUE(callback->m_expungedNoteGuids.isEmpty());
}

class NotesProcessorTestWithLinkedNotebookParam :
    public NotesProcessorTest,
    public testing::WithParamInterface<std::optional<qevercloud::Guid>>
{};

const std::array g_test_linked_notebook_guids{
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

    const auto callback = std::make_shared<NotesProcessorCallback>();

    auto future = notesProcessor->processNotes(syncChunks, callback);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    ASSERT_EQ(notesPutIntoLocalStorage.size(), notes.size());
    for (int i = 0, size = notes.size(); i < size; ++i) {
        const auto noteWithContent = addContentToNote(notes[i], i);
        EXPECT_EQ(notesPutIntoLocalStorage[i], noteWithContent);
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();
    EXPECT_EQ(status.totalNewNotes, static_cast<quint64>(notes.size()));
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);
    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());
    EXPECT_TRUE(status.cancelledNoteGuidsAndUsns.isEmpty());
    EXPECT_TRUE(status.expungedNoteGuids.isEmpty());

    ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), notes.size());

    for (const auto & note: qAsConst(notes)) {
        const auto it =
            status.processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, status.processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }

    EXPECT_TRUE(callback->m_notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(callback->m_notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_guidsWhichFailedToExpunge.isEmpty());
    EXPECT_TRUE(callback->m_cancelledNotes.isEmpty());
    EXPECT_TRUE(callback->m_expungedNoteGuids.isEmpty());

    ASSERT_EQ(callback->m_processedNoteGuidsAndUsns.size(), notes.size());

    for (const auto & note: qAsConst(notes)) {
        const auto it =
            callback->m_processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, callback->m_processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }
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

            if (it->updateSequenceNum().value() == 2) {
                return threading::makeExceptionalFuture<qevercloud::Note>(
                    RuntimeError{
                        ErrorString{"Failed to download full note data"}});
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

    const auto callback = std::make_shared<NotesProcessorCallback>();

    auto future = notesProcessor->processNotes(syncChunks, callback);
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

    EXPECT_EQ(notesPutIntoLocalStorage, expectedProcessedNotes);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewNotes, static_cast<quint64>(notes.size()));
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);

    ASSERT_EQ(status.notesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(status.notesWhichFailedToDownload[0].note, notes[1]);

    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());
    EXPECT_TRUE(status.cancelledNoteGuidsAndUsns.isEmpty());
    EXPECT_TRUE(status.expungedNoteGuids.isEmpty());

    ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), notes.size() - 1);

    for (const auto & note: qAsConst(notes)) {
        if (note.updateSequenceNum().value() == 2) {
            continue;
        }

        const auto it =
            status.processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, status.processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }

    ASSERT_EQ(callback->m_notesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(callback->m_notesWhichFailedToDownload[0].first, notes[1]);

    EXPECT_TRUE(callback->m_notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(callback->m_guidsWhichFailedToExpunge.isEmpty());
    EXPECT_TRUE(callback->m_cancelledNotes.isEmpty());
    EXPECT_TRUE(callback->m_expungedNoteGuids.isEmpty());

    ASSERT_EQ(callback->m_processedNoteGuidsAndUsns.size(), notes.size() - 1);

    for (const auto & note: qAsConst(notes)) {
        if (note.updateSequenceNum().value() == 2) {
            continue;
        }

        const auto it =
            callback->m_processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, callback->m_processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }
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

    EXPECT_EQ(notesPutIntoLocalStorage, expectedProcessedNotes);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(
        status.totalNewNotes,
        static_cast<quint64>(expectedProcessedNotes.size()));

    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());

    ASSERT_EQ(status.notesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(status.notesWhichFailedToProcess[0].note, notes[1]);

    ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), notes.size() - 1);

    for (const auto & note: qAsConst(notes)) {
        if (note.guid() == notes[1].guid()) {
            continue;
        }

        const auto it =
            status.processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, status.processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }
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

    EXPECT_EQ(notesPutIntoLocalStorage, expectedProcessedNotes);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewNotes, static_cast<quint64>(notes.size()));
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());

    ASSERT_EQ(status.notesWhichFailedToProcess.size(), 1);

    EXPECT_EQ(
        status.notesWhichFailedToProcess[0].note,
        addContentToNote(notes[1], 1));

    ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), notes.size() - 1);

    for (const auto & note: qAsConst(notes)) {
        if (note.guid() == notes[1].guid()) {
            continue;
        }

        const auto it =
            status.processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, status.processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }
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

    EXPECT_EQ(notesPutIntoLocalStorage, expectedProcessedNotes);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(
        status.totalNewNotes,
        static_cast<quint64>(expectedProcessedNotes.size()));

    EXPECT_EQ(status.totalUpdatedNotes, 1UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());

    ASSERT_EQ(status.notesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(status.notesWhichFailedToProcess[0].note, notes[1]);

    ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), notes.size() - 1);

    for (const auto & note: qAsConst(notes)) {
        if (note.guid() == notes[1].guid()) {
            continue;
        }

        const auto it =
            status.processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, status.processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }
}

TEST_F(NotesProcessorTest, CancelFurtherNoteDownloadingOnApiRateLimitExceeding)
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

    QList<qevercloud::Note> notesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const auto addContentToNote = [](qevercloud::Note note,
                                     const int index) -> qevercloud::Note {
        note.setContent(
            QString::fromUtf8("<en-note>Hello world from note #%1</en-note>")
                .arg(index));
        return note;
    };

    const std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt;

    EXPECT_CALL(*m_mockNoteStore, linkedNotebookGuid)
        .WillRepeatedly(ReturnRef(linkedNotebookGuid));

    QList<std::shared_ptr<QPromise<std::optional<qevercloud::Note>>>>
        findNoteByGuidPromises;

    findNoteByGuidPromises.reserve(notes.size());

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
            EXPECT_EQ(it, notesPutIntoLocalStorage.constEnd());
            if (it != notesPutIntoLocalStorage.constEnd()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Note>>(*it);
            }

            findNoteByGuidPromises << std::make_shared<
                QPromise<std::optional<qevercloud::Note>>>();

            findNoteByGuidPromises.back()->start();
            return findNoteByGuidPromises.back()->future();
        });

    int downloadFullNoteDataCallCount = 0;
    EXPECT_CALL(*m_mockNoteFullDataDownloader, downloadFullNoteData)
        .WillRepeatedly([&](qevercloud::Guid noteGuid,
                            const INoteFullDataDownloader::IncludeNoteLimits
                                includeNoteLimitsOption,
                            const qevercloud::IRequestContextPtr & ctx) {
            Q_UNUSED(ctx)

            ++downloadFullNoteDataCallCount;

            EXPECT_EQ(
                includeNoteLimitsOption,
                INoteFullDataDownloader::IncludeNoteLimits::No);

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
                    qevercloud::EDAMSystemExceptionBuilder{}
                        .setErrorCode(
                            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED)
                        .build());
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
    ASSERT_FALSE(future.isFinished());
    EXPECT_EQ(downloadFullNoteDataCallCount, 0);

    ASSERT_EQ(findNoteByGuidPromises.size(), notes.size());
    for (int i = 0; i < 2; ++i) {
        findNoteByGuidPromises[i]->addResult(std::nullopt);
        findNoteByGuidPromises[i]->finish();
    }

    QCoreApplication::processEvents();

    ASSERT_FALSE(future.isFinished());
    EXPECT_EQ(downloadFullNoteDataCallCount, 2);

    for (int i = 2; i < notes.size(); ++i) {
        findNoteByGuidPromises[i]->addResult(std::nullopt);
        findNoteByGuidPromises[i]->finish();
    }

    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();

    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(downloadFullNoteDataCallCount, 2);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewNotes, 2UL);
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());

    ASSERT_EQ(status.notesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(status.notesWhichFailedToDownload[0].note, notes[1]);

    bool caughtEdamSystemExceptionWithRateLimit = false;
    try {
        status.notesWhichFailedToDownload[0].exception->raise();
    }
    catch (const qevercloud::EDAMSystemException & e) {
        if (e.errorCode() == qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
            caughtEdamSystemExceptionWithRateLimit = true;
        }
    }
    catch (...) {
    }

    EXPECT_TRUE(caughtEdamSystemExceptionWithRateLimit);

    ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), 1);

    EXPECT_EQ(
        status.processedNoteGuidsAndUsns.begin().key(),
        notes[0].guid().value());

    EXPECT_EQ(
        status.processedNoteGuidsAndUsns.begin().value(),
        notes[0].updateSequenceNum().value());

    ASSERT_EQ(status.cancelledNoteGuidsAndUsns.size(), notes.size() - 2);
    for (const auto & note: qAsConst(notes)) {
        if (note.guid() == notes[0].guid() || note.guid() == notes[1].guid()) {
            continue;
        }

        const auto it =
            status.cancelledNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, status.cancelledNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }
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

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewNotes, 0UL);
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(
        status.totalExpungedNotes,
        static_cast<quint64>(expungedNoteGuids.size()));

    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());
}

TEST_F(NotesProcessorTest, TolerateFailuresToExpungeNotes)
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
            if (noteGuid == expungedNoteGuids[1]) {
                return threading::makeExceptionalFuture<void>(
                    RuntimeError{ErrorString{"failed to expunge note"}});
            }
            return threading::makeReadyFuture();
        });

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    EXPECT_EQ(processedNoteGuids, expungedNoteGuids);

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewNotes, 0UL);
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(
        status.totalExpungedNotes,
        static_cast<quint64>(expungedNoteGuids.size()));

    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());

    ASSERT_EQ(status.noteGuidsWhichFailedToExpunge.size(), 1);
    EXPECT_EQ(
        status.noteGuidsWhichFailedToExpunge[0].guid, expungedNoteGuids[1]);
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
    EXPECT_EQ(status.totalNewNotes, 0UL);
    EXPECT_EQ(status.totalUpdatedNotes, 0UL);
    EXPECT_EQ(
        status.totalExpungedNotes,
        static_cast<quint64>(expungedNoteGuids.size()));
    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());
}

class NotesProcessorTestWithConflict :
    public NotesProcessorTest,
    public testing::WithParamInterface<
        ISyncConflictResolver::NoteConflictResolution>
{};

const std::array gConflictResolutions{
    ISyncConflictResolver::NoteConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseTheirs{}},
    ISyncConflictResolver::NoteConflictResolution{
        ISyncConflictResolver::ConflictResolution::UseMine{}},
    ISyncConflictResolver::NoteConflictResolution{
        ISyncConflictResolver::ConflictResolution::IgnoreMine{}},
    ISyncConflictResolver::NoteConflictResolution{
        ISyncConflictResolver::ConflictResolution::MoveMine<qevercloud::Note>{
            qevercloud::Note{}}}};

INSTANTIATE_TEST_SUITE_P(
    NotesProcessorTestWithConflictInstance, NotesProcessorTestWithConflict,
    testing::ValuesIn(gConflictResolutions));

TEST_P(NotesProcessorTestWithConflict, HandleConflictByGuid)
{
    const auto notebookGuid = UidGenerator::Generate();

    auto note = qevercloud::NoteBuilder{}
                    .setGuid(UidGenerator::Generate())
                    .setNotebookGuid(notebookGuid)
                    .setUpdateSequenceNum(1)
                    .setTitle(QStringLiteral("Note #1"))
                    .build();

    const auto localConflict =
        qevercloud::NoteBuilder{}
            .setGuid(note.guid())
            .setTitle(note.title())
            .setUpdateSequenceNum(note.updateSequenceNum().value() - 1)
            .setLocallyFavorited(true)
            .build();

    QList<qevercloud::Note> notesPutIntoLocalStorage;
    QSet<qevercloud::Guid> triedGuids;

    const std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt;

    EXPECT_CALL(*m_mockNoteStore, linkedNotebookGuid)
        .WillRepeatedly(ReturnRef(linkedNotebookGuid));

    EXPECT_CALL(*m_mockLocalStorage, findNoteByGuid)
        .WillRepeatedly([&](const qevercloud::Guid & guid,
                            const local_storage::ILocalStorage::FetchNoteOptions
                                fetchNoteOptions) {
            Q_UNUSED(fetchNoteOptions)

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

            if (guid == note.guid()) {
                return threading::makeReadyFuture<
                    std::optional<qevercloud::Note>>(localConflict);
            }

            return threading::makeReadyFuture<std::optional<qevercloud::Note>>(
                std::nullopt);
        });

    auto resolution = GetParam();
    std::optional<qevercloud::Note> movedLocalConflict;
    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Note>>(resolution))
    {
        movedLocalConflict =
            qevercloud::NoteBuilder{}
                .setTitle(
                    localConflict.title().value() + QStringLiteral("_moved"))
                .build();

        resolution = ISyncConflictResolver::NoteConflictResolution{
            ISyncConflictResolver::ConflictResolution::MoveMine<
                qevercloud::Note>{*movedLocalConflict}};
    }

    EXPECT_CALL(*m_mockSyncConflictResolver, resolveNoteConflict)
        .WillOnce([&, resolution](
                      const qevercloud::Note & theirs,
                      const qevercloud::Note & mine) mutable {
            EXPECT_EQ(theirs, note);
            EXPECT_EQ(mine, localConflict);
            return threading::makeReadyFuture<
                ISyncConflictResolver::NoteConflictResolution>(
                std::move(resolution));
        });

    EXPECT_CALL(*m_mockLocalStorage, putNote)
        .WillRepeatedly(
            [&, conflictGuid = note.guid()](const qevercloud::Note & note) {
                if (Q_UNLIKELY(!note.guid())) {
                    if (std::holds_alternative<
                            ISyncConflictResolver::ConflictResolution::MoveMine<
                                qevercloud::Note>>(resolution))
                    {
                        notesPutIntoLocalStorage << note;
                        return threading::makeReadyFuture();
                    }

                    return threading::makeExceptionalFuture<void>(RuntimeError{
                        ErrorString{"Detected note without guid"}});
                }

                EXPECT_TRUE(
                    triedGuids.contains(*note.guid()) ||
                    (movedLocalConflict && movedLocalConflict == note));

                notesPutIntoLocalStorage << note;
                return threading::makeReadyFuture();
            });

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseTheirs>(resolution))
    {
        note.setLocalId(localConflict.localId());
    }

    auto notes = QList<qevercloud::Note>{}
        << note
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

    const auto originalNotesSize = notes.size();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotes(notes).build();

    const auto addContentToNote = [](qevercloud::Note note,
                                     const int index) -> qevercloud::Note {
        note.setContent(
            QString::fromUtf8("<en-note>Hello world from note #%1</en-note>")
                .arg(index));
        return note;
    };

    EXPECT_CALL(*m_mockNoteFullDataDownloader, downloadFullNoteData)
        .WillRepeatedly([&](qevercloud::Guid noteGuid,
                            const INoteFullDataDownloader::IncludeNoteLimits
                                includeNoteLimitsOption,
                            const qevercloud::IRequestContextPtr & ctx) {
            Q_UNUSED(ctx)

            EXPECT_EQ(
                includeNoteLimitsOption,
                INoteFullDataDownloader::IncludeNoteLimits::No);

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

    const auto notesProcessor = std::make_shared<NotesProcessor>(
        m_mockLocalStorage, m_mockSyncConflictResolver,
        m_mockNoteFullDataDownloader, m_mockNoteStore);

    auto future = notesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    EXPECT_NO_THROW(future.waitForFinished());

    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        notes.removeAt(0);
    }
    else if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                        MoveMine<qevercloud::Note>>(resolution))
    {
        ASSERT_TRUE(movedLocalConflict);
        notes.push_front(*movedLocalConflict);
    }

    ASSERT_EQ(notesPutIntoLocalStorage.size(), notes.size());
    if (std::holds_alternative<
            ISyncConflictResolver::ConflictResolution::UseMine>(resolution))
    {
        for (int i = 0, size = notes.size(); i < size; ++i) {
            const auto noteWithContent = addContentToNote(notes[i], i + 1);
            EXPECT_EQ(notesPutIntoLocalStorage[i], noteWithContent);
        }
    }
    else if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                        MoveMine<qevercloud::Note>>(resolution))
    {
        ASSERT_FALSE(notesPutIntoLocalStorage.isEmpty());
        EXPECT_EQ(notesPutIntoLocalStorage[0], notes[0]);
        for (int i = 1, size = notes.size(); i < size; ++i) {
            const auto noteWithContent = addContentToNote(notes[i], i - 1);
            EXPECT_EQ(notesPutIntoLocalStorage[i], noteWithContent);
        }
    }
    else {
        for (int i = 0, size = notes.size(); i < size; ++i) {
            const auto noteWithContent = addContentToNote(notes[i], i);
            EXPECT_EQ(notesPutIntoLocalStorage[i], noteWithContent);
        }
    }

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(
        status.totalNewNotes, static_cast<quint64>(originalNotesSize - 1));

    EXPECT_EQ(status.totalUpdatedNotes, 1UL);
    EXPECT_EQ(status.totalExpungedNotes, 0UL);

    EXPECT_TRUE(status.notesWhichFailedToDownload.isEmpty());
    EXPECT_TRUE(status.notesWhichFailedToProcess.isEmpty());
    EXPECT_TRUE(status.noteGuidsWhichFailedToExpunge.isEmpty());
    EXPECT_TRUE(status.cancelledNoteGuidsAndUsns.isEmpty());

    if (std::holds_alternative<ISyncConflictResolver::ConflictResolution::
                                   MoveMine<qevercloud::Note>>(resolution))
    {
        ASSERT_EQ(status.processedNoteGuidsAndUsns.size() + 1, notes.size());
    }
    else {
        ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), notes.size());
    }

    for (const auto & note: qAsConst(notes)) {
        if (!note.guid()) {
            ASSERT_TRUE(movedLocalConflict);
            EXPECT_EQ(note, *movedLocalConflict);
            continue;
        }

        const auto it =
            status.processedNoteGuidsAndUsns.find(note.guid().value());

        ASSERT_NE(it, status.processedNoteGuidsAndUsns.end());
        EXPECT_EQ(it.value(), note.updateSequenceNum().value());
    }
}

} // namespace quentier::synchronization::tests
