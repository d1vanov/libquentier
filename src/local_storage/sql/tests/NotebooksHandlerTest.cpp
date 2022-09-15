/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include "../LinkedNotebooksHandler.h"
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
// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

class NotebooksHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit NotebooksHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::Notebook> & putNotebooks()
        const noexcept
    {
        return m_putNotebooks;
    }

    [[nodiscard]] const QStringList & expungedNotebookLocalIds() const noexcept
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
    const CreateNotebookOptions createOptions = {})
{
    qevercloud::Notebook notebook;
    notebook.setLocallyModified(true);
    notebook.setLocalOnly(false);
    notebook.setLocallyFavorited(true);

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

        m_resourceDataFilesLock = std::make_shared<QReadWriteLock>();

        m_notifier = new Notifier;
        m_notifier->moveToThread(m_writerThread.get());

        QObject::connect(
            m_writerThread.get(), &QThread::finished, m_notifier,
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

TEST_F(NotebooksHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock));
}

TEST_F(NotebooksHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            nullptr, QThreadPool::globalInstance(), m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, nullptr, m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            nullptr, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, CtorNullResourceDataFilesLock)
{
    EXPECT_THROW(
        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), nullptr),
        IQuentierException);
}

TEST_F(NotebooksHandlerTest, ShouldHaveZeroNotebookCountWhenThereAreNoNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 0U);
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByLocalId)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto notebookFuture =
        notebooksHandler->findNotebookByLocalId(UidGenerator::Generate());

    notebookFuture.waitForFinished();
    ASSERT_EQ(notebookFuture.resultCount(), 1);
    EXPECT_FALSE(notebookFuture.result());
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByGuid)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto notebookFuture =
        notebooksHandler->findNotebookByGuid(UidGenerator::Generate());

    notebookFuture.waitForFinished();
    ASSERT_EQ(notebookFuture.resultCount(), 1);
    EXPECT_FALSE(notebookFuture.result());
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentNotebookByName)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto notebookFuture =
        notebooksHandler->findNotebookByName(QStringLiteral("My notebook"));

    notebookFuture.waitForFinished();
    ASSERT_EQ(notebookFuture.resultCount(), 1);
    EXPECT_FALSE(notebookFuture.result());
}

TEST_F(NotebooksHandlerTest, ShouldNotFindNonexistentDefaultNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto notebookFuture = notebooksHandler->findDefaultNotebook();
    notebookFuture.waitForFinished();
    ASSERT_EQ(notebookFuture.resultCount(), 1);
    EXPECT_FALSE(notebookFuture.result());
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByLocalId)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeNotebookFuture =
        notebooksHandler->expungeNotebookByLocalId(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByGuid)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeNotebookFuture =
        notebooksHandler->expungeNotebookByGuid(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, IgnoreAttemptToExpungeNonexistentNotebookByName)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeNotebookFuture =
        notebooksHandler->expungeNotebookByName(QStringLiteral("My notebook"));

    EXPECT_NO_THROW(expungeNotebookFuture.waitForFinished());
}

TEST_F(NotebooksHandlerTest, ShouldListNoNotebooksWhenThereAreNoNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto listNotebooksOptions = ILocalStorage::ListNotebooksOptions{};

    listNotebooksOptions.m_affiliation = ILocalStorage::Affiliation::Any;

    auto listNotebooksFuture =
        notebooksHandler->listNotebooks(listNotebooksOptions);

    listNotebooksFuture.waitForFinished();
    EXPECT_TRUE(listNotebooksFuture.result().isEmpty());
}

TEST_F(NotebooksHandlerTest, ShouldListNoSharedNotebooksForNonexistentNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto sharedNotebooksFuture =
        notebooksHandler->listSharedNotebooks(UidGenerator::Generate());

    sharedNotebooksFuture.waitForFinished();
    ASSERT_EQ(sharedNotebooksFuture.resultCount(), 1);
    EXPECT_TRUE(sharedNotebooksFuture.result().isEmpty());
}

