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
#include "../TablesInitializer.h"
#include "../NotebooksHandler.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

namespace quentier::local_storage::sql::tests {

namespace {

[[nodiscard]] QList<qevercloud::SharedNotebook> createSharedNotebooks(
    const qevercloud::Guid & notebookGuid)
{
    qevercloud::SharedNotebook sharedNotebook1;
    sharedNotebook1.setLocallyModified(false);
    sharedNotebook1.setLocalOnly(false);
    sharedNotebook1.setLocallyFavorited(false);
    sharedNotebook1.setId(2L);
    sharedNotebook1.setUserId(qevercloud::UserID{1});
    sharedNotebook1.setSharerUserId(qevercloud::UserID{3});
    sharedNotebook1.setNotebookGuid(notebookGuid);
    sharedNotebook1.setEmail(QStringLiteral("example1@example.com"));
    sharedNotebook1.setRecipientIdentityId(qevercloud::IdentityID{3});
    sharedNotebook1.setGlobalId(QStringLiteral("globalId1"));
    sharedNotebook1.setRecipientUsername(QStringLiteral("recipientUsername1"));
    sharedNotebook1.setRecipientUserId(qevercloud::UserID{4});
    sharedNotebook1.setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::READ_NOTEBOOK);

    qevercloud::SharedNotebookRecipientSettings recipientSettings;
    recipientSettings.setReminderNotifyEmail(true);
    recipientSettings.setReminderNotifyInApp(false);
    sharedNotebook1.setRecipientSettings(recipientSettings);

    const auto now = QDateTime::currentMSecsSinceEpoch();
    sharedNotebook1.setServiceCreated(now);
    sharedNotebook1.setServiceUpdated(now + 1);
    sharedNotebook1.setServiceAssigned(now + 2);

    qevercloud::SharedNotebook sharedNotebook2;
    sharedNotebook2.setLocallyModified(true);
    sharedNotebook2.setLocalOnly(false);
    sharedNotebook2.setLocallyFavorited(true);
    sharedNotebook2.setId(5L);
    sharedNotebook2.setUserId(qevercloud::UserID{6});
    sharedNotebook2.setSharerUserId(qevercloud::UserID{7});
    sharedNotebook2.setNotebookGuid(notebookGuid);
    sharedNotebook2.setEmail(QStringLiteral("example2@example.com"));
    sharedNotebook2.setRecipientIdentityId(qevercloud::IdentityID{8});
    sharedNotebook2.setGlobalId(QStringLiteral("globalId2"));
    sharedNotebook2.setRecipientUsername(QStringLiteral("recipientUsername2"));
    sharedNotebook2.setRecipientUserId(qevercloud::UserID{9});
    sharedNotebook2.setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    recipientSettings.setReminderNotifyEmail(false);
    recipientSettings.setReminderNotifyInApp(true);
    sharedNotebook2.setRecipientSettings(recipientSettings);

    sharedNotebook2.setServiceCreated(now + 3);
    sharedNotebook2.setServiceUpdated(now + 4);
    sharedNotebook2.setServiceAssigned(now + 5);

    return QList<qevercloud::SharedNotebook>{} << sharedNotebook1
                                               << sharedNotebook2;
}

[[nodiscard]] qevercloud::BusinessNotebook createBusinessNotebook()
{
    qevercloud::BusinessNotebook businessNotebook;
    businessNotebook.setRecommended(true);
    businessNotebook.setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::BUSINESS_FULL_ACCESS);

    businessNotebook.setNotebookDescription(
        QStringLiteral("notebookDescription"));

    return businessNotebook;
}

[[nodiscard]] qevercloud::User createContact()
{
    qevercloud::User user;
    user.setId(1);
    user.setUsername(QStringLiteral("fake_user_username"));
    user.setEmail(QStringLiteral("fake_user _mail"));
    user.setName(QStringLiteral("fake_user_name"));
    user.setTimezone(QStringLiteral("fake_user_timezone"));
    user.setPrivilege(qevercloud::PrivilegeLevel::NORMAL);
    user.setCreated(2);
    user.setUpdated(3);
    user.setActive(true);
    return user;
}

class NotebooksHandlerTest : public testing::Test
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
};

} // namespace

TEST_F(NotebooksHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
            m_temporaryDir.path()));
}

TEST_F(NotebooksHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            nullptr, QThreadPool::globalInstance(), m_writerThread,
            m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, nullptr, m_writerThread,
            m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, ShouldHaveZeroNotebookCountWhenThereAreNoNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 0U);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByLocalId)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findNotebookByLocalId(
        UidGenerator::Generate());

    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByGuid)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findNotebookByGuid(
        UidGenerator::Generate());

    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByName)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findNotebookByName(
        QStringLiteral("My notebook"));

    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentDefaultNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findDefaultNotebook();
    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByLocalId)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto expungeNotebookFuture = notebooksHandler->expungeNotebookByLocalId(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByGuid)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto expungeNotebookFuture = notebooksHandler->expungeNotebookByGuid(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByName)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto expungeNotebookFuture = notebooksHandler->expungeNotebookByName(
        QStringLiteral("My notebook"));

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, ShouldListNoNotebooksWhenThereAreNoNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto listNotebooksFuture = notebooksHandler->listNotebooks(
        NotebooksHandler::ListOptions<NotebooksHandler::ListNotebooksOrder>{});

    listNotebooksFuture.waitForFinished();
    EXPECT_TRUE(listNotebooksFuture.result().isEmpty());
}

TEST_F(NotebooksHandlerTest, ShouldListNoSharedNotebooksForNonexistentNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread,
        m_temporaryDir.path());

    auto sharedNotebooksFuture = notebooksHandler->listSharedNotebooks(
        UidGenerator::Generate());

    sharedNotebooksFuture.waitForFinished();
    EXPECT_TRUE(sharedNotebooksFuture.result().isEmpty());
}

} // namespace quentier::local_storage::sql::tests
