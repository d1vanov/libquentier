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

#include <synchronization/processors/DurableNotesProcessor.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/tests/mocks/MockINotesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace {

template <class T>
QList<T> sorted(QList<T> lst)
{
    std::sort(lst.begin(), lst.end());
    return lst;
}

[[nodiscard]] QList<qevercloud::Note> generateTestNotes(
    const qint32 startUsn, const qint32 endUsn)
{
    EXPECT_GE(endUsn, startUsn);
    if (endUsn < startUsn) {
        return {};
    }

    const auto notebookGuid = UidGenerator::Generate();

    QList<qevercloud::Note> result;
    result.reserve(endUsn - startUsn + 1);
    for (qint32 i = startUsn; i <= endUsn; ++i) {
        result << qevercloud::NoteBuilder{}
                      .setGuid(UidGenerator::Generate())
                      .setNotebookGuid(notebookGuid)
                      .setUpdateSequenceNum(i)
                      .setTitle(QString::fromUtf8("Note #%1").arg(i))
                      .build();
    }

    return result;
}

[[nodiscard]] QList<qevercloud::Guid> generateTestGuids(const qint32 count)
{
    QList<qevercloud::Guid> result;
    result.reserve(count);
    for (qint32 i = 0; i < count; ++i) {
        result << UidGenerator::Generate();
    }

    return result;
}

[[nodiscard]] QHash<qevercloud::Guid, qint32> generateTestProcessedNotesInfo(
    const qint32 startUsn, const qint32 endUsn)
{
    EXPECT_GT(endUsn, startUsn);
    if (endUsn < startUsn) {
        return {};
    }

    QHash<qevercloud::Guid, qint32> result;
    result.reserve(endUsn - startUsn + 1);
    for (qint32 i = startUsn; i <= endUsn; ++i) {
        result[UidGenerator::Generate()] = i;
    }

    return result;
}

MATCHER_P(
    EqSyncChunksWithSortedNotes, syncChunks,
    "Check sync chunks with sorted notes equality")
{
    const auto sortSyncChunkNotes = [](qevercloud::SyncChunk & syncChunk) {
        if (!syncChunk.notes()) {
            return;
        }

        std::sort(
            syncChunk.mutableNotes()->begin(), syncChunk.mutableNotes()->end(),
            [](const qevercloud::Note & lhs, const qevercloud::Note & rhs) {
                return lhs.updateSequenceNum() < rhs.updateSequenceNum();
            });
    };

    QList<qevercloud::SyncChunk> argSyncChunks = arg;
    for (auto & syncChunk: argSyncChunks) {
        sortSyncChunkNotes(syncChunk);
    }

    QList<qevercloud::SyncChunk> expectedSyncChunks = syncChunks;
    for (auto & syncChunk: expectedSyncChunks) {
        sortSyncChunkNotes(syncChunk);
    }

    testing::Matcher<QList<qevercloud::SyncChunk>> matcher =
        testing::Eq(expectedSyncChunks);
    return matcher.MatchAndExplain(argSyncChunks, result_listener);
}

MATCHER_P(
    EqSyncChunksWithSortedExpungedNotes, syncChunks,
    "Check sync chunks with sorted expunged notes equality")
{
    const auto sortSyncChunkExpungedNotes =
        [](qevercloud::SyncChunk & syncChunk) {
            if (!syncChunk.expungedNotes()) {
                return;
            }

            std::sort(
                syncChunk.mutableExpungedNotes()->begin(),
                syncChunk.mutableExpungedNotes()->end());
        };

    QList<qevercloud::SyncChunk> argSyncChunks = arg;
    for (auto & syncChunk: argSyncChunks) {
        sortSyncChunkExpungedNotes(syncChunk);
    }

    QList<qevercloud::SyncChunk> expectedSyncChunks = syncChunks;
    for (auto & syncChunk: expectedSyncChunks) {
        sortSyncChunkExpungedNotes(syncChunk);
    }

    testing::Matcher<QList<qevercloud::SyncChunk>> matcher =
        testing::Eq(expectedSyncChunks);
    return matcher.MatchAndExplain(argSyncChunks, result_listener);
}

} // namespace

