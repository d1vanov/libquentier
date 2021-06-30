/*
 * Copyright 2021 Dmitry Ivanov
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

#include "../ConnectionPool.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"
#include "../TagsHandler.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFlags>
#include <QFutureSynchronizer>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <array>
#include <iterator>

// clazy:excludeall=non-pod-global-static

namespace quentier::local_storage::sql::tests {

class TagsHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit TagsHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::Tag> & putTags() const
    {
        return m_putTags;
    }

    [[nodiscard]] const QStringList & expungedTagLocalIds() const
    {
        return m_expungedTagLocalIds;
    }

public Q_SLOTS:
    void onTagPut(qevercloud::Tag tag) // NOLINT
    {
        m_putTags << tag;
    }

    void onTagExpunged(QString tagLocalId) // NOLINT
    {
        m_expungedTagLocalIds << tagLocalId;
    }

private:
    QList<qevercloud::Tag> m_putTags;
    QStringList m_expungedTagLocalIds;
};

namespace {

enum class CreateTagOption
{
    WithLinkedNotebookGuid = 1 << 0
};

Q_DECLARE_FLAGS(CreateTagOptions, CreateTagOption);

[[nodiscard]] qevercloud::Tag createTag(
    const CreateTagOptions createOptions = {})
{
    qevercloud::Tag tag;
    tag.setLocallyModified(true);
    tag.setLocallyFavorited(true);
    tag.setLocalOnly(false);

    QHash<QString, QVariant> localData;
    localData[QStringLiteral("hey")] = QStringLiteral("hi");
    tag.setLocalData(std::move(localData));

    tag.setGuid(UidGenerator::Generate());
    tag.setName(QStringLiteral("name"));
    tag.setUpdateSequenceNum(1);

    if (createOptions & CreateTagOption::WithLinkedNotebookGuid) {
        tag.setLinkedNotebookGuid(UidGenerator::Generate());
    }

    return tag;
}

class TagsHandlerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_connectionPool = std::make_shared<ConnectionPool>(
            QStringLiteral("localhost"), QStringLiteral("user"),
            QStringLiteral("password"), QStringLiteral("file::memory:"),
            QStringLiteral("QSQLITE"),
            QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_ENABLE_SHARED_CACHE"));

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_writerThread = std::make_shared<QThread>();

        m_notifier = new Notifier;
        m_notifier->moveToThread(m_writerThread.get());

        QObject::connect(
            m_writerThread.get(),
            &QThread::finished,
            m_notifier,
            &QObject::deleteLater);

        m_writerThread->start();
    }

    void TearDown() override
    {
        m_writerThread->quit();
        m_writerThread->wait();

        // Give lambdas connected to threads finished signal a chance to fire
        QCoreApplication::processEvents();
    }

protected:
    ConnectionPoolPtr m_connectionPool;
    QThreadPtr m_writerThread;
    Notifier * m_notifier;
};

} // namespace

TEST_F(TagsHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread));
}

TEST_F(TagsHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            nullptr, QThreadPool::globalInstance(), m_notifier,
            m_writerThread),
        IQuentierException);
}

TEST_F(TagsHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, nullptr, m_notifier, m_writerThread),
        IQuentierException);
}

TEST_F(TagsHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_writerThread),
        IQuentierException);
}

TEST_F(TagsHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            nullptr),
        IQuentierException);
}

TEST_F(TagsHandlerTest, ShouldHaveZeroTagCountWhenThereAreNoTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto tagCountFuture = tagsHandler->tagCount();
    tagCountFuture.waitForFinished();
    EXPECT_EQ(tagCountFuture.result(), 0U);
}

TEST_F(TagsHandlerTest, ShouldNotFindNonexistentTagByLocalId)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto tagFuture = tagsHandler->findTagByLocalId(UidGenerator::Generate());
    tagFuture.waitForFinished();
    EXPECT_EQ(tagFuture.resultCount(), 0);
}

TEST_F(TagsHandlerTest, ShouldNotFindNonexistentTagByGuid)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto tagFuture = tagsHandler->findTagByGuid(UidGenerator::Generate());
    tagFuture.waitForFinished();
    EXPECT_EQ(tagFuture.resultCount(), 0);
}

TEST_F(TagsHandlerTest, ShouldNotFindNonexistentTagByName)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto tagFuture = tagsHandler->findTagByName(QStringLiteral("My tag"));
    tagFuture.waitForFinished();
    EXPECT_EQ(tagFuture.resultCount(), 0);
}

TEST_F(TagsHandlerTest, IgnoreAttemptToExpungeNonexistentTagByLocalId)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto expungeTagFuture = tagsHandler->expungeTagByLocalId(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeTagFuture.waitForFinished());
}

TEST_F(TagsHandlerTest, IgnoreAttemptToExpungeNonexistentTagByGuid)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto expungeTagFuture = tagsHandler->expungeTagByGuid(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeTagFuture.waitForFinished());
}

TEST_F(TagsHandlerTest, IgnoreAttemptToExpungeNonexistentTagByName)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto expungeTagFuture = tagsHandler->expungeTagByName(
        QStringLiteral("My tag"));

    EXPECT_NO_THROW(expungeTagFuture.waitForFinished());
}

TEST_F(TagsHandlerTest, ShouldListNoTagsWhenThereAreNoTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto listTagsOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListTagsOrder>{};

    listTagsOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();
    EXPECT_TRUE(listTagsFuture.result().isEmpty());
}

TEST_F(TagsHandlerTest, ShouldListNoTagsPerNoteWhenThereAreNoTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto listTagsOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListTagsOrder>{};

    listTagsOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listTagsFuture = tagsHandler->listTagsPerNoteLocalId(
        UidGenerator::Generate(), listTagsOptions);

    listTagsFuture.waitForFinished();
    EXPECT_TRUE(listTagsFuture.result().isEmpty());
}

} // namespace quentier::local_storage::sql::tests

#include "TagsHandlerTest.moc"