TEST_F(NotebooksHandlerTest, ShouldListNoNotebookGuidsWhenThereAreNoNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto listNotebookGuidsFilters = ILocalStorage::ListGuidsFilters{};
    listNotebookGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Include;

    auto listNotebookGuidsFuture = notebooksHandler->listNotebookGuids(
        listNotebookGuidsFilters);

    listNotebookGuidsFuture.waitForFinished();
    ASSERT_EQ(listNotebookGuidsFuture.resultCount(), 1);
    EXPECT_TRUE(listNotebookGuidsFuture.result().isEmpty());
}

class NotebooksHandlerSingleNotebookTest :
    public NotebooksHandlerTest,
    public testing::WithParamInterface<qevercloud::Notebook>
{};

const std::array gNotebookTestValues{
    createNotebook(),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithBusinessNotebook}),
    createNotebook(CreateNotebookOptions{CreateNotebookOption::WithContact}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithRestrictions}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithRecipientSettings}),
    createNotebook(CreateNotebookOptions{CreateNotebookOption::WithPublishing}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithLinkedNotebookGuid}),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithBusinessNotebook),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithContact),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithRestrictions),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithRecipientSettings),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithPublishing),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithSharedNotebooks} |
        CreateNotebookOption::WithLinkedNotebookGuid),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithBusinessNotebook} |
        CreateNotebookOption::WithContact |
        CreateNotebookOption::WithRestrictions),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithBusinessNotebook} |
        CreateNotebookOption::WithRestrictions |
        CreateNotebookOption::WithPublishing),
    createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithContact} |
        CreateNotebookOption::WithRestrictions |
        CreateNotebookOption::WithPublishing |
        CreateNotebookOption::WithLinkedNotebookGuid)};

INSTANTIATE_TEST_SUITE_P(
    NotebooksHandlerSingleNotebookTestInstance,
    NotebooksHandlerSingleNotebookTest, testing::ValuesIn(gNotebookTestValues));

