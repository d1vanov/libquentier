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
#include "../NotebooksHandler.h"
#include "../Notifier.h"
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

#include <array>
#include <iterator>

// clazy:excludeall=non-pod-global-static

namespace quentier::local_storage::sql::tests {

class NotebooksHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit NotebooksHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::Notebook> & putNotebooks() const
    {
        return m_putNotebooks;
    }

    [[nodiscard]] const QStringList & expungeNotebookLocalIds() const
    {
        return m_expungedNotebookLocalIds;
    }

public Q_SLOTS:
    void onNotebookPut(qevercloud::Notebook notebook) // NOLINT
    {
        m_putNotebooks << notebook;
    }

    void onNotebookExpunged(QString notebookLocalId) // NOLINT
    {
        m_expungedNotebookLocalIds << notebookLocalId;
    }

private:
    QList<qevercloud::Notebook> m_putNotebooks;
    QStringList m_expungedNotebookLocalIds;
};

namespace {

[[nodiscard]] QList<qevercloud::SharedNotebook> createSharedNotebooks(
    const qevercloud::Guid & notebookGuid)
{
    qevercloud::SharedNotebook sharedNotebook1;
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

[[nodiscard]] qevercloud::NotebookRestrictions createNotebookRestrictions()
{
    qevercloud::NotebookRestrictions restrictions;
    restrictions.setNoReadNotes(false);
    restrictions.setNoCreateNotes(true);
    restrictions.setNoUpdateNotes(true);
    restrictions.setNoExpungeNotes(true);
    restrictions.setNoShareNotes(false);
    restrictions.setNoEmailNotes(false);
    restrictions.setNoSendMessageToRecipients(false);
    restrictions.setNoUpdateNotebook(false);
    restrictions.setNoExpungeNotebook(true);
    restrictions.setNoSetDefaultNotebook(true);
    restrictions.setNoSetNotebookStack(false);
    restrictions.setNoPublishToPublic(true);
    restrictions.setNoPublishToBusinessLibrary(false);
    restrictions.setNoCreateTags(false);
    restrictions.setNoUpdateTags(true);
    restrictions.setNoExpungeTags(false);
    restrictions.setNoSetParentTag(true);
    restrictions.setNoCreateSharedNotebooks(false);
    restrictions.setNoShareNotesWithBusiness(false);
    restrictions.setNoRenameNotebook(false);

    restrictions.setUpdateWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::ASSIGNED);

    restrictions.setExpungeWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS);

    return restrictions;
}

[[nodiscard]] qevercloud::NotebookRecipientSettings
    createNotebookRecipientSettings()
{
    qevercloud::NotebookRecipientSettings settings;
    settings.setReminderNotifyEmail(true);
    settings.setReminderNotifyInApp(true);
    settings.setInMyList(false);
    settings.setStack(QStringLiteral("stack1"));
    return settings;
}

[[nodiscard]] qevercloud::Publishing createPublishing()
{
    qevercloud::Publishing publishing;
    publishing.setUri(QStringLiteral("uri"));
    publishing.setOrder(qevercloud::NoteSortOrder::CREATED);
    publishing.setAscending(true);
    publishing.setPublicDescription(QStringLiteral("public description"));
    return publishing;
}

enum class CreateNotebookOption
{
    WithSharedNotebooks = 1 << 0,
    WithBusinessNotebook = 1 << 1,
    WithContact = 1 << 2,
    WithRestrictions = 1 << 3,
    WithRecipientSettings = 1 << 4,
    WithPublishing = 1 << 5,
    WithLinkedNotebookGuid = 1 << 6
};

Q_DECLARE_FLAGS(CreateNotebookOptions, CreateNotebookOption);

