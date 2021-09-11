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
#include "../ResourcesHandler.h"
#include "../TablesInitializer.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFlags>
#include <QFutureSynchronizer>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static

namespace quentier::local_storage::sql::tests {

class ResourcesHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit ResourcesHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::Resource> & putResources() const
    {
        return m_putResources;
    }

    [[nodiscard]] const QList<qevercloud::Resource> & putResourceMetadata()
        const
    {
        return m_putResourceMetadata;
    }

    [[nodiscard]] const QStringList & expungedResourceLocalIds() const
    {
        return m_expungedResourceLocalIds;
    }

public Q_SLOTS:
    void onResourcePut(qevercloud::Resource resource) // NOLINT
    {
        m_putResources << resource;
    }

    void onResourceMetadataPut(qevercloud::Resource resource) // NOLINT
    {
        m_putResourceMetadata << resource;
    }

    void onResourceExpunged(QString resourceLocalId) // NOLINT
    {
        m_expungedResourceLocalIds << resourceLocalId;
    }

private:
    QList<qevercloud::Resource> m_putResources;
    QList<qevercloud::Resource> m_putResourceMetadata;
    QStringList m_expungedResourceLocalIds;
};

namespace {

class ResourcesHandlerTest : public testing::Test
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

        m_resourceDataFilesLock = std::make_shared<QReadWriteLock>();

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
    QReadWriteLockPtr m_resourceDataFilesLock;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

} // namespace

TEST_F(ResourcesHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock));
}

TEST_F(ResourcesHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            nullptr, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, nullptr, m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            nullptr, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullResourceDataFilesLock)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), nullptr),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, ShouldHaveZeroResourceCountWhenThereAreNoResources)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto resourceCountFuture = resourcesHandler->resourceCount(
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeNonDeletedNotes);

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 0U);
}

TEST_F(ResourcesHandlerTest, ShouldNotFindNonexistentResourceByLocalId)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto resourceFuture = resourcesHandler->findResourceByLocalId(
        UidGenerator::Generate());

    resourceFuture.waitForFinished();
    EXPECT_EQ(resourceFuture.resultCount(), 0);

    resourceFuture = resourcesHandler->findResourceByLocalId(
        UidGenerator::Generate(),
        ILocalStorage::FetchResourceOption::WithBinaryData);

    resourceFuture.waitForFinished();
    EXPECT_EQ(resourceFuture.resultCount(), 0);
}

TEST_F(ResourcesHandlerTest, ShouldNotFindNonexistentResourceByGuid)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto resourceFuture = resourcesHandler->findResourceByGuid(
        UidGenerator::Generate());

    resourceFuture.waitForFinished();
    EXPECT_EQ(resourceFuture.resultCount(), 0);

    resourceFuture = resourcesHandler->findResourceByGuid(
        UidGenerator::Generate(),
        ILocalStorage::FetchResourceOption::WithBinaryData);

    resourceFuture.waitForFinished();
    EXPECT_EQ(resourceFuture.resultCount(), 0);
}

TEST_F(ResourcesHandlerTest, IgnoreAttemptToExpungeNonexistentResourceByLocalId)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeResourceFuture = resourcesHandler->expungeResourceByLocalId(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeResourceFuture.waitForFinished());
}

TEST_F(ResourcesHandlerTest, IgnoreAttemptToExpungeNonexistentResourceByGuid)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeResourceFuture = resourcesHandler->expungeResourceByGuid(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeResourceFuture.waitForFinished());
}

} // namespace quentier::local_storage::sql::tests

#include "ResourcesHandlerTest.moc"