TEST_P(NotebooksHandlerSingleNotebookTest, HandleSingleNotebook)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    NotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::notebookPut, &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookPut);

    QObject::connect(
        m_notifier, &Notifier::notebookExpunged, &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookExpunged);

    const auto notebook = GetParam();

    // === Put ===

    if (notebook.linkedNotebookGuid()) {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path());

        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(notebook.linkedNotebookGuid());

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();
    }

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), 1);
    EXPECT_EQ(notifierListener.putNotebooks()[0], notebook);

    // === Count ===

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 1U);

    // === Find by local id ===

    auto foundByLocalIdNotebookFuture =
        notebooksHandler->findNotebookByLocalId(notebook.localId());

    foundByLocalIdNotebookFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdNotebookFuture.resultCount(), 1);
    EXPECT_EQ(foundByLocalIdNotebookFuture.result(), notebook);

    // === Find by guid ===

    auto foundByGuidNotebookFuture =
        notebooksHandler->findNotebookByGuid(notebook.guid().value());

    foundByGuidNotebookFuture.waitForFinished();
    ASSERT_EQ(foundByGuidNotebookFuture.resultCount(), 1);
    EXPECT_EQ(foundByGuidNotebookFuture.result(), notebook);

    // === Find by name ===

    auto foundByNameNotebookFuture = notebooksHandler->findNotebookByName(
        notebook.name().value(), notebook.linkedNotebookGuid());

    foundByNameNotebookFuture.waitForFinished();
    ASSERT_EQ(foundByNameNotebookFuture.resultCount(), 1);
    EXPECT_EQ(foundByNameNotebookFuture.result(), notebook);

    // === Find default ===

    auto foundDefaultNotebookFuture = notebooksHandler->findDefaultNotebook();
    foundDefaultNotebookFuture.waitForFinished();
    EXPECT_EQ(foundDefaultNotebookFuture.result(), notebook);

    // === List notebooks ===

    auto listNotebooksOptions = ILocalStorage::ListNotebooksOptions{};

    listNotebooksOptions.m_affiliation = ILocalStorage::Affiliation::Any;

    auto listNotebooksFuture =
        notebooksHandler->listNotebooks(listNotebooksOptions);

    listNotebooksFuture.waitForFinished();
    ASSERT_EQ(listNotebooksFuture.resultCount(), 1);
    auto notebooks = listNotebooksFuture.result();
    ASSERT_EQ(notebooks.size(), 1);
    EXPECT_EQ(notebooks[0], notebook);

    // === List notebook guids

    // == Including locally modified notebooks ==
    auto listNotebookGuidsFilters = ILocalStorage::ListGuidsFilters{};
    listNotebookGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Include;

    auto listNotebookGuidsFuture = notebooksHandler->listNotebookGuids(
        listNotebookGuidsFilters, notebook.linkedNotebookGuid());

    listNotebookGuidsFuture.waitForFinished();
    ASSERT_EQ(listNotebookGuidsFuture.resultCount(), 1);

    auto notebookGuids = listNotebookGuidsFuture.result();
    ASSERT_EQ(notebookGuids.size(), 1);
    EXPECT_EQ(*notebookGuids.constBegin(), notebooks[0].guid().value());

    // == Excluding locally modified notebooks ==
    listNotebookGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Exclude;

    listNotebookGuidsFuture = notebooksHandler->listNotebookGuids(
        listNotebookGuidsFilters, notebook.linkedNotebookGuid());

    listNotebookGuidsFuture.waitForFinished();
    ASSERT_EQ(listNotebookGuidsFuture.resultCount(), 1);

    notebookGuids = listNotebookGuidsFuture.result();
    EXPECT_TRUE(notebookGuids.isEmpty());

    // === Expunge notebook by local id ===

    auto expungeNotebookByLocalIdFuture =
        notebooksHandler->expungeNotebookByLocalId(notebook.localId());

    expungeNotebookByLocalIdFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedNotebookLocalIds().size(), 1);

    EXPECT_EQ(
        notifierListener.expungedNotebookLocalIds()[0], notebook.localId());

    auto checkNotebookDeleted = [&] {
        notebookCountFuture = notebooksHandler->notebookCount();
        notebookCountFuture.waitForFinished();
        EXPECT_EQ(notebookCountFuture.result(), 0U);

        foundByLocalIdNotebookFuture =
            notebooksHandler->findNotebookByLocalId(notebook.localId());

        foundByLocalIdNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundByLocalIdNotebookFuture.result());

        foundByGuidNotebookFuture =
            notebooksHandler->findNotebookByGuid(notebook.guid().value());

        foundByGuidNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByGuidNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidNotebookFuture.result());

        foundByNameNotebookFuture =
            notebooksHandler->findNotebookByName(notebook.name().value());

        foundByNameNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByNameNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundByNameNotebookFuture.result());

        foundDefaultNotebookFuture = notebooksHandler->findDefaultNotebook();
        foundDefaultNotebookFuture.waitForFinished();
        ASSERT_EQ(foundDefaultNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundDefaultNotebookFuture.result());

        listNotebooksFuture =
            notebooksHandler->listNotebooks(listNotebooksOptions);

        listNotebooksFuture.waitForFinished();
        EXPECT_TRUE(listNotebooksFuture.result().isEmpty());

        listNotebookGuidsFuture = notebooksHandler->listNotebookGuids(
            ILocalStorage::ListGuidsFilters{}, notebook.linkedNotebookGuid());

        listNotebookGuidsFuture.waitForFinished();
        ASSERT_EQ(listNotebookGuidsFuture.resultCount(), 1);

        notebookGuids = listNotebookGuidsFuture.result();
        EXPECT_TRUE(notebookGuids.isEmpty());
    };

    checkNotebookDeleted();

    // === Put notebook ===

    putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), 2);
    EXPECT_EQ(notifierListener.putNotebooks()[1], notebook);

    // === Expunge notebook by guid ===

    auto expungeNotebookByGuidFuture =
        notebooksHandler->expungeNotebookByGuid(notebook.guid().value());

    expungeNotebookByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedNotebookLocalIds().size(), 2);

    EXPECT_EQ(
        notifierListener.expungedNotebookLocalIds()[1], notebook.localId());

    checkNotebookDeleted();

    // === Put notebook ===

    putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), 3);
    EXPECT_EQ(notifierListener.putNotebooks()[2], notebook);

    // === Expunge notebook by name ===

    auto expungeNotebookByNameFuture = notebooksHandler->expungeNotebookByName(
        notebook.name().value(), notebook.linkedNotebookGuid());

    expungeNotebookByNameFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedNotebookLocalIds().size(), 3);

    EXPECT_EQ(
        notifierListener.expungedNotebookLocalIds()[2], notebook.localId());

    checkNotebookDeleted();
}

