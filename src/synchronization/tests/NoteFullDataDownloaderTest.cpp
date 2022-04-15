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

#include <synchronization/processors/NoteFullDataDownloader.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/UidGenerator.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NoteResultSpecBuilder.h>

#include <QCoreApplication>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class NoteFullDataDownloaderTest : public testing::Test
{
protected:
    std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<mocks::qevercloud::MockINoteStore>();

    const quint32 m_maxInFlightDownloads = 100U;
};

TEST_F(NoteFullDataDownloaderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto noteFullDataDownloader =
            std::make_shared<NoteFullDataDownloader>(
                m_mockNoteStore, m_maxInFlightDownloads));
}

TEST_F(NoteFullDataDownloaderTest, CtorNullNoteStore)
{
    EXPECT_THROW(
        const auto noteFullDataDownloader =
            std::make_shared<NoteFullDataDownloader>(
                nullptr, m_maxInFlightDownloads),
        InvalidArgument);
}

TEST_F(NoteFullDataDownloaderTest, CtorZeroMaxInFlightDownloads)
{
    EXPECT_THROW(
        const auto noteFullDataDownloader =
            std::make_shared<NoteFullDataDownloader>(m_mockNoteStore, 0U),
        InvalidArgument);
}

const std::array gIncludeNoteLimits{
    INoteFullDataDownloader::IncludeNoteLimits::Yes,
    INoteFullDataDownloader::IncludeNoteLimits::No,
};

class NoteFullDataDownloaderGroupTest :
    public NoteFullDataDownloaderTest,
    public testing::WithParamInterface<
        INoteFullDataDownloader::IncludeNoteLimits>
{};

INSTANTIATE_TEST_SUITE_P(
    NoteFullDataDownloaderGroupTestInstance, NoteFullDataDownloaderGroupTest,
    testing::ValuesIn(gIncludeNoteLimits));

TEST_P(NoteFullDataDownloaderGroupTest, DownloadSingleNote)
{
    const auto noteFullDataDownloader =
        std::make_shared<NoteFullDataDownloader>(
            m_mockNoteStore, m_maxInFlightDownloads);

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const auto expectedNoteResultSpec =
        qevercloud::NoteResultSpecBuilder{}
            .setIncludeContent(true)
            .setIncludeResourcesData(true)
            .setIncludeResourcesRecognition(true)
            .setIncludeResourcesAlternateData(true)
            .setIncludeSharedNotes(true)
            .setIncludeNoteAppDataValues(true)
            .setIncludeResourceAppDataValues(true)
            .setIncludeAccountLimits(
                GetParam() == INoteFullDataDownloader::IncludeNoteLimits::Yes)
            .build();

    const auto note = qevercloud::NoteBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(1U)
                          .setNotebookGuid(UidGenerator::Generate())
                          .build();

    EXPECT_CALL(
        *m_mockNoteStore,
        getNoteWithResultSpecAsync(
            note.guid().value(), expectedNoteResultSpec, ctx))
        .WillOnce(Return(threading::makeReadyFuture(note)));

    auto future = noteFullDataDownloader->downloadFullNoteData(
        note.guid().value(),GetParam(), ctx);

    ASSERT_TRUE(future.isFinished());
    ASSERT_EQ(future.resultCount(), 1);
    EXPECT_EQ(future.result(), note);
}

TEST_P(
    NoteFullDataDownloaderGroupTest, DownloadSeveralNotesInParallelWithinLimit)
{
    const quint32 noteCount = 5;

    const auto noteFullDataDownloader =
        std::make_shared<NoteFullDataDownloader>(m_mockNoteStore, noteCount);

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const auto expectedNoteResultSpec =
        qevercloud::NoteResultSpecBuilder{}
            .setIncludeContent(true)
            .setIncludeResourcesData(true)
            .setIncludeResourcesRecognition(true)
            .setIncludeResourcesAlternateData(true)
            .setIncludeSharedNotes(true)
            .setIncludeNoteAppDataValues(true)
            .setIncludeResourceAppDataValues(true)
            .setIncludeAccountLimits(
                GetParam() == INoteFullDataDownloader::IncludeNoteLimits::Yes)
            .build();

    const QList<qevercloud::Note> notes = [&] {
        QList<qevercloud::Note> result;
        result.reserve(noteCount);
        for (quint32 i = 0; i < noteCount; ++i) {
            result << qevercloud::NoteBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(i + 1U)
                          .setNotebookGuid(UidGenerator::Generate())
                          .build();
        }
        return result;
    }();

    QList<std::shared_ptr<QPromise<qevercloud::Note>>> promises;
    promises.reserve(static_cast<int>(noteCount));

    EXPECT_CALL(*m_mockNoteStore, getNoteWithResultSpecAsync)
        .WillRepeatedly([&](const qevercloud::Guid &, // NOLINT
                            const qevercloud::NoteResultSpec & noteResultSpec,
                            const qevercloud::IRequestContextPtr &) { // NOLINT
            EXPECT_EQ(noteResultSpec, expectedNoteResultSpec);
            auto promise = std::make_shared<QPromise<qevercloud::Note>>();
            promise->start();
            auto future = promise->future();
            promises << promise;
            return future;
        });

    QList<QFuture<qevercloud::Note>> futures;
    futures.reserve(noteCount);

    for (const auto & note: qAsConst(notes)) {
        futures << noteFullDataDownloader->downloadFullNoteData(
            note.guid().value(), GetParam(), ctx);
        EXPECT_FALSE(futures.back().isFinished());
    }

    ASSERT_EQ(promises.size(), noteCount);
    for (int i = 0; i < static_cast<int>(noteCount); ++i) {
        auto & promise = promises[i];
        promise->addResult(notes[i]);
        promise->finish();
    }

    QCoreApplication::processEvents();

    for (int i = 0; i < static_cast<int>(noteCount); ++i) {
        const auto & future = futures[i];
        ASSERT_TRUE(future.isFinished()) << i;
        ASSERT_EQ(future.resultCount(), 1);
        EXPECT_EQ(future.result(), notes[i]);
    }
}

