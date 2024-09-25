/*
 * Copyright 2021-2024 Dmitry Ivanov
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

#include "Utils.h"

#include "../ConnectionPool.h"
#include "../LinkedNotebooksHandler.h"
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

#include <gtest/gtest.h>

#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

class LinkedNotebooksHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit LinkedNotebooksHandlerTestNotifierListener(
        QObject * parent = nullptr) : QObject(parent)
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
    void onLinkedNotebookPut(
        qevercloud::LinkedNotebook linkedNotebook) // NOLINT
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
        m_connectionPool = utils::createConnectionPool();

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_thread = std::make_shared<QThread>();

        m_notifier = new Notifier;
        m_notifier->moveToThread(m_thread.get());

        QObject::connect(
            m_thread.get(), &QThread::finished, m_notifier,
            &QObject::deleteLater);

        m_thread->start();
    }

    void TearDown() override
    {
        m_thread->quit();
        m_thread->wait();

        // Give lambdas connected to threads finished signal a chance to fire
        QCoreApplication::processEvents();
    }

protected:
    ConnectionPoolPtr m_connectionPool;
    threading::QThreadPtr m_thread;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

} // namespace

TEST_F(LinkedNotebooksHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, m_notifier, m_thread, m_temporaryDir.path()));
}

TEST_F(LinkedNotebooksHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                nullptr, m_notifier, m_thread, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(LinkedNotebooksHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, nullptr, m_thread, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(LinkedNotebooksHandlerTest, CtorNullThread)
{
    EXPECT_THROW(
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, m_notifier, nullptr, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(
    LinkedNotebooksHandlerTest,
    ShouldHaveZeroLinkedNotebookCountWhenThereAreNoLinkedNotebooks)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    auto linkedNotebookCountFuture =
        linkedNotebooksHandler->linkedNotebookCount();
    linkedNotebookCountFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookCountFuture.result(), 0U);
}

TEST_F(LinkedNotebooksHandlerTest, ShouldNotFindNonexistentLinkedNotebookByGuid)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    auto linkedNotebookFuture =
        linkedNotebooksHandler->findLinkedNotebookByGuid(
            UidGenerator::Generate());

    linkedNotebookFuture.waitForFinished();
    ASSERT_EQ(linkedNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(linkedNotebookFuture.result());
}

TEST_F(
    LinkedNotebooksHandlerTest,
    IgnoreAttemptToExpungeNonexistentLinkedNotebookByGuid)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    auto expungeLinkedNotebookFuture =
        linkedNotebooksHandler->expungeLinkedNotebookByGuid(
            UidGenerator::Generate());

    EXPECT_NO_THROW(expungeLinkedNotebookFuture.waitForFinished());
}

TEST_F(
    LinkedNotebooksHandlerTest,
    ShouldListNoLinkedNotebooksWhenThereAreNoLinkedNotebooks)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    const auto listLinkedNotebooksOptions =
        ILocalStorage::ListLinkedNotebooksOptions{};

    auto listLinkedNotebooksFuture =
        linkedNotebooksHandler->listLinkedNotebooks(listLinkedNotebooksOptions);

    listLinkedNotebooksFuture.waitForFinished();
    EXPECT_TRUE(listLinkedNotebooksFuture.result().isEmpty());
}

TEST_F(LinkedNotebooksHandlerTest, HandleSingleLinkedNotebook)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    LinkedNotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::linkedNotebookPut, &notifierListener,
        &LinkedNotebooksHandlerTestNotifierListener::onLinkedNotebookPut);

    QObject::connect(
        m_notifier, &Notifier::linkedNotebookExpunged, &notifierListener,
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
    ASSERT_EQ(foundByGuidLinkedNotebookFuture.resultCount(), 1);
    EXPECT_EQ(foundByGuidLinkedNotebookFuture.result(), linkedNotebook);

    const auto listLinkedNotebooksOptions =
        ILocalStorage::ListLinkedNotebooksOptions{};

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

    linkedNotebookCountFuture = linkedNotebooksHandler->linkedNotebookCount();

    linkedNotebookCountFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookCountFuture.result(), 0U);

    foundByGuidLinkedNotebookFuture =
        linkedNotebooksHandler->findLinkedNotebookByGuid(
            linkedNotebook.guid().value());

    foundByGuidLinkedNotebookFuture.waitForFinished();
    ASSERT_EQ(foundByGuidLinkedNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(foundByGuidLinkedNotebookFuture.result());
}

TEST_F(LinkedNotebooksHandlerTest, HandleMultipleLinkedNotebooks)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_notifier, m_thread, m_temporaryDir.path());

    LinkedNotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::linkedNotebookPut, &notifierListener,
        &LinkedNotebooksHandlerTestNotifierListener::onLinkedNotebookPut);

    QObject::connect(
        m_notifier, &Notifier::linkedNotebookExpunged, &notifierListener,
        &LinkedNotebooksHandlerTestNotifierListener::onLinkedNotebookExpunged);

    const int linkedNotebookCount = 5;
    QList<qevercloud::LinkedNotebook> linkedNotebooks;
    linkedNotebooks.reserve(linkedNotebookCount);
    for (int i = 0; i < linkedNotebookCount; ++i) {
        linkedNotebooks << createLinkedNotebook();
    }

    QFutureSynchronizer<void> putLinkedNotebooksSynchronizer;
    for (auto linkedNotebook: std::as_const(linkedNotebooks)) {
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

    for (const auto & linkedNotebook: std::as_const(linkedNotebooks)) {
        auto foundByGuidLinkedNotebookFuture =
            linkedNotebooksHandler->findLinkedNotebookByGuid(
                linkedNotebook.guid().value());

        foundByGuidLinkedNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByGuidLinkedNotebookFuture.resultCount(), 1);
        EXPECT_EQ(foundByGuidLinkedNotebookFuture.result(), linkedNotebook);
    }

    for (const auto & linkedNotebook: std::as_const(linkedNotebooks)) {
        auto expungeLinkedNotebookByGuidFuture =
            linkedNotebooksHandler->expungeLinkedNotebookByGuid(
                linkedNotebook.guid().value());

        expungeLinkedNotebookByGuidFuture.waitForFinished();
    }

    QCoreApplication::processEvents();

    EXPECT_EQ(
        notifierListener.expungedLinkedNotebookGuids().size(),
        linkedNotebookCount);

    linkedNotebookCountFuture = linkedNotebooksHandler->linkedNotebookCount();

    linkedNotebookCountFuture.waitForFinished();
    EXPECT_EQ(linkedNotebookCountFuture.result(), 0U);

    for (const auto & linkedNotebook: std::as_const(linkedNotebooks)) {
        auto foundByGuidLinkedNotebookFuture =
            linkedNotebooksHandler->findLinkedNotebookByGuid(
                linkedNotebook.guid().value());

        foundByGuidLinkedNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByGuidLinkedNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidLinkedNotebookFuture.result());
    }
}

} // namespace quentier::local_storage::sql::tests

#include "LinkedNotebooksHandlerTest.moc"