class DurableNotesProcessorTest : public testing::Test
{
protected:
    void TearDown() override
    {
        QDir dir{m_temporaryDir.path()};
        const auto entries =
            dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

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
    const std::shared_ptr<mocks::MockINotesProcessor> m_mockNotesProcessor =
        std::make_shared<StrictMock<mocks::MockINotesProcessor>>();

    QTemporaryDir m_temporaryDir;
};

TEST_F(DurableNotesProcessorTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto durableNotesProcessor =
            std::make_shared<DurableNotesProcessor>(
                m_mockNotesProcessor, QDir{m_temporaryDir.path()}));
}

TEST_F(DurableNotesProcessorTest, CtorNullNotesProcessor)
{
    EXPECT_THROW(
        const auto durableNotesProcessor =
            std::make_shared<DurableNotesProcessor>(
                nullptr, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(DurableNotesProcessorTest, ProcessSyncChunksWithoutPreviousSyncInfo)
{
    const auto notes = generateTestNotes(1, 4);

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotes(notes).build();

    const auto durableNotesProcessor = std::make_shared<DurableNotesProcessor>(
        m_mockNotesProcessor, QDir{m_temporaryDir.path()});

    EXPECT_CALL(*m_mockNotesProcessor, processNotes)
        .WillOnce([&](const QList<qevercloud::SyncChunk> & syncChunks,
                      const INotesProcessor::ICallbackWeakPtr & callbackWeak) {
            const auto callback = callbackWeak.lock();
            EXPECT_TRUE(callback);

            const QList<qevercloud::Note> syncChunkNotes = [&] {
                QList<qevercloud::Note> result;
                for (const auto & syncChunk: qAsConst(syncChunks)) {
                    result << utils::collectNotesFromSyncChunk(syncChunk);
                }
                return result;
            }();

            EXPECT_EQ(syncChunkNotes, notes);

            DownloadNotesStatus status;
            status.m_totalNewNotes =
                static_cast<quint64>(syncChunkNotes.size());

            for (const auto & note: qAsConst(notes)) {
                status.m_processedNoteGuidsAndUsns[note.guid().value()] =
                    note.updateSequenceNum().value();

                if (callback) {
                    callback->onProcessedNote(
                        note.guid().value(), note.updateSequenceNum().value());
                }
            }

            return threading::makeReadyFuture<DownloadNotesStatus>(
                std::move(status));
        });

    auto future = durableNotesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.m_totalNewNotes, notes.size());
    ASSERT_EQ(status.m_processedNoteGuidsAndUsns.size(), notes.size());
    for (const auto & note: qAsConst(notes)) {
        const auto it =
            status.m_processedNoteGuidsAndUsns.find(note.guid().value());

        EXPECT_NE(it, status.m_processedNoteGuidsAndUsns.end());
        if (it != status.m_processedNoteGuidsAndUsns.end()) {
            EXPECT_EQ(it.value(), note.updateSequenceNum().value());
        }
    }

    QDir lastSyncNotesDir = [&] {
        QDir syncPersistentStorageDir{m_temporaryDir.path()};
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{lastSyncDataDir.absoluteFilePath(QStringLiteral("notes"))};
    }();

    const auto processedNotesInfo =
        utils::processedNotesInfoFromLastSync(lastSyncNotesDir);
    ASSERT_EQ(processedNotesInfo.size(), notes.size());

    for (const auto it: qevercloud::toRange(qAsConst(processedNotesInfo))) {
        const auto sit = status.m_processedNoteGuidsAndUsns.find(it.key());

        EXPECT_NE(sit, status.m_processedNoteGuidsAndUsns.end());
        if (sit != status.m_processedNoteGuidsAndUsns.end()) {
            EXPECT_EQ(sit.value(), it.value());
        }
    }
}

TEST_F(
    DurableNotesProcessorTest,
    HandleDifferentCallbacksDuringSyncChunksProcessing)
{
    const auto notes = generateTestNotes(1, 5);
    const auto expungedNotes = generateTestGuids(4);

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}
               .setNotes(notes)
               .setExpungedNotes(expungedNotes)
               .build();

    const auto durableNotesProcessor = std::make_shared<DurableNotesProcessor>(
        m_mockNotesProcessor, QDir{m_temporaryDir.path()});

    EXPECT_CALL(*m_mockNotesProcessor, processNotes)
        .WillOnce([&](const QList<qevercloud::SyncChunk> & syncChunks,
                      const INotesProcessor::ICallbackWeakPtr & callbackWeak) {
            const auto callback = callbackWeak.lock();
            EXPECT_TRUE(callback);

            const QList<qevercloud::Note> syncChunkNotes = [&] {
                QList<qevercloud::Note> result;
                for (const auto & syncChunk: qAsConst(syncChunks)) {
                    result << utils::collectNotesFromSyncChunk(syncChunk);
                }
                return result;
            }();

            EXPECT_EQ(syncChunkNotes, notes);

            EXPECT_EQ(syncChunkNotes.size(), 5);
            if (syncChunkNotes.size() != 5) {
                return threading::makeExceptionalFuture<DownloadNotesStatus>(
                    RuntimeError{ErrorString{"Invalid note count"}});
            }

            DownloadNotesStatus status;
            status.m_totalNewNotes =
                static_cast<quint64>(syncChunkNotes.size());

            // First note gets marked as a successfully processed one
            status
                .m_processedNoteGuidsAndUsns[syncChunkNotes[0].guid().value()] =
                syncChunkNotes[0].updateSequenceNum().value();

            if (callback) {
                callback->onProcessedNote(
                    *syncChunkNotes[0].guid(),
                    *syncChunkNotes[0].updateSequenceNum());
            }

            // Second note is marked as failed to process one
            status.m_notesWhichFailedToProcess
                << DownloadNotesStatus::NoteWithException{
                       syncChunkNotes[1],
                       std::make_shared<RuntimeError>(
                           ErrorString{"Failed to process note"})};

            if (callback) {
                callback->onNoteFailedToProcess(
                    status.m_notesWhichFailedToProcess.last().first,
                    *status.m_notesWhichFailedToProcess.last().second);
            }

            // Third note is marked as failed to download one
            status.m_notesWhichFailedToDownload
                << DownloadNotesStatus::NoteWithException{
                       syncChunkNotes[2],
                       std::make_shared<RuntimeError>(
                           ErrorString{"Failed to download note"})};

            if (callback) {
                callback->onNoteFailedToDownload(
                    status.m_notesWhichFailedToDownload.last().first,
                    *status.m_notesWhichFailedToDownload.last().second);
            }

            // Fourth and fifth notes are marked as cancelled because, for
            // example, the download error was API rate limit exceeding.
            for (int i = 3; i < 5; ++i) {
                status.m_cancelledNoteGuidsAndUsns
                    [syncChunkNotes[i].guid().value()] =
                    syncChunkNotes[i].updateSequenceNum().value();

                if (callback) {
                    callback->onNoteProcessingCancelled(syncChunkNotes[i]);
                }
            }

            const QList<qevercloud::Guid> syncChunkExpungedNotes = [&] {
                QList<qevercloud::Guid> result;
                for (const auto & syncChunk: qAsConst(syncChunks)) {
                    result << utils::collectExpungedNoteGuidsFromSyncChunk(
                        syncChunk);
                }
                return result;
            }();

            EXPECT_EQ(syncChunkExpungedNotes, expungedNotes);

            EXPECT_EQ(syncChunkExpungedNotes.size(), 4);
            if (syncChunkExpungedNotes.size() != 4) {
                return threading::makeExceptionalFuture<DownloadNotesStatus>(
                    RuntimeError{ErrorString{"Invalid expunged note count"}});
            }

            status.m_totalExpungedNotes =
                static_cast<quint64>(syncChunkExpungedNotes.size());

            // First two expunged notes are marked as successfully processed
            // ones
            status.m_expungedNoteGuids = QList<qevercloud::Guid>{}
                << syncChunkExpungedNotes[0] << syncChunkExpungedNotes[1];

            if (callback) {
                callback->onExpungedNote(syncChunkExpungedNotes[0]);
                callback->onExpungedNote(syncChunkExpungedNotes[1]);
            }

            // Other two expunged notes are marked as failed to expunged ones
            for (int i = 2; i < 4; ++i) {
                status.m_noteGuidsWhichFailedToExpunge
                    << DownloadNotesStatus::GuidWithException{
                           syncChunkExpungedNotes[i],
                           std::make_shared<RuntimeError>(
                               ErrorString{"Failed to expunge note"})};

                if (callback) {
                    callback->onFailedToExpungeNote(
                        status.m_noteGuidsWhichFailedToExpunge.last().first,
                        *status.m_noteGuidsWhichFailedToExpunge.last().second);
                }
            }

            return threading::makeReadyFuture<DownloadNotesStatus>(
                std::move(status));
        });

    auto future = durableNotesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.m_totalNewNotes, notes.size());
    EXPECT_EQ(status.m_totalExpungedNotes, expungedNotes.size());

    ASSERT_EQ(status.m_processedNoteGuidsAndUsns.size(), 1);
    EXPECT_EQ(
        status.m_processedNoteGuidsAndUsns.constBegin().key(),
        notes[0].guid().value());
    EXPECT_EQ(
        status.m_processedNoteGuidsAndUsns.constBegin().value(),
        notes[0].updateSequenceNum().value());

    ASSERT_EQ(status.m_notesWhichFailedToProcess.size(), 1);
    EXPECT_EQ(status.m_notesWhichFailedToProcess.constBegin()->first, notes[1]);

    ASSERT_EQ(status.m_notesWhichFailedToDownload.size(), 1);
    EXPECT_EQ(
        status.m_notesWhichFailedToDownload.constBegin()->first, notes[2]);

    ASSERT_EQ(status.m_cancelledNoteGuidsAndUsns.size(), 2);
    for (int i = 3; i < 5; ++i) {
        const auto it = status.m_cancelledNoteGuidsAndUsns.constFind(
            notes[i].guid().value());
        EXPECT_NE(it, status.m_cancelledNoteGuidsAndUsns.constEnd());

        if (it != status.m_cancelledNoteGuidsAndUsns.constEnd()) {
            EXPECT_EQ(it.value(), notes[i].updateSequenceNum().value());
        }
    }

    QDir lastSyncNotesDir = [&] {
        QDir syncPersistentStorageDir{m_temporaryDir.path()};
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{lastSyncDataDir.absoluteFilePath(QStringLiteral("notes"))};
    }();

    const auto processedNotesInfo =
        utils::processedNotesInfoFromLastSync(lastSyncNotesDir);
    ASSERT_EQ(processedNotesInfo.size(), 1);
    EXPECT_EQ(processedNotesInfo.constBegin().key(), notes[0].guid().value());
    EXPECT_EQ(
        processedNotesInfo.constBegin().value(),
        notes[0].updateSequenceNum().value());

    const auto failedToProcessNotes =
        utils::notesWhichFailedToProcessDuringLastSync(lastSyncNotesDir);
    ASSERT_EQ(failedToProcessNotes.size(), 1);
    EXPECT_EQ(*failedToProcessNotes.constBegin(), notes[1]);

    const auto failedToDownloadNotes =
        utils::notesWhichFailedToDownloadDuringLastSync(lastSyncNotesDir);
    ASSERT_EQ(failedToDownloadNotes.size(), 1);
    EXPECT_EQ(*failedToDownloadNotes.constBegin(), notes[2]);

    const auto cancelledNotes = [&] {
        auto cancelledNotes =
            utils::notesCancelledDuringLastSync(lastSyncNotesDir);

        std::sort(
            cancelledNotes.begin(), cancelledNotes.end(),
            [](const qevercloud::Note & lhs, const qevercloud::Note & rhs) {
                return lhs.updateSequenceNum() < rhs.updateSequenceNum();
            });

        return cancelledNotes;
    }();

    ASSERT_EQ(cancelledNotes.size(), 2);
    for (int i = 3, j = 0; i < 5 && j < cancelledNotes.size(); ++i, ++j) {
        EXPECT_EQ(cancelledNotes[j], notes[i]);
    }

    const auto expungedNoteGuids =
        utils::noteGuidsExpungedDuringLastSync(lastSyncNotesDir);
    ASSERT_EQ(expungedNoteGuids.size(), 2);
    EXPECT_TRUE(expungedNoteGuids.contains(expungedNotes[0]));
    EXPECT_TRUE(expungedNoteGuids.contains(expungedNotes[1]));

    const auto failedToExpungeNoteGuids =
        utils::noteGuidsWhichFailedToExpungeDuringLastSync(lastSyncNotesDir);
    ASSERT_EQ(failedToExpungeNoteGuids.size(), 2);
    for (int i = 2; i < 4; ++i) {
        EXPECT_TRUE(failedToExpungeNoteGuids.contains(expungedNotes[i]));
    }
}