[[nodiscard]] qevercloud::Notebook createNotebook(
    const CreateNotebookOptions & createOptions = {})
{
    qevercloud::Notebook notebook;
    notebook.setLocallyModified(true);
    notebook.setLocalOnly(false);
    notebook.setLocallyFavorited(true);

    QHash<QString, QVariant> localData;
    localData[QStringLiteral("hey")] = QStringLiteral("hi");
    notebook.setLocalData(std::move(localData));

    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("name"));
    notebook.setUpdateSequenceNum(1);
    notebook.setDefaultNotebook(true);
    notebook.setStack(QStringLiteral("stack1"));

    const auto now = QDateTime::currentMSecsSinceEpoch();
    notebook.setServiceCreated(now);
    notebook.setServiceUpdated(now);

    if (createOptions & CreateNotebookOption::WithPublishing) {
        notebook.setPublished(true);
        notebook.setPublishing(createPublishing());
    }
    else {
        notebook.setPublished(false);
    }

    if (createOptions & CreateNotebookOption::WithSharedNotebooks) {
        notebook.setSharedNotebooks(
            createSharedNotebooks(notebook.guid().value()));
    }

    if (createOptions & CreateNotebookOption::WithBusinessNotebook) {
        notebook.setBusinessNotebook(createBusinessNotebook());
    }

    if (createOptions & CreateNotebookOption::WithContact) {
        notebook.setContact(createContact());
    }

    if (createOptions & CreateNotebookOption::WithRestrictions) {
        notebook.setRestrictions(createNotebookRestrictions());
    }

    if (createOptions & CreateNotebookOption::WithRecipientSettings) {
        notebook.setRecipientSettings(createNotebookRecipientSettings());
    }

    if (createOptions & CreateNotebookOption::WithLinkedNotebookGuid) {
        notebook.setLinkedNotebookGuid(UidGenerator::Generate());
    }

    return notebook;
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

TEST_F(NotebooksHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path()));
}

TEST_F(NotebooksHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            nullptr, QThreadPool::globalInstance(), m_notifier, m_writerThread,
            m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, nullptr, m_notifier, m_writerThread,
            m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_writerThread, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            nullptr, m_temporaryDir.path()),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, ShouldHaveZeroNotebookCountWhenThereAreNoNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 0U);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByLocalId)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findNotebookByLocalId(
        UidGenerator::Generate());

    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByGuid)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findNotebookByGuid(
        UidGenerator::Generate());

    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByName)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findNotebookByName(
        QStringLiteral("My notebook"));

    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentDefaultNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto notebookFuture = notebooksHandler->findDefaultNotebook();
    notebookFuture.waitForFinished();
    EXPECT_EQ(notebookFuture.resultCount(), 0);
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByLocalId)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto expungeNotebookFuture = notebooksHandler->expungeNotebookByLocalId(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByGuid)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto expungeNotebookFuture = notebooksHandler->expungeNotebookByGuid(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByName)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto expungeNotebookFuture = notebooksHandler->expungeNotebookByName(
        QStringLiteral("My notebook"));

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, ShouldListNoNotebooksWhenThereAreNoNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto listNotebooksOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotebooksOrder>{};

    listNotebooksOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listNotebooksFuture =
        notebooksHandler->listNotebooks(listNotebooksOptions);

    listNotebooksFuture.waitForFinished();
    EXPECT_TRUE(listNotebooksFuture.result().isEmpty());
}

TEST_F(NotebooksHandlerTest, ShouldListNoSharedNotebooksForNonexistentNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    auto sharedNotebooksFuture = notebooksHandler->listSharedNotebooks(
        UidGenerator::Generate());

    sharedNotebooksFuture.waitForFinished();
    EXPECT_TRUE(sharedNotebooksFuture.result().isEmpty());
}

class NotebooksHandlerSingleNotebookTest :
    public NotebooksHandlerTest,
    public testing::WithParamInterface<qevercloud::Notebook>
{};

const std::array notebook_test_values{
    createNotebook(),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithBusinessNotebook}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithContact}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithRestrictions}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithRecipientSettings}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithPublishing}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithLinkedNotebookGuid}),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithBusinessNotebook),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithContact),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithRestrictions),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithRecipientSettings),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithPublishing),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithLinkedNotebookGuid),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithBusinessNotebook} |
        CreateNotebookOption::WithContact |
        CreateNotebookOption::WithRestrictions),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithBusinessNotebook} |
        CreateNotebookOption::WithRestrictions |
        CreateNotebookOption::WithPublishing),
    createNotebook(CreateNotebookOptions{
        CreateNotebookOption::WithContact} |
        CreateNotebookOption::WithRestrictions |
        CreateNotebookOption::WithPublishing |
        CreateNotebookOption::WithLinkedNotebookGuid)
};

