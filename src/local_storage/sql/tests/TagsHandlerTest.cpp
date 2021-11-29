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
#include "../LinkedNotebooksHandler.h"
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
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <array>
#include <iterator>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

class TagsHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit TagsHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::Tag> & putTags() const noexcept
    {
        return m_putTags;
    }

    [[nodiscard]] const QStringList & expungedTagLocalIds() const noexcept
    {
        return m_expungedTagLocalIds;
    }

public Q_SLOTS:
    void onTagPut(qevercloud::Tag tag) // NOLINT
    {
        m_putTags << tag;
    }

    void onTagExpunged(QString tagLocalId, QStringList expungedChildTagLocalIds) // NOLINT
    {
        m_expungedTagLocalIds << tagLocalId;
        m_expungedTagLocalIds << expungedChildTagLocalIds;
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
    QTemporaryDir m_temporaryDir;
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

    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

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

    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTagsPerNoteLocalId(
        UidGenerator::Generate(), listTagsOptions);

    listTagsFuture.waitForFinished();
    EXPECT_TRUE(listTagsFuture.result().isEmpty());
}

class TagsHandlerSingleTagTest :
    public TagsHandlerTest,
    public testing::WithParamInterface<qevercloud::Tag>
{};

const std::array gTagTestValues{
    createTag(),
    createTag(CreateTagOptions{CreateTagOption::WithLinkedNotebookGuid})
};

INSTANTIATE_TEST_SUITE_P(
    TagsHandlerSingleTagTestInstance,
    TagsHandlerSingleTagTest,
    testing::ValuesIn(gTagTestValues));

TEST_P(TagsHandlerSingleTagTest, HandleSingleTag)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::tagPut,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier,
        &Notifier::tagExpunged,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagExpunged);

    const auto tag = GetParam();

    if (tag.linkedNotebookGuid()) {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread, m_temporaryDir.path());

        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(tag.linkedNotebookGuid());

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();
    }

    auto putTagFuture = tagsHandler->putTag(tag);
    putTagFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putTags().size(), 1);
    EXPECT_EQ(notifierListener.putTags()[0], tag);

    auto tagCountFuture = tagsHandler->tagCount();
    tagCountFuture.waitForFinished();
    EXPECT_EQ(tagCountFuture.result(), 1U);

    auto foundByLocalIdTagFuture = tagsHandler->findTagByLocalId(tag.localId());
    foundByLocalIdTagFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdTagFuture.resultCount(), 1);
    EXPECT_EQ(foundByLocalIdTagFuture.result(), tag);

    auto foundByGuidTagFuture = tagsHandler->findTagByGuid(tag.guid().value());
    foundByGuidTagFuture.waitForFinished();
    ASSERT_EQ(foundByGuidTagFuture.resultCount(), 1);
    EXPECT_EQ(foundByGuidTagFuture.result(), tag);

    auto foundByNameTagFuture = tagsHandler->findTagByName(
        tag.name().value(), tag.linkedNotebookGuid());

    foundByNameTagFuture.waitForFinished();
    ASSERT_EQ(foundByNameTagFuture.resultCount(), 1);
    EXPECT_EQ(foundByNameTagFuture.result(), tag);

    auto listTagsOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListTagsOrder>{};

    listTagsOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    auto tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 1);
    EXPECT_EQ(tags[0], tag);

    auto expungeTagByLocalIdFuture =
        tagsHandler->expungeTagByLocalId(tag.localId());

    expungeTagByLocalIdFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedTagLocalIds().size(), 1);
    EXPECT_EQ(notifierListener.expungedTagLocalIds()[0], tag.localId());

    auto checkTagDeleted = [&]
    {
        tagCountFuture = tagsHandler->tagCount();
        tagCountFuture.waitForFinished();
        EXPECT_EQ(tagCountFuture.result(), 0U);

        foundByLocalIdTagFuture = tagsHandler->findTagByLocalId(tag.localId());
        foundByLocalIdTagFuture.waitForFinished();
        EXPECT_EQ(foundByLocalIdTagFuture.resultCount(), 0);

        foundByGuidTagFuture = tagsHandler->findTagByGuid(tag.guid().value());
        foundByGuidTagFuture.waitForFinished();
        EXPECT_EQ(foundByGuidTagFuture.resultCount(), 0);

        foundByNameTagFuture = tagsHandler->findTagByName(
            tag.name().value(), tag.linkedNotebookGuid());

        foundByNameTagFuture.waitForFinished();
        EXPECT_EQ(foundByNameTagFuture.resultCount(), 0);

        listTagsFuture = tagsHandler->listTags(listTagsOptions);
        listTagsFuture.waitForFinished();
        EXPECT_TRUE(listTagsFuture.result().isEmpty());
    };

    checkTagDeleted();

    putTagFuture = tagsHandler->putTag(tag);
    putTagFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putTags().size(), 2);
    EXPECT_EQ(notifierListener.putTags()[1], tag);

    auto expungeTagByGuidFuture =
        tagsHandler->expungeTagByGuid(tag.guid().value());

    expungeTagByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedTagLocalIds().size(), 2);
    EXPECT_EQ(notifierListener.expungedTagLocalIds()[1], tag.localId());

    checkTagDeleted();

    putTagFuture = tagsHandler->putTag(tag);
    putTagFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putTags().size(), 3);
    EXPECT_EQ(notifierListener.putTags()[2], tag);

    auto expungeTagByNameFuture = tagsHandler->expungeTagByName(
        tag.name().value(), tag.linkedNotebookGuid());

    expungeTagByNameFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedTagLocalIds().size(), 3);
    EXPECT_EQ(notifierListener.expungedTagLocalIds()[2], tag.localId());

    checkTagDeleted();
}