struct PreviousNoteSyncTestData
{
    QList<qevercloud::Note> m_notesToProcess;

    QHash<qevercloud::Guid, qint32> m_processedNotesInfo = {};
    QList<qevercloud::Guid> m_expungedNoteGuids = {};

    QList<qevercloud::Note> m_notesWhichFailedToDownloadDuringPreviousSync = {};
    QList<qevercloud::Note> m_notesWhichFailedToProcessDuringPreviousSync = {};
    QList<qevercloud::Note> m_notesCancelledDuringPreviousSync = {};
    QList<qevercloud::Guid> m_noteGuidsWhichFailedToExpungeDuringPreviousSync =
        {};
};

class DurableNotesProcessorTestWithPreviousSyncData :
    public DurableNotesProcessorTest,
    public testing::WithParamInterface<PreviousNoteSyncTestData>
{};

const std::array gTestData{
    PreviousNoteSyncTestData{
        generateTestNotes(14, 17), // m_notesToProcess
    },
    PreviousNoteSyncTestData{
        generateTestNotes(14, 17),            // m_notesToProcess
        generateTestProcessedNotesInfo(1, 4), // m_processedNotesInfo
    },
    PreviousNoteSyncTestData{
        generateTestNotes(14, 17),            // m_notesToProcess
        generateTestProcessedNotesInfo(1, 4), // m_processedNotesInfo
        generateTestGuids(3),                 // m_expungedNoteGuids
    },
    PreviousNoteSyncTestData{
        generateTestNotes(14, 17),            // m_notesToProcess
        generateTestProcessedNotesInfo(1, 4), // m_processedNotesInfo
        generateTestGuids(3),                 // m_expungedNoteGuids
        generateTestNotes(
            5, 7), // m_notesWhichFailedToDownloadDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        generateTestNotes(14, 17),            // m_notesToProcess
        generateTestProcessedNotesInfo(1, 4), // m_processedNotesInfo
        generateTestGuids(3),                 // m_expungedNoteGuids
        generateTestNotes(
            5, 7), // m_notesWhichFailedToDownloadDuringPreviousSync
        generateTestNotes(
            8, 10), // m_notesWhichFailedToProcessDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        generateTestNotes(14, 17),            // m_notesToProcess
        generateTestProcessedNotesInfo(1, 4), // m_processedNotesInfo
        generateTestGuids(3),                 // m_expungedNoteGuids
        generateTestNotes(
            5, 7), // m_notesWhichFailedToDownloadDuringPreviousSync
        generateTestNotes(
            8, 10), // m_notesWhichFailedToProcessDuringPreviousSync
        generateTestNotes(11, 13), // m_notesCancelledDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        generateTestNotes(14, 17),            // m_notesToProcess
        generateTestProcessedNotesInfo(1, 4), // m_processedNotesInfo
        generateTestGuids(3),                 // m_expungedNoteGuids
        generateTestNotes(
            5, 7), // m_notesWhichFailedToDownloadDuringPreviousSync
        generateTestNotes(
            8, 10), // m_notesWhichFailedToProcessDuringPreviousSync
        generateTestNotes(11, 13), // m_notesCancelledDuringPreviousSync
        generateTestGuids(
            3), // m_noteGuidsWhichFailedToExpungeDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        {},                                   // m_notesToProcess
        generateTestProcessedNotesInfo(1, 4), // m_processedNotesInfo
        generateTestGuids(3),                 // m_expungedNoteGuids
        generateTestNotes(
            5, 7), // m_notesWhichFailedToDownloadDuringPreviousSync
        generateTestNotes(
            8, 10), // m_notesWhichFailedToProcessDuringPreviousSync
        generateTestNotes(11, 13), // m_notesCancelledDuringPreviousSync
        generateTestGuids(
            3), // m_noteGuidsWhichFailedToExpungeDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        {},                   // m_notesToProcess
        {},                   // m_processedNotesInfo
        generateTestGuids(3), // m_expungedNoteGuids
        generateTestNotes(
            5, 7), // m_notesWhichFailedToDownloadDuringPreviousSync
        generateTestNotes(
            8, 10), // m_notesWhichFailedToProcessDuringPreviousSync
        generateTestNotes(11, 13), // m_notesCancelledDuringPreviousSync
        generateTestGuids(
            3), // m_noteGuidsWhichFailedToExpungeDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        {}, // m_notesToProcess
        {}, // m_processedNotesInfo
        {}, // m_expungedNoteGuids
        generateTestNotes(
            5, 7), // m_notesWhichFailedToDownloadDuringPreviousSync
        generateTestNotes(
            8, 10), // m_notesWhichFailedToProcessDuringPreviousSync
        generateTestNotes(11, 13), // m_notesCancelledDuringPreviousSync
        generateTestGuids(
            3), // m_noteGuidsWhichFailedToExpungeDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        {}, // m_notesToProcess
        {}, // m_processedNotesInfo
        {}, // m_expungedNoteGuids
        {}, // m_notesWhichFailedToDownloadDuringPreviousSync
        generateTestNotes(
            8, 10), // m_notesWhichFailedToProcessDuringPreviousSync
        generateTestNotes(11, 13), // m_notesCancelledDuringPreviousSync
        generateTestGuids(
            3), // m_noteGuidsWhichFailedToExpungeDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        {}, // m_notesToProcess
        {}, // m_processedNotesInfo
        {}, // m_expungedNoteGuids
        {}, // m_notesWhichFailedToDownloadDuringPreviousSync
        {}, // m_notesWhichFailedToProcessDuringPreviousSync
        generateTestNotes(11, 13), // m_notesCancelledDuringPreviousSync
        generateTestGuids(
            3), // m_noteGuidsWhichFailedToExpungeDuringPreviousSync
    },
    PreviousNoteSyncTestData{
        {}, // m_notesToProcess
        {}, // m_processedNotesInfo
        {}, // m_expungedNoteGuids
        {}, // m_notesWhichFailedToDownloadDuringPreviousSync
        {}, // m_notesWhichFailedToProcessDuringPreviousSync
        {}, // m_notesCancelledDuringPreviousSync
        generateTestGuids(
            3), // m_noteGuidsWhichFailedToExpungeDuringPreviousSync
    },
};