TEST_F(NotebooksHandlerTest, HandleMultipleNotebooks)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    NotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::notebookPut, &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookPut);

    QObject::connect(
        m_notifier, &Notifier::notebookExpunged, &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookExpunged);

    QStringList linkedNotebookGuids;
    auto notebooks = gNotebookTestValues;
    int notebookCounter = 2;
    qint64 sharedNotebookIdCounter = 6U;
    for (auto it = std::next(notebooks.begin()); // NOLINT
         it != notebooks.end(); ++it)
    {
        auto & notebook = *it;
        notebook.setLocalId(UidGenerator::Generate());
        notebook.setGuid(UidGenerator::Generate());

        notebook.setName(
            notebooks.begin()->name().value() + QStringLiteral(" #") +
            QString::number(notebookCounter));

        if (notebook.sharedNotebooks() &&
            !notebook.sharedNotebooks()->isEmpty()) {
            for (auto & sharedNotebook: *notebook.mutableSharedNotebooks()) {
                sharedNotebook.setNotebookGuid(notebook.guid());
                sharedNotebook.setId(sharedNotebookIdCounter);
                ++sharedNotebookIdCounter;
            }
        }

        if (notebook.contact()) {
            notebook.setContact(std::nullopt);
        }

        notebook.setLocallyModified(notebookCounter % 2 != 0);

        notebook.setUpdateSequenceNum(notebookCounter);
        ++notebookCounter;

        notebook.setDefaultNotebook(std::nullopt);

        if (notebook.linkedNotebookGuid()) {
            linkedNotebookGuids << *notebook.linkedNotebookGuid();
        }
    }

    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();
    }

    QFutureSynchronizer<void> putNotebooksSynchronizer;
    for (auto notebook: notebooks) {
        auto putNotebookFuture =
            notebooksHandler->putNotebook(std::move(notebook));

        putNotebooksSynchronizer.addFuture(putNotebookFuture);
    }

    EXPECT_NO_THROW(putNotebooksSynchronizer.waitForFinished());

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putNotebooks().size(), notebooks.size());

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    ASSERT_EQ(notebookCountFuture.resultCount(), 1);
    EXPECT_EQ(notebookCountFuture.result(), notebooks.size());

    for (const auto & notebook: notebooks) {
        auto foundByLocalIdNotebookFuture =
            notebooksHandler->findNotebookByLocalId(notebook.localId());
        foundByLocalIdNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdNotebookFuture.resultCount(), 1);
        EXPECT_EQ(foundByLocalIdNotebookFuture.result(), notebook);

        auto foundByGuidNotebookFuture =
            notebooksHandler->findNotebookByGuid(notebook.guid().value());
        foundByGuidNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByGuidNotebookFuture.resultCount(), 1);
        EXPECT_EQ(foundByGuidNotebookFuture.result(), notebook);

        auto foundByNameNotebookFuture =
            notebooksHandler->findNotebookByName(notebook.name().value());
        foundByNameNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByNameNotebookFuture.resultCount(), 1);
        EXPECT_EQ(foundByNameNotebookFuture.result(), notebook);
    }

    for (const auto & notebook: notebooks) {
        auto expungeNotebookByLocalIdFuture =
            notebooksHandler->expungeNotebookByLocalId(notebook.localId());
        expungeNotebookByLocalIdFuture.waitForFinished();
    }

    QCoreApplication::processEvents();

    EXPECT_EQ(
        notifierListener.expungedNotebookLocalIds().size(), notebooks.size());

    notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 0U);

    for (const auto & notebook: notebooks) {
        auto foundByLocalIdNotebookFuture =
            notebooksHandler->findNotebookByLocalId(notebook.localId());
        foundByLocalIdNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundByLocalIdNotebookFuture.result());

        auto foundByGuidNotebookFuture =
            notebooksHandler->findNotebookByGuid(notebook.guid().value());
        foundByGuidNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByGuidNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidNotebookFuture.result());

        auto foundByNameNotebookFuture =
            notebooksHandler->findNotebookByName(notebook.name().value());
        foundByNameNotebookFuture.waitForFinished();
        ASSERT_EQ(foundByNameNotebookFuture.resultCount(), 1);
        EXPECT_FALSE(foundByNameNotebookFuture.result());
    }
}

