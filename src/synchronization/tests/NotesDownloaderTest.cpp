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

#include <synchronization/NotesDownloader.h>
#include <synchronization/processors/Utils.h>
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/tests/mocks/MockINotesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
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

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::Matcher;
using testing::Return;
using testing::StrictMock;
using testing::TypedEq;

class NotesDownloaderTest : public testing::Test
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

TEST_F(NotesDownloaderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notesDownloader = std::make_shared<NotesDownloader>(
            m_mockNotesProcessor, QDir{m_temporaryDir.path()}));
}

TEST_F(NotesDownloaderTest, CtorNullNotesProcessor)
{
    EXPECT_THROW(
        const auto notesDownloader = std::make_shared<NotesDownloader>(
            nullptr, QDir{m_temporaryDir.path()}),
        InvalidArgument);
}

TEST_F(NotesDownloaderTest, ProcessSyncChunksWithoutPreviousSyncInfo)
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

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotes(notes).build();

    const auto notesDownloader = std::make_shared<NotesDownloader>(
        m_mockNotesProcessor, QDir{m_temporaryDir.path()});

    EXPECT_CALL(
        *m_mockNotesProcessor,
        processNotes(
            Matcher<const QList<qevercloud::SyncChunk> &>(_),
            Matcher<INotesProcessor::ICallbackWeakPtr>(_)))
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

            INotesDownloader::DownloadNotesStatus status;
            status.totalNewNotes = static_cast<quint64>(syncChunkNotes.size());

            for (const auto & note: qAsConst(notes)) {
                status.processedNoteGuidsAndUsns[note.guid().value()] =
                    note.updateSequenceNum().value();

                if (callback) {
                    callback->onProcessedNote(
                        note.guid().value(), note.updateSequenceNum().value());
                }
            }

            return threading::makeReadyFuture<
                INotesDownloader::DownloadNotesStatus>(std::move(status));
        });

    auto future = notesDownloader->downloadNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto status = future.result();

    EXPECT_EQ(status.totalNewNotes, notes.size());
    ASSERT_EQ(status.processedNoteGuidsAndUsns.size(), notes.size());
    for (const auto & note: qAsConst(notes)) {
        const auto it =
            status.processedNoteGuidsAndUsns.find(note.guid().value());

        EXPECT_NE(it, status.processedNoteGuidsAndUsns.end());
        if (it != status.processedNoteGuidsAndUsns.end()) {
            EXPECT_EQ(it.value(), note.updateSequenceNum().value());
        }
    }

    QDir lastSyncNotesDir = [&] {
        QDir syncPersistentStorageDir{m_temporaryDir.path()};
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};
        return QDir{lastSyncDataDir.absoluteFilePath(QStringLiteral("notes"))};
    }();

    QSettings processedNotes{
        lastSyncNotesDir.absoluteFilePath(QStringLiteral("processedNotes.ini")),
        QSettings::IniFormat};

    const auto processedNoteGuids = processedNotes.allKeys();
    ASSERT_EQ(processedNoteGuids.size(), notes.size());
    for (const auto & processedNoteGuid: qAsConst(processedNoteGuids)) {
        const auto it =
            status.processedNoteGuidsAndUsns.find(processedNoteGuid);

        EXPECT_NE(it, status.processedNoteGuidsAndUsns.end());
        if (it != status.processedNoteGuidsAndUsns.end()) {
            const auto value = processedNotes.value(processedNoteGuid);
            EXPECT_TRUE(value.isValid());

            bool conversionResult = false;
            const int usn = value.toInt(&conversionResult);
            EXPECT_TRUE(conversionResult);
            EXPECT_EQ(usn, it.value());
        }
    }
}

struct TestData
{
    QHash<qevercloud::Guid, qint32> m_processedNotesInfo;
    QList<qevercloud::Guid> m_expungedNoteGuids;

    QList<qevercloud::Note> m_notesWhichFailedToDownloadDuringPreviousSync;
    QList<qevercloud::Note> m_notesWhichFailedToProcessDuringPreviousSync;
    QList<qevercloud::Note> m_notesCancelledDuringPreviousSync;
    QList<qevercloud::Guid> m_noteGuidsWhichFailedToExpungeDuringPreviousSync;
};

class NotesDownloaderTestWithPreviousSyncData :
    public NotesDownloaderTest,
    public testing::WithParamInterface<TestData>
{};

const std::array gTestData{
    TestData{},
};

INSTANTIATE_TEST_SUITE_P(
    NotesDownloaderTestWithPreviousSyncDataInstance,
    NotesDownloaderTestWithPreviousSyncData, testing::ValuesIn(gTestData));

TEST_P(
    NotesDownloaderTestWithPreviousSyncData,
    ProcessSyncChunksWithPreviousSyncInfo)
{
    const auto notebookGuid = UidGenerator::Generate();

    const auto notes = QList<qevercloud::Note>{}
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(10)
               .setTitle(QStringLiteral("Note #10"))
               .build()
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(11)
               .setTitle(QStringLiteral("Note #11"))
               .build()
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(12)
               .setTitle(QStringLiteral("Note #12"))
               .build()
        << qevercloud::NoteBuilder{}
               .setGuid(UidGenerator::Generate())
               .setNotebookGuid(notebookGuid)
               .setUpdateSequenceNum(13)
               .setTitle(QStringLiteral("Note #13"))
               .build();

    const auto syncChunks = QList<qevercloud::SyncChunk>{}
        << qevercloud::SyncChunkBuilder{}.setNotes(notes).build();

    QDir syncPersistentStorageDir{m_temporaryDir.path()};

    QDir syncNotesDir = [&] {
        QDir lastSyncDataDir{syncPersistentStorageDir.absoluteFilePath(
            QStringLiteral("lastSyncData"))};

        return QDir{lastSyncDataDir.absoluteFilePath(QStringLiteral("notes"))};
    }();

    const auto & testData = GetParam();

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

    if (!expungedNoteGuidsFromPreviousSync.isEmpty()) {
        const auto expectedSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setExpungedNotes(expungedNoteGuidsFromPreviousSync)
                   .build();

        EXPECT_CALL(*m_mockNotesProcessor, processNotes(expectedSyncChunks, _))
            .WillOnce(Return(threading::makeReadyFuture<
                             INotesDownloader::DownloadNotesStatus>({})));
    }

    if (!notesFromPreviousSync.isEmpty()) {
        const auto expectedSyncChunks = QList<qevercloud::SyncChunk>{}
            << qevercloud::SyncChunkBuilder{}
                   .setNotes(notesFromPreviousSync)
                   .build();

        EXPECT_CALL(*m_mockNotesProcessor, processNotes(expectedSyncChunks, _))
            .WillOnce(Return(threading::makeReadyFuture<
                             INotesDownloader::DownloadNotesStatus>({})));
    }

    EXPECT_CALL(*m_mockNotesProcessor, processNotes(syncChunks, _))
        .WillOnce(Return(
            threading::makeReadyFuture<INotesDownloader::DownloadNotesStatus>(
                {})));

    const auto notesDownloader = std::make_shared<NotesDownloader>(
        m_mockNotesProcessor, syncPersistentStorageDir);

    auto future = notesDownloader->downloadNotes(syncChunks);
    ASSERT_TRUE(future.isFinished());

    // TODO: use some actual DownloadNotesStatus to verify it here
}

} // namespace quentier::synchronization::tests