TEST_F(TagsHandlerTest, HandleMultipleTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::tagPut,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier,
        &Notifier::tagExpunged,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagExpunged);

    QStringList linkedNotebookGuids;
    auto tags = gTagTestValues;
    for (const auto & tag: qAsConst(tags)) {
        if (tag.linkedNotebookGuid()) {
            linkedNotebookGuids << *tag.linkedNotebookGuid();
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

    QFutureSynchronizer<void> putTagsSynchronizer;
    for (auto tag: tags)
    {
        auto putTagFuture = tagsHandler->putTag(std::move(tag));
        putTagsSynchronizer.addFuture(putTagFuture);
    }

    EXPECT_NO_THROW(putTagsSynchronizer.waitForFinished());

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putTags().size(), tags.size());

    auto tagCountFuture = tagsHandler->tagCount();
    tagCountFuture.waitForFinished();
    EXPECT_EQ(tagCountFuture.result(), tags.size());

    for (const auto & tag: tags)
    {
        auto foundByLocalIdTagFuture =
            tagsHandler->findTagByLocalId(tag.localId());
        foundByLocalIdTagFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdTagFuture.resultCount(), 1);
        EXPECT_EQ(foundByLocalIdTagFuture.result(), tag);

        auto foundByGuidTagFuture =
            tagsHandler->findTagByGuid(tag.guid().value());
        foundByGuidTagFuture.waitForFinished();
        ASSERT_EQ(foundByGuidTagFuture.resultCount(), 1);
        EXPECT_EQ(foundByGuidTagFuture.result(), tag);

        auto foundByNameTagFuture = tagsHandler->findTagByName(
            tag.name().value(), tag.linkedNotebookGuid());
        foundByNameTagFuture.waitForFinished();
        ASSERT_EQ(foundByNameTagFuture.resultCount(), 1);
        EXPECT_EQ(foundByNameTagFuture.result(), tag);
    }

    for (const auto & tag: tags) {
        auto expungeTagByLocalIdFuture =
            tagsHandler->expungeTagByLocalId(tag.localId());
        expungeTagByLocalIdFuture.waitForFinished();
    }

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedTagLocalIds().size(), tags.size());

    tagCountFuture = tagsHandler->tagCount();
    tagCountFuture.waitForFinished();
    EXPECT_EQ(tagCountFuture.result(), 0U);

    for (const auto & tag: tags) {
        auto foundByLocalIdTagFuture =
            tagsHandler->findTagByLocalId(tag.localId());
        foundByLocalIdTagFuture.waitForFinished();
        EXPECT_EQ(foundByLocalIdTagFuture.resultCount(), 0);

        auto foundByGuidTagFuture =
            tagsHandler->findTagByGuid(tag.guid().value());
        foundByGuidTagFuture.waitForFinished();
        EXPECT_EQ(foundByGuidTagFuture.resultCount(), 0);

        auto foundByNameTagFuture = tagsHandler->findTagByName(
            tag.name().value(), tag.linkedNotebookGuid());
        foundByNameTagFuture.waitForFinished();
        EXPECT_EQ(foundByNameTagFuture.resultCount(), 0);
    }
}

