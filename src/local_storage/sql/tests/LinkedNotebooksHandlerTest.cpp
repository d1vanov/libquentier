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

#include "../LinkedNotebooksHandler.h"
#include "../ConnectionPool.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
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

class LinkedNotebooksHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit LinkedNotebooksHandlerTestNotifierListener(
        QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::LinkedNotebook> & putLinkedNotebooks()
        const noexcept
    {
        return m_putLinkedNotebooks;
    }

    [[nodiscard]] const QStringList & expungedLinkedNotebookGuids()
        const noexcept
    {
        return m_expungedLinkedNotebookGuids;
    }

public Q_SLOTS:
    void onLinkedNotebookPut(qevercloud::LinkedNotebook linkedNotebook) // NOLINT
    {
        m_putLinkedNotebooks << linkedNotebook;
    }

    void onLinkedNotebookExpunged(QString linkedNotebookGuid) // NOLINT
    {
        m_expungedLinkedNotebookGuids << linkedNotebookGuid;
    }

private:
    QList<qevercloud::LinkedNotebook> m_putLinkedNotebooks;
    QStringList m_expungedLinkedNotebookGuids;
};

namespace {

[[nodiscard]] qevercloud::LinkedNotebook createLinkedNotebook()
{
    qevercloud::LinkedNotebook linkedNotebook;
    linkedNotebook.setLocallyModified(true);

    QHash<QString, QVariant> localData;
    localData[QStringLiteral("hey")] = QStringLiteral("hi");
    linkedNotebook.setLocalData(std::move(localData));

    linkedNotebook.setShareName(QStringLiteral("shareName"));
    linkedNotebook.setUsername(QStringLiteral("username"));
    linkedNotebook.setShardId(QStringLiteral("shardId"));

    linkedNotebook.setSharedNotebookGlobalId(
        QStringLiteral("sharedNotebookGlobalId"));

    linkedNotebook.setUri(QStringLiteral("uri"));
    linkedNotebook.setGuid(UidGenerator::Generate());
    linkedNotebook.setUpdateSequenceNum(1);
    linkedNotebook.setNoteStoreUrl(QStringLiteral("noteStoreUrl"));
    linkedNotebook.setWebApiUrlPrefix(QStringLiteral("webApiUrlPrefix"));
    linkedNotebook.setStack(QStringLiteral("stack"));
    linkedNotebook.setBusinessId(2);

    return linkedNotebook;
}

class LinkedNotebooksHandlerTest : public testing::Test
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
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

} // namespace

TEST_F(LinkedNotebooksHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path()));
}

TEST_F(LinkedNotebooksHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                nullptr, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(LinkedNotebooksHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, nullptr, m_notifier, m_writerThread,
                m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(LinkedNotebooksHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), nullptr,
                m_writerThread, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(LinkedNotebooksHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                nullptr, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(LinkedNotebooksHandlerTest, ShouldHaveZeroLinkedNotebookCountWhenThereAreNoLinkedNotebooks)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    auto linkedNotebookCountFuture = linkedNotebooksHandler->linkedNotebookCount();
    linkedNotebookCountFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookCountFuture.result(), 0U);
}

TEST_F(LinkedNotebooksHandlerTest, ShouldNotFindNonexistentLinkedNotebookByGuid)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    auto linkedNotebookFuture = linkedNotebooksHandler->findLinkedNotebookByGuid(
        UidGenerator::Generate());

    linkedNotebookFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookFuture.resultCount(), 0);
}

TEST_F(LinkedNotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentLinkedNotebookByGuid)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    auto expungeLinkedNotebookFuture = linkedNotebooksHandler->expungeLinkedNotebookByGuid(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeLinkedNotebookFuture.waitForFinished());
}

TEST_F(LinkedNotebooksHandlerTest, ShouldListNoLinkedNotebooksWhenThereAreNoLinkedNotebooks)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    auto listLinkedNotebooksOptions =
        ILocalStorage::ListLinkedNotebooksOptions{};

    listLinkedNotebooksOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listLinkedNotebooksFuture =
        linkedNotebooksHandler->listLinkedNotebooks(listLinkedNotebooksOptions);

    listLinkedNotebooksFuture.waitForFinished();
    EXPECT_TRUE(listLinkedNotebooksFuture.result().isEmpty());
}