TEST_F(NotebooksHandlerTest, UseLinkedNotebookGuidWhenNameIsAmbiguous)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    NotebooksHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::notebookPut, &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookPut);

    QObject::connect(
        m_notifier, &Notifier::notebookExpunged, &notifierListener,
        &NotebooksHandlerTestNotifierListener::onNotebookExpunged);

    auto notebook1 = createNotebook();

    auto notebook2 = createNotebook(
        CreateNotebookOptions{CreateNotebookOption::WithLinkedNotebookGuid});

    notebook2.setDefaultNotebook(std::nullopt);

    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    qevercloud::LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(notebook2.linkedNotebookGuid());

    auto putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

    putLinkedNotebookFuture.waitForFinished();

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook1);
    putNotebookFuture.waitForFinished();

    putNotebookFuture = notebooksHandler->putNotebook(notebook2);
    putNotebookFuture.waitForFinished();

    auto findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook1.name().value(), QString{});
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_EQ(findNotebookFuture.result(), notebook1);

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook2.name().value(), notebook2.linkedNotebookGuid());
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_EQ(findNotebookFuture.result(), notebook2);

    auto expungeNotebookFuture = notebooksHandler->expungeNotebookByName(
        notebook2.name().value(), notebook2.linkedNotebookGuid());
    expungeNotebookFuture.waitForFinished();

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook1.name().value(), QString{});
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_EQ(findNotebookFuture.result(), notebook1);

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook2.name().value(), notebook2.linkedNotebookGuid());
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(findNotebookFuture.result());

    expungeNotebookFuture = notebooksHandler->expungeNotebookByName(
        notebook1.name().value(), QString{});
    expungeNotebookFuture.waitForFinished();

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook1.name().value(), QString{});
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(findNotebookFuture.result());

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook2.name().value(), notebook2.linkedNotebookGuid());
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(findNotebookFuture.result());

    putNotebookFuture = notebooksHandler->putNotebook(notebook1);
    putNotebookFuture.waitForFinished();

    putNotebookFuture = notebooksHandler->putNotebook(notebook2);
    putNotebookFuture.waitForFinished();

    expungeNotebookFuture = notebooksHandler->expungeNotebookByName(
        notebook1.name().value(), QString{});
    expungeNotebookFuture.waitForFinished();

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook1.name().value(), QString{});
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(findNotebookFuture.result());

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook2.name().value(), notebook2.linkedNotebookGuid());
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_EQ(findNotebookFuture.result(), notebook2);

    expungeNotebookFuture = notebooksHandler->expungeNotebookByName(
        notebook2.name().value(), notebook2.linkedNotebookGuid());
    expungeNotebookFuture.waitForFinished();

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook1.name().value(), QString{});
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(findNotebookFuture.result());

    findNotebookFuture = notebooksHandler->findNotebookByName(
        notebook2.name().value(), notebook2.linkedNotebookGuid());
    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_FALSE(findNotebookFuture.result());
}