INSTANTIATE_TEST_SUITE_P(
    NotebooksHandlerSingleNotebookTestInstance,
    NotebooksHandlerSingleNotebookTest,
    testing::ValuesIn(notebook_test_values));

TEST_P(NotebooksHandlerSingleNotebookTest, HandleSingleNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    NotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::notebookPut,
        &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookPut);

    QObject::connect(
        m_notifier,
        &Notifier::notebookExpunged,
        &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookExpunged);

    const auto notebook = GetParam();
    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), 1);
    EXPECT_EQ(notifierListener.putNotebooks()[0], notebook);

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 1U);

    auto foundByLocalIdNotebookFuture = notebooksHandler->findNotebookByLocalId(
        notebook.localId());

    foundByLocalIdNotebookFuture.waitForFinished();
    EXPECT_EQ(foundByLocalIdNotebookFuture.result(), notebook);

    auto foundByGuidNotebookFuture = notebooksHandler->findNotebookByGuid(
        notebook.guid().value());

    foundByGuidNotebookFuture.waitForFinished();
    EXPECT_EQ(foundByGuidNotebookFuture.result(), notebook);

    auto foundByNameNotebookFuture = notebooksHandler->findNotebookByName(
        notebook.name().value());

    foundByNameNotebookFuture.waitForFinished();
    EXPECT_EQ(foundByNameNotebookFuture.result(), notebook);

    auto foundDefaultNotebookFuture = notebooksHandler->findDefaultNotebook();
    foundDefaultNotebookFuture.waitForFinished();
    EXPECT_EQ(foundDefaultNotebookFuture.result(), notebook);

    auto listNotebooksOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotebooksOrder>{};

    listNotebooksOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listNotebooksFuture =
        notebooksHandler->listNotebooks(listNotebooksOptions);

    listNotebooksFuture.waitForFinished();
    auto notebooks = listNotebooksFuture.result();
    EXPECT_EQ(notebooks.size(), 1);
    EXPECT_EQ(notebooks[0], notebook);

    auto expungeNotebookByLocalIdFuture =
        notebooksHandler->expungeNotebookByLocalId(notebook.localId());

    expungeNotebookByLocalIdFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungeNotebookLocalIds().size(), 1);

    EXPECT_EQ(
        notifierListener.expungeNotebookLocalIds()[0], notebook.localId());

    auto checkNotebookDeleted = [&]
    {
        notebookCountFuture = notebooksHandler->notebookCount();
        notebookCountFuture.waitForFinished();
        EXPECT_EQ(notebookCountFuture.result(), 0U);

        foundByLocalIdNotebookFuture = notebooksHandler->findNotebookByLocalId(
            notebook.localId());

        foundByLocalIdNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByLocalIdNotebookFuture.resultCount(), 0);

        foundByGuidNotebookFuture = notebooksHandler->findNotebookByGuid(
            notebook.guid().value());

        foundByGuidNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByGuidNotebookFuture.resultCount(), 0);

        foundByNameNotebookFuture = notebooksHandler->findNotebookByName(
            notebook.name().value());

        foundByNameNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByNameNotebookFuture.resultCount(), 0);

        foundDefaultNotebookFuture = notebooksHandler->findDefaultNotebook();
        foundDefaultNotebookFuture.waitForFinished();
        EXPECT_EQ(foundDefaultNotebookFuture.resultCount(), 0);

        listNotebooksFuture =
            notebooksHandler->listNotebooks(listNotebooksOptions);

        listNotebooksFuture.waitForFinished();
        EXPECT_TRUE(listNotebooksFuture.result().isEmpty());
    };

    checkNotebookDeleted();

    putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), 2);
    EXPECT_EQ(notifierListener.putNotebooks()[1], notebook);

    auto expungeNotebookByGuidFuture =
        notebooksHandler->expungeNotebookByGuid(notebook.guid().value());

    expungeNotebookByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungeNotebookLocalIds().size(), 2);

    EXPECT_EQ(
        notifierListener.expungeNotebookLocalIds()[1], notebook.localId());

    checkNotebookDeleted();

    putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), 3);
    EXPECT_EQ(notifierListener.putNotebooks()[2], notebook);

    auto expungeNotebookByNameFuture = notebooksHandler->expungeNotebookByName(
        notebook.name().value(),
        notebook.linkedNotebookGuid());

    expungeNotebookByNameFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungeNotebookLocalIds().size(), 3);

    EXPECT_EQ(
        notifierListener.expungeNotebookLocalIds()[2], notebook.localId());

    checkNotebookDeleted();
}