TEST_F(TagsHandlerTest, UseLinkedNotebookGuidWhenNameIsAmbiguous)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::tagPut,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier,
        &Notifier::tagExpunged,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagExpunged);

    auto tag1 = createTag();

    auto tag2 = createTag(
        CreateTagOptions{CreateTagOption::WithLinkedNotebookGuid});

    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path());

    qevercloud::LinkedNotebook linkedNotebook;
    linkedNotebook.setGuid(tag2.linkedNotebookGuid());

    auto putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

    putLinkedNotebookFuture.waitForFinished();

    auto putTagFuture = tagsHandler->putTag(tag1);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(tag2);
    putTagFuture.waitForFinished();

    auto findTagFuture = tagsHandler->findTagByName(
        tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_EQ(findTagFuture.result(), tag1);

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_EQ(findTagFuture.result(), tag2);

    auto expungeTagFuture = tagsHandler->expungeTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(
        tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_EQ(findTagFuture.result(), tag1);

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);

    expungeTagFuture = tagsHandler->expungeTagByName(
        tag1.name().value(), QString{});
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(
        tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);

    putTagFuture = tagsHandler->putTag(tag1);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(tag2);
    putTagFuture.waitForFinished();

    expungeTagFuture = tagsHandler->expungeTagByName(
        tag1.name().value(), QString{});
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(
        tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_EQ(findTagFuture.result(), tag2);

    expungeTagFuture = tagsHandler->expungeTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(
        tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);
}

TEST_F(TagsHandlerTest, ExpungeChildTagsAlongWithParentTag)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::tagPut,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier,
        &Notifier::tagExpunged,
        &notifierListener,
        &TagsHandlerTestNotifierListener::onTagExpunged);

    auto tag1 = createTag();
    auto tag2 = createTag();
    tag2.setName(tag2.name().value() + QStringLiteral("#2"));
    tag2.setParentTagLocalId(tag1.localId());
    tag2.setParentGuid(tag1.guid());

    auto putTagFuture = tagsHandler->putTag(tag1);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(tag2);
    putTagFuture.waitForFinished();

    auto findTagFuture = tagsHandler->findTagByName(tag1.name().value());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_EQ(findTagFuture.result(), tag1);

    findTagFuture = tagsHandler->findTagByName(tag2.name().value());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_EQ(findTagFuture.result(), tag2);

    auto expungeTagFuture = tagsHandler->expungeTagByLocalId(tag1.localId());
    expungeTagFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedTagLocalIds().size(), 2);

    EXPECT_TRUE(
        notifierListener.expungedTagLocalIds().contains(tag1.localId()));

    EXPECT_TRUE(
        notifierListener.expungedTagLocalIds().contains(tag2.localId()));

    findTagFuture = tagsHandler->findTagByName(tag1.name().value());
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);

    findTagFuture = tagsHandler->findTagByName(tag2.name().value());
    findTagFuture.waitForFinished();
    EXPECT_EQ(findTagFuture.resultCount(), 0);
}

TEST_F(TagsHandlerTest, RefuseToPutTagWithUnknownParent)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto tag = createTag();
    tag.setParentTagLocalId(UidGenerator::Generate());
    tag.setParentGuid(UidGenerator::Generate());

    auto putTagFuture = tagsHandler->putTag(tag);
    EXPECT_THROW(putTagFuture.waitForFinished(), IQuentierException);
}