// The test checks that NotebooksHandler doesn't confuse notebooks
// which names are very similar and differ only by the presence of diacritics
// in one of names
TEST_F(NotebooksHandlerTest, FindNotebookByNameWithDiacritics)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Notebook notebook1;
    notebook1.setGuid(UidGenerator::Generate());
    notebook1.setUpdateSequenceNum(1);
    notebook1.setName(QStringLiteral("notebook"));

    qevercloud::Notebook notebook2;
    notebook2.setGuid(UidGenerator::Generate());
    notebook2.setUpdateSequenceNum(2);
    notebook2.setName(QStringLiteral("Notébook"));

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook1);
    putNotebookFuture.waitForFinished();

    putNotebookFuture = notebooksHandler->putNotebook(notebook2);
    putNotebookFuture.waitForFinished();

    auto foundNotebookByNameFuture =
        notebooksHandler->findNotebookByName(notebook1.name().value());

    foundNotebookByNameFuture.waitForFinished();
    ASSERT_EQ(foundNotebookByNameFuture.resultCount(), 1);
    EXPECT_EQ(foundNotebookByNameFuture.result(), notebook1);

    foundNotebookByNameFuture =
        notebooksHandler->findNotebookByName(notebook2.name().value());

    foundNotebookByNameFuture.waitForFinished();
    ASSERT_EQ(foundNotebookByNameFuture.resultCount(), 1);
    EXPECT_EQ(foundNotebookByNameFuture.result(), notebook2);
}

// The test checks that updates of existing notebook in the local storage work
// as expected when updated notebook doesn't have several fields which existed
// for the original notebook
TEST_F(NotebooksHandlerTest, RemoveNotebookFieldsOnUpdate)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000049"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);
    notebook.setDefaultNotebook(true);

    notebook.setPublishing(qevercloud::Publishing{});
    notebook.mutablePublishing()->setUri(QStringLiteral("Fake publishing uri"));
    notebook.mutablePublishing()->setOrder(qevercloud::NoteSortOrder::CREATED);
    notebook.mutablePublishing()->setAscending(true);
    notebook.mutablePublishing()->setPublicDescription(
        QStringLiteral("Fake public description"));

    notebook.setPublished(true);
    notebook.setStack(QStringLiteral("Fake notebook stack"));

    notebook.setBusinessNotebook(qevercloud::BusinessNotebook{});
    notebook.mutableBusinessNotebook()->setNotebookDescription(
        QStringLiteral("Fake business notebook description"));
    notebook.mutableBusinessNotebook()->setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);
    notebook.mutableBusinessNotebook()->setRecommended(true);

    notebook.setRestrictions(qevercloud::NotebookRestrictions{});
    auto & notebookRestrictions = *notebook.mutableRestrictions();
    notebookRestrictions.setNoReadNotes(false);
    notebookRestrictions.setNoCreateNotes(false);
    notebookRestrictions.setNoUpdateNotes(false);
    notebookRestrictions.setNoExpungeNotes(true);
    notebookRestrictions.setNoShareNotes(false);
    notebookRestrictions.setNoEmailNotes(true);
    notebookRestrictions.setNoSendMessageToRecipients(false);
    notebookRestrictions.setNoUpdateNotebook(false);
    notebookRestrictions.setNoExpungeNotebook(true);
    notebookRestrictions.setNoSetDefaultNotebook(false);
    notebookRestrictions.setNoSetNotebookStack(true);
    notebookRestrictions.setNoPublishToPublic(false);
    notebookRestrictions.setNoPublishToBusinessLibrary(true);
    notebookRestrictions.setNoCreateTags(false);
    notebookRestrictions.setNoUpdateTags(false);
    notebookRestrictions.setNoExpungeTags(true);
    notebookRestrictions.setNoSetParentTag(false);
    notebookRestrictions.setNoCreateSharedNotebooks(false);
    notebookRestrictions.setNoUpdateNotebook(false);
    notebookRestrictions.setUpdateWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::ASSIGNED);
    notebookRestrictions.setExpungeWhichSharedNotebookRestrictions(
        qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS);

    qevercloud::SharedNotebook sharedNotebook;
    sharedNotebook.setId(1);
    sharedNotebook.setUserId(1);
    sharedNotebook.setNotebookGuid(notebook.guid());
    sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email"));
    sharedNotebook.setServiceCreated(1);
    sharedNotebook.setServiceUpdated(1);

    sharedNotebook.setGlobalId(
        QStringLiteral("Fake shared notebook global id"));

    sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username"));
    sharedNotebook.setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    sharedNotebook.setRecipientSettings(
        qevercloud::SharedNotebookRecipientSettings{});
    sharedNotebook.mutableRecipientSettings()->setReminderNotifyEmail(true);
    sharedNotebook.mutableRecipientSettings()->setReminderNotifyInApp(false);

    notebook.setSharedNotebooks(
        QList<qevercloud::SharedNotebook>() << sharedNotebook);

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    // Remove restrictions and shared notebooks
    qevercloud::Notebook updatedNotebook;
    updatedNotebook.setLocalId(notebook.localId());
    updatedNotebook.setGuid(notebook.guid());
    updatedNotebook.setUpdateSequenceNum(1);
    updatedNotebook.setName(QStringLiteral("Fake notebook name"));
    updatedNotebook.setServiceCreated(1);
    updatedNotebook.setServiceUpdated(1);
    updatedNotebook.setDefaultNotebook(true);

    updatedNotebook.setPublishing(qevercloud::Publishing{});
    updatedNotebook.mutablePublishing()->setUri(
        QStringLiteral("Fake publishing uri"));
    updatedNotebook.mutablePublishing()->setOrder(
        qevercloud::NoteSortOrder::CREATED);
    updatedNotebook.mutablePublishing()->setAscending(true);

    updatedNotebook.mutablePublishing()->setPublicDescription(
        QStringLiteral("Fake public description"));

    updatedNotebook.setPublished(true);
    updatedNotebook.setStack(QStringLiteral("Fake notebook stack"));

    updatedNotebook.setBusinessNotebook(qevercloud::BusinessNotebook{});
    updatedNotebook.mutableBusinessNotebook()->setNotebookDescription(
        QStringLiteral("Fake business notebook description"));

    updatedNotebook.mutableBusinessNotebook()->setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    updatedNotebook.mutableBusinessNotebook()->setRecommended(true);

    putNotebookFuture = notebooksHandler->putNotebook(updatedNotebook);
    putNotebookFuture.waitForFinished();

    auto foundNotebookFuture =
        notebooksHandler->findNotebookByLocalId(notebook.localId());

    foundNotebookFuture.waitForFinished();
    ASSERT_EQ(foundNotebookFuture.resultCount(), 1);
    EXPECT_EQ(*foundNotebookFuture.result(), updatedNotebook);
}