TEST_F(NotebooksHandlerTest, HandleMultipleNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path());

    NotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::notebookPut,
        &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookPut);

    QObject::connect(
        m_notifier,
        &Notifier::notebookExpunged,
        &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookExpunged);

    auto notebooks = notebook_test_values;
    int notebookCounter = 2;
    qint64 sharedNotebookIdCounter = 6U;
    for (auto it = std::next(notebooks.begin()); it != notebooks.end(); ++it) { // NOLINT
        auto & notebook = *it;
        notebook.setLocalId(UidGenerator::Generate());
        notebook.setGuid(UidGenerator::Generate());

        notebook.setName(
            notebooks.begin()->name().value() + QStringLiteral(" #") +
            QString::number(notebookCounter));

        if (notebook.sharedNotebooks() && !notebook.sharedNotebooks()->isEmpty()) {
            for (auto & sharedNotebook: *notebook.mutableSharedNotebooks()) {
                sharedNotebook.setNotebookGuid(notebook.guid());
                sharedNotebook.setId(sharedNotebookIdCounter);
                ++sharedNotebookIdCounter;
            }
        }

        if (notebook.contact()) {
            notebook.setContact(std::nullopt);
        }

        notebook.setUpdateSequenceNum(notebookCounter);
        ++notebookCounter;

        notebook.setDefaultNotebook(std::nullopt);
    }

    QFutureSynchronizer<void> putNotebooksSynchronizer;
    for (auto notebook: notebooks) {
        auto putNotebookFuture = notebooksHandler->putNotebook(
            std::move(notebook));

        putNotebooksSynchronizer.addFuture(putNotebookFuture);
    }

    EXPECT_NO_THROW(putNotebooksSynchronizer.waitForFinished());

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), notebooks.size());

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), notebooks.size());

    for (const auto & notebook: notebooks) {
        auto foundByLocalIdNotebookFuture =
            notebooksHandler->findNotebookByLocalId(notebook.localId());
        foundByLocalIdNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByLocalIdNotebookFuture.result(), notebook);

        auto foundByGuidNotebookFuture =
            notebooksHandler->findNotebookByGuid(notebook.guid().value());
        foundByGuidNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByGuidNotebookFuture.result(), notebook);

        auto foundByNameNotebookFuture =
            notebooksHandler->findNotebookByName(notebook.name().value());
        foundByNameNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByNameNotebookFuture.result(), notebook);
    }

    for (const auto & notebook: notebooks) {
        auto expungeNotebookByLocalIdFuture =
            notebooksHandler->expungeNotebookByLocalId(notebook.localId());
        expungeNotebookByLocalIdFuture.waitForFinished();
    }

    QCoreApplication::processEvents();

    EXPECT_EQ(
        notifierListener.expungeNotebookLocalIds().size(), notebooks.size());

    notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 0U);

    for (const auto & notebook: notebooks) {
        auto foundByLocalIdNotebookFuture =
            notebooksHandler->findNotebookByLocalId(notebook.localId());
        foundByLocalIdNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByLocalIdNotebookFuture.resultCount(), 0);

        auto foundByGuidNotebookFuture =
            notebooksHandler->findNotebookByGuid(notebook.guid().value());
        foundByGuidNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByGuidNotebookFuture.resultCount(), 0);

        auto foundByNameNotebookFuture =
            notebooksHandler->findNotebookByName(notebook.name().value());
        foundByNameNotebookFuture.waitForFinished();
        EXPECT_EQ(foundByNameNotebookFuture.resultCount(), 0);
    }
}

} // namespace quentier::local_storage::sql::tests

#include "NotebooksHandlerTest.moc"
