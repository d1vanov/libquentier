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
#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/tests/mocks/MockINotesProcessor.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/SyncChunk.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>

#include <QSettings>
#include <QTemporaryDir>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::Matcher;
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

            const QList<qevercloud::Note> syncChunkNotes = [&]
            {
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

    QDir lastSyncNotesDir = [&]
    {
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
        if (it != status.processedNoteGuidsAndUsns.end())
        {
            const auto value = processedNotes.value(processedNoteGuid);
            EXPECT_TRUE(value.isValid());

            bool conversionResult = false;
            const int usn = value.toInt(&conversionResult);
            EXPECT_TRUE(conversionResult);
            EXPECT_EQ(usn, it.value());
        }
    }
}

} // namespace quentier::synchronization::tests