// The test checks that NotebooksHandler properly considers affiliation when
// listing notebooks
TEST_F(NotebooksHandlerTest, ListNotebooksWithAffiliation)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    qevercloud::LinkedNotebook linkedNotebook1;
    linkedNotebook1.setGuid(UidGenerator::Generate());
    linkedNotebook1.setUsername(QStringLiteral("username1"));

    qevercloud::LinkedNotebook linkedNotebook2;
    linkedNotebook2.setGuid(UidGenerator::Generate());
    linkedNotebook2.setUsername(QStringLiteral("username1"));

    auto putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook1);

    putLinkedNotebookFuture.waitForFinished();

    putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook2);

    putLinkedNotebookFuture.waitForFinished();

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Notebook userOwnNotebook1;
    userOwnNotebook1.setGuid(UidGenerator::Generate());
    userOwnNotebook1.setUpdateSequenceNum(1);
    userOwnNotebook1.setName(QStringLiteral("userOwnNotebook #1"));

    qevercloud::Notebook userOwnNotebook2;
    userOwnNotebook2.setGuid(UidGenerator::Generate());
    userOwnNotebook2.setUpdateSequenceNum(2);
    userOwnNotebook2.setName(QStringLiteral("userOwnNotebook #2"));

    qevercloud::Notebook notebookFromLinkedNotebook1;
    notebookFromLinkedNotebook1.setGuid(UidGenerator::Generate());
    notebookFromLinkedNotebook1.setUpdateSequenceNum(3);
    notebookFromLinkedNotebook1.setName(
        QStringLiteral("Notebook from linkedNotebook1"));
    notebookFromLinkedNotebook1.setLinkedNotebookGuid(linkedNotebook1.guid());

    qevercloud::Notebook notebookFromLinkedNotebook2;
    notebookFromLinkedNotebook2.setGuid(UidGenerator::Generate());
    notebookFromLinkedNotebook2.setUpdateSequenceNum(4);
    notebookFromLinkedNotebook2.setName(
        QStringLiteral("Notebook from linkedNotebook2"));
    notebookFromLinkedNotebook2.setLinkedNotebookGuid(linkedNotebook2.guid());

    auto putNotebookFuture = notebooksHandler->putNotebook(userOwnNotebook1);
    putNotebookFuture.waitForFinished();

    putNotebookFuture = notebooksHandler->putNotebook(userOwnNotebook2);
    putNotebookFuture.waitForFinished();

    putNotebookFuture =
        notebooksHandler->putNotebook(notebookFromLinkedNotebook1);

    putNotebookFuture.waitForFinished();

    putNotebookFuture =
        notebooksHandler->putNotebook(notebookFromLinkedNotebook2);

    putNotebookFuture.waitForFinished();

    auto listNotebooksOptions = ILocalStorage::ListNotebooksOptions{};

    listNotebooksOptions.m_affiliation = ILocalStorage::Affiliation::Any;

    auto listNotebooksFuture =
        notebooksHandler->listNotebooks(listNotebooksOptions);

    listNotebooksFuture.waitForFinished();

    auto notebooks = listNotebooksFuture.result();
    EXPECT_EQ(notebooks.size(), 4);
    EXPECT_TRUE(notebooks.contains(userOwnNotebook1));
    EXPECT_TRUE(notebooks.contains(userOwnNotebook2));
    EXPECT_TRUE(notebooks.contains(notebookFromLinkedNotebook1));
    EXPECT_TRUE(notebooks.contains(notebookFromLinkedNotebook2));

    listNotebooksOptions.m_affiliation =
        ILocalStorage::Affiliation::AnyLinkedNotebook;

    listNotebooksFuture = notebooksHandler->listNotebooks(listNotebooksOptions);
    listNotebooksFuture.waitForFinished();

    notebooks = listNotebooksFuture.result();
    EXPECT_EQ(notebooks.size(), 2);
    EXPECT_TRUE(notebooks.contains(notebookFromLinkedNotebook1));
    EXPECT_TRUE(notebooks.contains(notebookFromLinkedNotebook2));

    listNotebooksOptions.m_affiliation = ILocalStorage::Affiliation::User;

    listNotebooksFuture = notebooksHandler->listNotebooks(listNotebooksOptions);
    listNotebooksFuture.waitForFinished();

    notebooks = listNotebooksFuture.result();
    EXPECT_EQ(notebooks.size(), 2);
    EXPECT_TRUE(notebooks.contains(userOwnNotebook1));
    EXPECT_TRUE(notebooks.contains(userOwnNotebook2));

    listNotebooksOptions.m_affiliation =
        ILocalStorage::Affiliation::ParticularLinkedNotebooks;

    listNotebooksOptions.m_linkedNotebookGuids = QList<qevercloud::Guid>{}
        << linkedNotebook1.guid().value();

    listNotebooksFuture = notebooksHandler->listNotebooks(listNotebooksOptions);
    listNotebooksFuture.waitForFinished();

    notebooks = listNotebooksFuture.result();
    EXPECT_EQ(notebooks.size(), 1);
    EXPECT_TRUE(notebooks.contains(notebookFromLinkedNotebook1));

    listNotebooksOptions.m_linkedNotebookGuids = QList<qevercloud::Guid>{}
        << linkedNotebook2.guid().value();

    listNotebooksFuture = notebooksHandler->listNotebooks(listNotebooksOptions);
    listNotebooksFuture.waitForFinished();

    notebooks = listNotebooksFuture.result();
    EXPECT_EQ(notebooks.size(), 1);
    EXPECT_TRUE(notebooks.contains(notebookFromLinkedNotebook2));
}

} // namespace quentier::local_storage::sql::tests

#include "NotebooksHandlerTest.moc"