// The test checks that TagsHandler doesn't confuse tags which names are very
// similar and differ only by the presence of diacritics in one of names
TEST_F(TagsHandlerTest, FindTagByNameWithDiacritics)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    qevercloud::Tag tag1;
    tag1.setGuid(UidGenerator::Generate());
    tag1.setUpdateSequenceNum(1);
    tag1.setName(QStringLiteral("tag"));

    qevercloud::Tag tag2;
    tag2.setGuid(UidGenerator::Generate());
    tag2.setUpdateSequenceNum(2);
    tag2.setName(QStringLiteral("tāg"));

    auto putTagFuture = tagsHandler->putTag(tag1);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(tag2);
    putTagFuture.waitForFinished();

    auto foundTagByNameFuture = tagsHandler->findTagByName(tag1.name().value());
    foundTagByNameFuture.waitForFinished();
    ASSERT_EQ(foundTagByNameFuture.resultCount(), 1);
    EXPECT_EQ(foundTagByNameFuture.result(), tag1);

    foundTagByNameFuture = tagsHandler->findTagByName(tag2.name().value());
    foundTagByNameFuture.waitForFinished();
    ASSERT_EQ(foundTagByNameFuture.resultCount(), 1);
    EXPECT_EQ(foundTagByNameFuture.result(), tag2);
}

// The test checks that TagsHandler properly considers affiliation when listing
// tags
TEST_F(TagsHandlerTest, ListTagsWithAffiliation)
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

    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    qevercloud::Tag userOwnTag1;
    userOwnTag1.setGuid(UidGenerator::Generate());
    userOwnTag1.setUpdateSequenceNum(1);
    userOwnTag1.setName(QStringLiteral("userOwnTag #1"));

    qevercloud::Tag userOwnTag2;
    userOwnTag2.setGuid(UidGenerator::Generate());
    userOwnTag2.setUpdateSequenceNum(2);
    userOwnTag2.setName(QStringLiteral("userOwnTag #2"));

    qevercloud::Tag tagFromLinkedNotebook1;
    tagFromLinkedNotebook1.setGuid(UidGenerator::Generate());
    tagFromLinkedNotebook1.setUpdateSequenceNum(3);
    tagFromLinkedNotebook1.setName(QStringLiteral("Tag from linkedNotebook1"));
    tagFromLinkedNotebook1.setLinkedNotebookGuid(linkedNotebook1.guid());

    qevercloud::Tag tagFromLinkedNotebook2;
    tagFromLinkedNotebook2.setGuid(UidGenerator::Generate());
    tagFromLinkedNotebook2.setUpdateSequenceNum(4);
    tagFromLinkedNotebook2.setName(QStringLiteral("Tag from linkedNotebook2"));
    tagFromLinkedNotebook2.setLinkedNotebookGuid(linkedNotebook2.guid());

    auto putTagFuture = tagsHandler->putTag(userOwnTag1);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(userOwnTag2);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(tagFromLinkedNotebook1);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(tagFromLinkedNotebook2);
    putTagFuture.waitForFinished();

    auto listTagsOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListTagsOrder>{};

    listTagsOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    auto tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 4);
    EXPECT_TRUE(tags.contains(userOwnTag1));
    EXPECT_TRUE(tags.contains(userOwnTag2));
    EXPECT_TRUE(tags.contains(tagFromLinkedNotebook1));
    EXPECT_TRUE(tags.contains(tagFromLinkedNotebook2));

    listTagsOptions.m_affiliation =
        ILocalStorage::Affiliation::AnyLinkedNotebook;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 2);
    EXPECT_TRUE(tags.contains(tagFromLinkedNotebook1));
    EXPECT_TRUE(tags.contains(tagFromLinkedNotebook2));

    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::User;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 2);
    EXPECT_TRUE(tags.contains(userOwnTag1));
    EXPECT_TRUE(tags.contains(userOwnTag2));

    listTagsOptions.m_affiliation =
        ILocalStorage::Affiliation::ParticularLinkedNotebooks;

    listTagsOptions.m_linkedNotebookGuids = QList<qevercloud::Guid>{}
        << linkedNotebook1.guid().value();

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 1);
    EXPECT_TRUE(tags.contains(tagFromLinkedNotebook1));

    listTagsOptions.m_linkedNotebookGuids = QList<qevercloud::Guid>{}
        << linkedNotebook2.guid().value();

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 1);
    EXPECT_TRUE(tags.contains(tagFromLinkedNotebook2));
}

} // namespace quentier::local_storage::sql::tests

#include "TagsHandlerTest.moc"