TEST_F(LinkedNotebooksHandlerTest, HandleSingleLinkedNotebook)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    LinkedNotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::linkedNotebookPut,
        &notifierListener,
        &LinkedNotebooksHandlerTestNotifierListener::onLinkedNotebookPut);

    QObject::connect(
        m_notifier,
        &Notifier::linkedNotebookExpunged,
        &notifierListener,
        &LinkedNotebooksHandlerTestNotifierListener::onLinkedNotebookExpunged);

    const auto linkedNotebook = createLinkedNotebook();

    auto putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

    putLinkedNotebookFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putLinkedNotebooks().size(), 1);
    EXPECT_EQ(notifierListener.putLinkedNotebooks()[0], linkedNotebook);

    auto linkedNotebookCountFuture =
        linkedNotebooksHandler->linkedNotebookCount();

    linkedNotebookCountFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookCountFuture.result(), 1U);

    auto foundByGuidLinkedNotebookFuture =
        linkedNotebooksHandler->findLinkedNotebookByGuid(
            linkedNotebook.guid().value());

    foundByGuidLinkedNotebookFuture.waitForFinished();
    EXPECT_EQ(foundByGuidLinkedNotebookFuture.result(), linkedNotebook);

    auto listLinkedNotebooksOptions =
        ILocalStorage::ListLinkedNotebooksOptions{};

    listLinkedNotebooksOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listLinkedNotebooksFuture =
        linkedNotebooksHandler->listLinkedNotebooks(listLinkedNotebooksOptions);

    listLinkedNotebooksFuture.waitForFinished();
    auto linkedNotebooks = listLinkedNotebooksFuture.result();
    EXPECT_EQ(linkedNotebooks.size(), 1);
    EXPECT_EQ(linkedNotebooks[0], linkedNotebook);

    auto expungeLinkedNotebookByGuidFuture =
        linkedNotebooksHandler->expungeLinkedNotebookByGuid(
            linkedNotebook.guid().value());

    expungeLinkedNotebookByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedLinkedNotebookGuids().size(), 1);

    EXPECT_EQ(
        notifierListener.expungedLinkedNotebookGuids()[0],
        linkedNotebook.guid().value());

    linkedNotebookCountFuture =
        linkedNotebooksHandler->linkedNotebookCount();

    linkedNotebookCountFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookCountFuture.result(), 0U);

    foundByGuidLinkedNotebookFuture =
        linkedNotebooksHandler->findLinkedNotebookByGuid(
            linkedNotebook.guid().value());

    foundByGuidLinkedNotebookFuture.waitForFinished();
    EXPECT_EQ(foundByGuidLinkedNotebookFuture.resultCount(), 0);
}

TEST_F(LinkedNotebooksHandlerTest, HandleMultipleLinkedNotebooks)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    LinkedNotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::linkedNotebookPut,
        &notifierListener,
        &LinkedNotebooksHandlerTestNotifierListener::onLinkedNotebookPut);

    QObject::connect(
        m_notifier,
        &Notifier::linkedNotebookExpunged,
        &notifierListener,
        &LinkedNotebooksHandlerTestNotifierListener::onLinkedNotebookExpunged);

    const int linkedNotebookCount = 5;
    QList<qevercloud::LinkedNotebook> linkedNotebooks;
    linkedNotebooks.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebooks << createLinkedNotebook();
    }

    QFutureSynchronizer<void> putLinkedNotebooksSynchronizer;
    for (auto linkedNotebook: qAsConst(linkedNotebooks)) {
        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(
                std::move(linkedNotebook));

        putLinkedNotebooksSynchronizer.addFuture(putLinkedNotebookFuture);
    }

    EXPECT_NO_THROW(putLinkedNotebooksSynchronizer.waitForFinished());

    QCoreApplication::processEvents();

    EXPECT_EQ(
        notifierListener.putLinkedNotebooks().size(), linkedNotebookCount);

    auto linkedNotebookCountFuture =
        linkedNotebooksHandler->linkedNotebookCount();

    linkedNotebookCountFuture.waitForFinished();

    EXPECT_EQ(
        static_cast<int>(linkedNotebookCountFuture.result()),
        linkedNotebookCount);

    for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
        auto foundByGuidLinkedNotebookFuture =
            linkedNotebooksHandler->findLinkedNotebookByGuid(
                linkedNotebook.guid().value());

        foundByGuidLinkedNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByGuidLinkedNotebookFuture.result(), linkedNotebook);
    }

    for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
        auto expungeLinkedNotebookByGuidFuture =
            linkedNotebooksHandler->expungeLinkedNotebookByGuid(
                linkedNotebook.guid().value());

        expungeLinkedNotebookByGuidFuture.waitForFinished();
    }

    QCoreApplication::processEvents();

    EXPECT_EQ(
        notifierListener.expungedLinkedNotebookGuids().size(),
        linkedNotebookCount);

    linkedNotebookCountFuture =
        linkedNotebooksHandler->linkedNotebookCount();

    linkedNotebookCountFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookCountFuture.result(), 0U);

    for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
        auto foundByGuidLinkedNotebookFuture =
            linkedNotebooksHandler->findLinkedNotebookByGuid(
                linkedNotebook.guid().value());

        foundByGuidLinkedNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByGuidLinkedNotebookFuture.resultCount(), 0);
    }
}

} // namespace quentier::local_storage::sql::tests

#include "LinkedNotebooksHandlerTest.moc"