TEST_P(
    NoteFullDataDownloaderGroupTest, DownloadSeveralNotesInParallelBeyondLimit)
{
    const quint32 noteCount = 10;

    const auto noteFullDataDownloader =
        std::make_shared<NoteFullDataDownloader>(
            m_mockNoteStore, noteCount / 2);

    const QString authToken = QStringLiteral("token");
    const auto ctx = qevercloud::newRequestContext(authToken);

    const auto expectedNoteResultSpec =
        qevercloud::NoteResultSpecBuilder{}
            .setIncludeContent(true)
            .setIncludeResourcesData(true)
            .setIncludeResourcesRecognition(true)
            .setIncludeResourcesAlternateData(true)
            .setIncludeSharedNotes(true)
            .setIncludeNoteAppDataValues(true)
            .setIncludeResourceAppDataValues(true)
            .setIncludeAccountLimits(
                GetParam() == INoteFullDataDownloader::IncludeNoteLimits::Yes)
            .build();

    const QList<qevercloud::Note> notes = [&] {
        QList<qevercloud::Note> result;
        result.reserve(noteCount);
        for (quint32 i = 0; i < noteCount; ++i) {
            result << qevercloud::NoteBuilder{}
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(i + 1U)
                          .setNotebookGuid(UidGenerator::Generate())
                          .build();
        }
        return result;
    }();

    QList<std::shared_ptr<QPromise<qevercloud::Note>>> promises;
    promises.reserve(static_cast<int>(noteCount));

    EXPECT_CALL(*m_mockNoteStore, getNoteWithResultSpecAsync)
        .WillRepeatedly([&](const qevercloud::Guid &, // NOLINT
                            const qevercloud::NoteResultSpec & noteResultSpec,
                            const qevercloud::IRequestContextPtr &) { // NOLINT
            EXPECT_EQ(noteResultSpec, expectedNoteResultSpec);
            auto promise = std::make_shared<QPromise<qevercloud::Note>>();
            promise->start();
            auto future = promise->future();
            promises << promise;
            return future;
        });

    QList<QFuture<qevercloud::Note>> futures;
    futures.reserve(noteCount);

    for (const auto & note: qAsConst(notes)) {
        futures << noteFullDataDownloader->downloadFullNoteData(
            note.guid().value(), GetParam(), ctx);
        EXPECT_FALSE(futures.back().isFinished());
    }

    ASSERT_EQ(promises.size(), noteCount / 2);
    for (int i = 0; i < static_cast<int>(noteCount / 2); ++i) {
        auto & promise = promises[i];
        promise->addResult(notes[i]);
        promise->finish();
    }

    QCoreApplication::processEvents();

    for (int i = 0; i < static_cast<int>(noteCount / 2); ++i) {
        const auto & future = futures[i];
        ASSERT_TRUE(future.isFinished()) << i;
        ASSERT_EQ(future.resultCount(), 1);
        EXPECT_EQ(future.result(), notes[i]);
    }

    for (int i = static_cast<int>(noteCount / 2) + 1;
         i < static_cast<int>(noteCount); ++i)
    {
        const auto & future = futures[i];
        EXPECT_FALSE(future.isFinished());
    }

    ASSERT_EQ(promises.size(), noteCount);

    for (int i = static_cast<int>(noteCount / 2);
         i < static_cast<int>(noteCount); ++i)
    {
        auto & promise = promises[i];
        promise->addResult(notes[i]);
        promise->finish();
    }

    QCoreApplication::processEvents();

    for (int i = static_cast<int>(noteCount / 2) + 1;
         i < static_cast<int>(noteCount); ++i)
    {
        const auto & future = futures[i];
        ASSERT_TRUE(future.isFinished()) << i;
        ASSERT_EQ(future.resultCount(), 1);
        EXPECT_EQ(future.result(), notes[i]);
    }
}

} // namespace quentier::synchronization::tests