INSTANTIATE_TEST_SUITE_P(
    DurableNotesProcessorTestWithPreviousSyncDataInstance,
    DurableNotesProcessorTestWithPreviousSyncData,
    testing::ValuesIn(gTestData));

TEST_P(
    DurableNotesProcessorTestWithPreviousSyncData,
    ProcessSyncChunksWithPreviousSyncInfo)
{
    const auto & testData = GetParam();
    const auto & notes = testData.m_notesToProcess;

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotes(notes).build();

    QDir syncPersistentStorageDir{m_temporaryDir.path()};

    QDir syncNotesDir = [&] {
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};

        return QDir{lastSyncDataDir.absoluteFilePath(QStringLiteral("notes"))};
    }();

    // Prepare test data
    if (!testData.m_processedNotesInfo.isEmpty()) {
        for (const auto it: qevercloud::toRange(testData.m_processedNotesInfo))
        {
            utils::writeProcessedNoteInfo(it.key(), it.value(), syncNotesDir);
        }
    }

    if (!testData.m_expungedNoteGuids.isEmpty()) {
        for (const auto & guid: qAsConst(testData.m_expungedNoteGuids)) {
            utils::writeExpungedNote(guid, syncNotesDir);
        }
    }

    if (!testData.m_notesWhichFailedToDownloadDuringPreviousSync.isEmpty()) {
        for (const auto & note:
             qAsConst(testData.m_notesWhichFailedToDownloadDuringPreviousSync))
        {
            utils::writeFailedToDownloadNote(note, syncNotesDir);
        }
    }

    if (!testData.m_notesWhichFailedToProcessDuringPreviousSync.isEmpty()) {
        for (const auto & note:
             qAsConst(testData.m_notesWhichFailedToProcessDuringPreviousSync))
        {
            utils::writeFailedToProcessNote(note, syncNotesDir);
        }
    }

    if (!testData.m_notesCancelledDuringPreviousSync.isEmpty()) {
        for (const auto & note:
             qAsConst(testData.m_notesCancelledDuringPreviousSync)) {
            utils::writeCancelledNote(note, syncNotesDir);
        }
    }

    if (!testData.m_noteGuidsWhichFailedToExpungeDuringPreviousSync.isEmpty()) {
        for (const auto & guid: qAsConst(
                 testData.m_noteGuidsWhichFailedToExpungeDuringPreviousSync))
        {
            utils::writeFailedToExpungeNote(guid, syncNotesDir);
        }
    }

    const QList<qevercloud::Note> notesFromPreviousSync = [&] {
        QList<qevercloud::Note> result;
        result << testData.m_notesWhichFailedToDownloadDuringPreviousSync;
        result << testData.m_notesWhichFailedToProcessDuringPreviousSync;
        result << testData.m_notesCancelledDuringPreviousSync;

        for (auto it = result.begin(); it != result.end();) {
            EXPECT_TRUE(it->guid().has_value());
            if (it->guid().has_value()) {
                const auto pit =
                    testData.m_processedNotesInfo.find(*it->guid());

                if (pit != testData.m_processedNotesInfo.end() &&
                    it->updateSequenceNum() == pit.value())
                {
                    it = result.erase(it);
                    continue;
                }
            }

            ++it;
        }

        return result;
    }();

    const QList<qevercloud::Guid> expungedNoteGuidsFromPreviousSync = [&] {
        QList<qevercloud::Guid> result;
        result << testData.m_noteGuidsWhichFailedToExpungeDuringPreviousSync;

        for (auto it = result.begin(); it != result.end();) {
            const int i = testData.m_expungedNoteGuids.indexOf(*it);
            if (i >= 0) {
                it = result.erase(it);
                continue;
            }

            ++it;
        }

        return result;
    }();

    DownloadNotesStatus currentNotesStatus;
    currentNotesStatus.m_totalNewNotes =
        static_cast<quint64>(std::max<int>(notes.size(), 0));
    for (const auto & note: qAsConst(notes)) {
        EXPECT_TRUE(note.guid());
        if (!note.guid()) {
            continue;
        }

        EXPECT_TRUE(note.updateSequenceNum());
        if (!note.updateSequenceNum()) {
            continue;
        }

        currentNotesStatus.m_processedNoteGuidsAndUsns[*note.guid()] =
            *note.updateSequenceNum();
    }

    EXPECT_CALL(*m_mockNotesProcessor, processNotes(syncChunks, _))
        .WillOnce(Return(threading::makeReadyFuture<DownloadNotesStatus>(
            DownloadNotesStatus{currentNotesStatus})));

    std::optional<DownloadNotesStatus> previousExpungedNotesStatus;
    if (!expungedNoteGuidsFromPreviousSync.isEmpty()) {
        const auto expectedSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setExpungedNotes(expungedNoteGuidsFromPreviousSync)
                   .build();

        previousExpungedNotesStatus.emplace();
        previousExpungedNotesStatus->m_totalExpungedNotes =
            static_cast<quint64>(
                std::max<int>(expungedNoteGuidsFromPreviousSync.size(), 0));

        previousExpungedNotesStatus->m_expungedNoteGuids =
            expungedNoteGuidsFromPreviousSync;

        EXPECT_CALL(
            *m_mockNotesProcessor,
            processNotes(
                testing::MatcherCast<const QList<qevercloud::SyncChunk> &>(
                    EqSyncChunksWithSortedExpungedNotes(expectedSyncChunks)),
                _))
            .WillOnce(
                [&](const QList<qevercloud::SyncChunk> & syncChunks,
                    const INotesProcessor::ICallbackWeakPtr & callbackWeak) {
                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);
                    if (callback) {
                        for (const auto & syncChunk: qAsConst(syncChunks)) {
                            if (!syncChunk.expungedNotes()) {
                                continue;
                            }

                            for (const auto & noteGuid:
                                 *syncChunk.expungedNotes()) {
                                callback->onExpungedNote(noteGuid);
                            }
                        }
                    }

                    return threading::makeReadyFuture<DownloadNotesStatus>(
                        DownloadNotesStatus{*previousExpungedNotesStatus});
                });
    }

    std::optional<DownloadNotesStatus> previousNotesStatus;
    if (!notesFromPreviousSync.isEmpty()) {
        const auto expectedSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setNotes(notesFromPreviousSync)
                   .build();

        previousNotesStatus.emplace();
        previousNotesStatus->m_totalUpdatedNotes = static_cast<quint64>(
            std::max<int>(notesFromPreviousSync.size(), 0));

        for (const auto & note: qAsConst(notesFromPreviousSync)) {
            EXPECT_TRUE(note.guid());
            if (!note.guid()) {
                continue;
            }

            EXPECT_TRUE(note.updateSequenceNum());
            if (!note.updateSequenceNum()) {
                continue;
            }

            previousNotesStatus->m_processedNoteGuidsAndUsns[*note.guid()] =
                *note.updateSequenceNum();
        }

        EXPECT_CALL(
            *m_mockNotesProcessor,
            processNotes(
                testing::MatcherCast<const QList<qevercloud::SyncChunk> &>(
                    EqSyncChunksWithSortedNotes(expectedSyncChunks)),
                _))
            .WillOnce(
                [&](const QList<qevercloud::SyncChunk> & syncChunks,
                    const INotesProcessor::ICallbackWeakPtr & callbackWeak) {
                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);
                    if (callback) {
                        for (const auto & syncChunk: qAsConst(syncChunks)) {
                            if (!syncChunk.notes()) {
                                continue;
                            }

                            for (const auto & note: *syncChunk.notes()) {
                                EXPECT_TRUE(note.guid());
                                if (!note.guid()) {
                                    continue;
                                }

                                EXPECT_TRUE(note.updateSequenceNum());
                                if (!note.updateSequenceNum()) {
                                    continue;
                                }

                                callback->onProcessedNote(
                                    *note.guid(), *note.updateSequenceNum());
                            }
                        }
                    }

                    return threading::makeReadyFuture<DownloadNotesStatus>(
                        DownloadNotesStatus{*previousNotesStatus});
                });
    }

    const auto durableNotesProcessor = std::make_shared<DurableNotesProcessor>(
        m_mockNotesProcessor, syncPersistentStorageDir);

    auto future = durableNotesProcessor->processNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    const DownloadNotesStatus expectedStatus = [&] {
        DownloadNotesStatus expectedStatus;
        if (previousExpungedNotesStatus) {
            expectedStatus = utils::mergeDownloadNotesStatuses(
                std::move(expectedStatus), *previousExpungedNotesStatus);
        }

        if (previousNotesStatus) {
            expectedStatus = utils::mergeDownloadNotesStatuses(
                std::move(expectedStatus), *previousNotesStatus);
        }

        return utils::mergeDownloadNotesStatuses(
            std::move(expectedStatus), currentNotesStatus);
    }();

    EXPECT_EQ(status, expectedStatus);

    const auto processedNotesInfo =
        utils::processedNotesInfoFromLastSync(syncNotesDir);

    const auto expectedProcessedNotesInfo = [&] {
        QHash<qevercloud::Guid, qint32> result;

        if (!testData.m_processedNotesInfo.isEmpty()) {
            for (const auto it:
                 qevercloud::toRange(testData.m_processedNotesInfo)) {
                result.insert(it.key(), it.value());
            }
        }

        const auto appendNotes =
            [&result](const QList<qevercloud::Note> & notes) {
                if (notes.isEmpty()) {
                    return;
                }

                for (const auto & note: qAsConst(notes)) {
                    EXPECT_TRUE(note.guid());
                    if (Q_UNLIKELY(!note.guid())) {
                        continue;
                    }

                    EXPECT_TRUE(note.updateSequenceNum());
                    if (Q_UNLIKELY(!note.updateSequenceNum())) {
                        continue;
                    }

                    result[*note.guid()] = *note.updateSequenceNum();
                }
            };

        appendNotes(testData.m_notesWhichFailedToDownloadDuringPreviousSync);
        appendNotes(testData.m_notesWhichFailedToProcessDuringPreviousSync);
        appendNotes(testData.m_notesCancelledDuringPreviousSync);
        return result;
    }();

    EXPECT_EQ(processedNotesInfo, expectedProcessedNotesInfo);

    const auto expungedNoteGuids =
        utils::noteGuidsExpungedDuringLastSync(syncNotesDir);

    const auto expectedExpungedNoteGuids = [&] {
        return testData.m_noteGuidsWhichFailedToExpungeDuringPreviousSync +
            testData.m_expungedNoteGuids;
    }();

    EXPECT_EQ(sorted(expungedNoteGuids), sorted(expectedExpungedNoteGuids));
}

} // namespace quentier::synchronization::tests
