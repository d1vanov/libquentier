/*
 * Copyright 2021-2023 Dmitry Ivanov
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
#include "../NotebooksHandler.h"
#include "../NotesHandler.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"
#include "../TagsHandler.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

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
#include <utility>

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

    void onTagExpunged(
        QString tagLocalId, QStringList expungedChildTagLocalIds) // NOLINT
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
        m_connectionPool = utils::createConnectionPool();

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_writerThread = std::make_shared<QThread>();
        {
            auto nullDeleter = []([[maybe_unused]] QThreadPool * threadPool) {};
            m_threadPool = std::shared_ptr<QThreadPool>(
                QThreadPool::globalInstance(), std::move(nullDeleter));
        }

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
    threading::QThreadPtr m_writerThread;
    threading::QThreadPoolPtr m_threadPool;
    QReadWriteLockPtr m_resourceDataFilesLock;
    Notifier * m_notifier;
    QTemporaryDir m_temporaryDir;
};

} // namespace

TEST_F(TagsHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            m_writerThread));
}

TEST_F(TagsHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            nullptr, m_threadPool, m_notifier, m_writerThread),
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
            m_connectionPool, m_threadPool, nullptr,
            m_writerThread),
        IQuentierException);
}

TEST_F(TagsHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            nullptr),
        IQuentierException);
}

TEST_F(TagsHandlerTest, ShouldHaveZeroTagCountWhenThereAreNoTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto tagCountFuture = tagsHandler->tagCount();
    tagCountFuture.waitForFinished();
    EXPECT_EQ(tagCountFuture.result(), 0U);
}

TEST_F(TagsHandlerTest, ShouldNotFindNonexistentTagByLocalId)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto tagFuture = tagsHandler->findTagByLocalId(UidGenerator::Generate());
    tagFuture.waitForFinished();
    ASSERT_EQ(tagFuture.resultCount(), 1);
    EXPECT_FALSE(tagFuture.result());
}

TEST_F(TagsHandlerTest, ShouldNotFindNonexistentTagByGuid)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto tagFuture = tagsHandler->findTagByGuid(UidGenerator::Generate());
    tagFuture.waitForFinished();
    ASSERT_EQ(tagFuture.resultCount(), 1);
    EXPECT_FALSE(tagFuture.result());
}

TEST_F(TagsHandlerTest, ShouldNotFindNonexistentTagByName)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto tagFuture = tagsHandler->findTagByName(QStringLiteral("My tag"));
    tagFuture.waitForFinished();
    ASSERT_EQ(tagFuture.resultCount(), 1);
    EXPECT_FALSE(tagFuture.result());
}

TEST_F(TagsHandlerTest, IgnoreAttemptToExpungeNonexistentTagByLocalId)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto expungeTagFuture =
        tagsHandler->expungeTagByLocalId(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeTagFuture.waitForFinished());
}

TEST_F(TagsHandlerTest, IgnoreAttemptToExpungeNonexistentTagByGuid)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto expungeTagFuture =
        tagsHandler->expungeTagByGuid(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeTagFuture.waitForFinished());
}

TEST_F(TagsHandlerTest, IgnoreAttemptToExpungeNonexistentTagByName)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto expungeTagFuture =
        tagsHandler->expungeTagByName(QStringLiteral("My tag"));

    EXPECT_NO_THROW(expungeTagFuture.waitForFinished());
}

TEST_F(TagsHandlerTest, ShouldListNoTagsWhenThereAreNoTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto listTagsOptions = ILocalStorage::ListTagsOptions{};
    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();
    EXPECT_TRUE(listTagsFuture.result().isEmpty());
}

TEST_F(TagsHandlerTest, ShouldListNoTagsPerNoteWhenThereAreNoTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto listTagsOptions = ILocalStorage::ListTagsOptions{};
    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTagsPerNoteLocalId(
        UidGenerator::Generate(), listTagsOptions);

    listTagsFuture.waitForFinished();
    EXPECT_TRUE(listTagsFuture.result().isEmpty());
}

TEST_F(TagsHandlerTest, ShouldListNoTagGuidsWhenThereAreNoTags)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    auto listTagGuidsFilters = ILocalStorage::ListGuidsFilters{};
    listTagGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Include;

    auto listTagGuidsFuture = tagsHandler->listTagGuids(listTagGuidsFilters);
    listTagGuidsFuture.waitForFinished();
    ASSERT_EQ(listTagGuidsFuture.resultCount(), 1);
    EXPECT_TRUE(listTagGuidsFuture.result().isEmpty());
}

class TagsHandlerSingleTagTest :
    public TagsHandlerTest,
    public testing::WithParamInterface<qevercloud::Tag>
{};

const std::array gTagTestValues{
    createTag(),
    createTag(CreateTagOptions{CreateTagOption::WithLinkedNotebookGuid})};

INSTANTIATE_TEST_SUITE_P(
    TagsHandlerSingleTagTestInstance, TagsHandlerSingleTagTest,
    testing::ValuesIn(gTagTestValues));

TEST_P(TagsHandlerSingleTagTest, HandleSingleTag)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::tagPut, &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier, &Notifier::tagExpunged, &notifierListener,
        &TagsHandlerTestNotifierListener::onTagExpunged);

    const auto tag = GetParam();

    // === Put ===

    if (tag.linkedNotebookGuid()) {
        const auto linkedNotebooksHandler =
            std::make_shared<LinkedNotebooksHandler>(
                m_connectionPool, m_threadPool, m_notifier,
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

    // === Count ===

    auto tagCountFuture = tagsHandler->tagCount();
    tagCountFuture.waitForFinished();
    EXPECT_EQ(tagCountFuture.result(), 1U);

    // === Find by local id ===

    auto foundByLocalIdTagFuture = tagsHandler->findTagByLocalId(tag.localId());
    foundByLocalIdTagFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdTagFuture.resultCount(), 1);
    ASSERT_TRUE(foundByLocalIdTagFuture.result());
    EXPECT_EQ(*foundByLocalIdTagFuture.result(), tag);

    // === Find by guid ===

    auto foundByGuidTagFuture = tagsHandler->findTagByGuid(tag.guid().value());
    foundByGuidTagFuture.waitForFinished();
    ASSERT_EQ(foundByGuidTagFuture.resultCount(), 1);
    ASSERT_TRUE(foundByGuidTagFuture.result());
    EXPECT_EQ(*foundByGuidTagFuture.result(), tag);

    // === Find by name ===

    auto foundByNameTagFuture = tagsHandler->findTagByName(
        tag.name().value(), tag.linkedNotebookGuid());

    foundByNameTagFuture.waitForFinished();
    ASSERT_EQ(foundByNameTagFuture.resultCount(), 1);
    ASSERT_TRUE(foundByNameTagFuture.result());
    EXPECT_EQ(*foundByNameTagFuture.result(), tag);

    // === List tags ===

    auto listTagsOptions = ILocalStorage::ListTagsOptions{};
    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    auto tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 1);
    EXPECT_EQ(tags[0], tag);

    // === List tag guids ===

    // == Including locally modified tags ==
    auto listTagGuidsFilters = ILocalStorage::ListGuidsFilters{};
    listTagGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Include;

    auto listTagGuidsFuture = tagsHandler->listTagGuids(
        listTagGuidsFilters, tag.linkedNotebookGuid());

    listTagGuidsFuture.waitForFinished();
    ASSERT_EQ(listTagGuidsFuture.resultCount(), 1);

    auto tagGuids = listTagGuidsFuture.result();
    ASSERT_EQ(tagGuids.size(), 1);
    EXPECT_EQ(*tagGuids.constBegin(), tag.guid().value());

    // == Excluding locally modified tags ==
    listTagGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Exclude;

    listTagGuidsFuture = tagsHandler->listTagGuids(
        listTagGuidsFilters, tag.linkedNotebookGuid());

    listTagGuidsFuture.waitForFinished();
    ASSERT_EQ(listTagGuidsFuture.resultCount(), 1);

    tagGuids = listTagGuidsFuture.result();
    EXPECT_TRUE(tagGuids.isEmpty());

    // === Expunge tag by local id ===

    auto expungeTagByLocalIdFuture =
        tagsHandler->expungeTagByLocalId(tag.localId());

    expungeTagByLocalIdFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedTagLocalIds().size(), 1);
    EXPECT_EQ(notifierListener.expungedTagLocalIds()[0], tag.localId());

    auto checkTagDeleted = [&] {
        tagCountFuture = tagsHandler->tagCount();
        tagCountFuture.waitForFinished();
        EXPECT_EQ(tagCountFuture.result(), 0U);

        foundByLocalIdTagFuture = tagsHandler->findTagByLocalId(tag.localId());
        foundByLocalIdTagFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdTagFuture.resultCount(), 1);
        EXPECT_FALSE(foundByLocalIdTagFuture.result());

        foundByGuidTagFuture = tagsHandler->findTagByGuid(tag.guid().value());
        foundByGuidTagFuture.waitForFinished();
        ASSERT_EQ(foundByGuidTagFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidTagFuture.result());

        foundByNameTagFuture = tagsHandler->findTagByName(
            tag.name().value(), tag.linkedNotebookGuid());

        foundByNameTagFuture.waitForFinished();
        ASSERT_EQ(foundByNameTagFuture.resultCount(), 1);
        EXPECT_FALSE(foundByNameTagFuture.result());

        listTagsFuture = tagsHandler->listTags(listTagsOptions);
        listTagsFuture.waitForFinished();
        EXPECT_TRUE(listTagsFuture.result().isEmpty());

        listTagGuidsFuture = tagsHandler->listTagGuids(
            ILocalStorage::ListGuidsFilters{}, tag.linkedNotebookGuid());

        listTagGuidsFuture.waitForFinished();
        ASSERT_EQ(listTagGuidsFuture.resultCount(), 1);

        tagGuids = listTagGuidsFuture.result();
        EXPECT_TRUE(tagGuids.isEmpty());
    };

    checkTagDeleted();

    // === Put tag ===

    putTagFuture = tagsHandler->putTag(tag);
    putTagFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putTags().size(), 2);
    EXPECT_EQ(notifierListener.putTags()[1], tag);

    // === Expunge tag by guid ===

    auto expungeTagByGuidFuture =
        tagsHandler->expungeTagByGuid(tag.guid().value());

    expungeTagByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedTagLocalIds().size(), 2);
    EXPECT_EQ(notifierListener.expungedTagLocalIds()[1], tag.localId());

    checkTagDeleted();

    // === Put tag ===

    putTagFuture = tagsHandler->putTag(tag);
    putTagFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putTags().size(), 3);
    EXPECT_EQ(notifierListener.putTags()[2], tag);

    // === Expunge tag by name ===

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
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::tagPut, &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier, &Notifier::tagExpunged, &notifierListener,
        &TagsHandlerTestNotifierListener::onTagExpunged);

    QStringList linkedNotebookGuids;
    auto tags = gTagTestValues;
    for (const auto & tag: std::as_const(tags)) {
        if (tag.linkedNotebookGuid()) {
            linkedNotebookGuids << *tag.linkedNotebookGuid();
        }
    }

    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            m_writerThread, m_temporaryDir.path());

    for (const auto & linkedNotebookGuid: std::as_const(linkedNotebookGuids)) {
        qevercloud::LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(linkedNotebookGuid);

        auto putLinkedNotebookFuture =
            linkedNotebooksHandler->putLinkedNotebook(linkedNotebook);

        putLinkedNotebookFuture.waitForFinished();
    }

    QFutureSynchronizer<void> putTagsSynchronizer;
    for (auto tag: tags) {
        auto putTagFuture = tagsHandler->putTag(std::move(tag));
        putTagsSynchronizer.addFuture(putTagFuture);
    }

    EXPECT_NO_THROW(putTagsSynchronizer.waitForFinished());

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putTags().size(), tags.size());

    auto tagCountFuture = tagsHandler->tagCount();
    tagCountFuture.waitForFinished();
    EXPECT_EQ(tagCountFuture.result(), tags.size());

    for (const auto & tag: tags) {
        auto foundByLocalIdTagFuture =
            tagsHandler->findTagByLocalId(tag.localId());
        foundByLocalIdTagFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdTagFuture.resultCount(), 1);
        ASSERT_TRUE(foundByLocalIdTagFuture.result());
        EXPECT_EQ(foundByLocalIdTagFuture.result(), tag);

        auto foundByGuidTagFuture =
            tagsHandler->findTagByGuid(tag.guid().value());
        foundByGuidTagFuture.waitForFinished();
        ASSERT_EQ(foundByGuidTagFuture.resultCount(), 1);
        ASSERT_TRUE(foundByGuidTagFuture.result());
        EXPECT_EQ(foundByGuidTagFuture.result(), tag);

        auto foundByNameTagFuture = tagsHandler->findTagByName(
            tag.name().value(), tag.linkedNotebookGuid());
        foundByNameTagFuture.waitForFinished();
        ASSERT_EQ(foundByNameTagFuture.resultCount(), 1);
        ASSERT_TRUE(foundByNameTagFuture.result());
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
        ASSERT_EQ(foundByLocalIdTagFuture.resultCount(), 1);
        EXPECT_FALSE(foundByLocalIdTagFuture.result());

        auto foundByGuidTagFuture =
            tagsHandler->findTagByGuid(tag.guid().value());
        foundByGuidTagFuture.waitForFinished();
        ASSERT_EQ(foundByGuidTagFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidTagFuture.result());

        auto foundByNameTagFuture = tagsHandler->findTagByName(
            tag.name().value(), tag.linkedNotebookGuid());
        foundByNameTagFuture.waitForFinished();
        ASSERT_EQ(foundByNameTagFuture.resultCount(), 1);
        EXPECT_FALSE(foundByNameTagFuture.result());
    }
}

TEST_F(TagsHandlerTest, UseLinkedNotebookGuidWhenNameIsAmbiguous)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::tagPut, &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier, &Notifier::tagExpunged, &notifierListener,
        &TagsHandlerTestNotifierListener::onTagExpunged);

    auto tag1 = createTag();

    auto tag2 =
        createTag(CreateTagOptions{CreateTagOption::WithLinkedNotebookGuid});

    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_threadPool, m_notifier,
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

    auto findTagFuture =
        tagsHandler->findTagByName(tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    ASSERT_TRUE(findTagFuture.result());
    EXPECT_EQ(findTagFuture.result(), tag1);

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    ASSERT_TRUE(findTagFuture.result());
    EXPECT_EQ(findTagFuture.result(), tag2);

    auto expungeTagFuture = tagsHandler->expungeTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    ASSERT_TRUE(findTagFuture.result());
    EXPECT_EQ(findTagFuture.result(), tag1);

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());

    expungeTagFuture =
        tagsHandler->expungeTagByName(tag1.name().value(), QString{});
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());

    putTagFuture = tagsHandler->putTag(tag1);
    putTagFuture.waitForFinished();

    putTagFuture = tagsHandler->putTag(tag2);
    putTagFuture.waitForFinished();

    expungeTagFuture =
        tagsHandler->expungeTagByName(tag1.name().value(), QString{});
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    ASSERT_TRUE(findTagFuture.result());
    EXPECT_EQ(findTagFuture.result(), tag2);

    expungeTagFuture = tagsHandler->expungeTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    expungeTagFuture.waitForFinished();

    findTagFuture = tagsHandler->findTagByName(tag1.name().value(), QString{});
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());

    findTagFuture = tagsHandler->findTagByName(
        tag2.name().value(), tag2.linkedNotebookGuid());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());
}

TEST_F(TagsHandlerTest, ExpungeChildTagsAlongWithParentTag)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    TagsHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::tagPut, &notifierListener,
        &TagsHandlerTestNotifierListener::onTagPut);

    QObject::connect(
        m_notifier, &Notifier::tagExpunged, &notifierListener,
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
    ASSERT_TRUE(findTagFuture.result());
    EXPECT_EQ(findTagFuture.result(), tag1);

    findTagFuture = tagsHandler->findTagByName(tag2.name().value());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    ASSERT_TRUE(findTagFuture.result());
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
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());

    findTagFuture = tagsHandler->findTagByName(tag2.name().value());
    findTagFuture.waitForFinished();
    ASSERT_EQ(findTagFuture.resultCount(), 1);
    EXPECT_FALSE(findTagFuture.result());
}

TEST_F(TagsHandlerTest, RefuseToPutTagWithUnknownParent)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
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
        m_connectionPool, m_threadPool, m_notifier,
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
    ASSERT_TRUE(foundTagByNameFuture.result());
    EXPECT_EQ(foundTagByNameFuture.result(), tag1);

    foundTagByNameFuture = tagsHandler->findTagByName(tag2.name().value());
    foundTagByNameFuture.waitForFinished();
    ASSERT_EQ(foundTagByNameFuture.resultCount(), 1);
    ASSERT_TRUE(foundTagByNameFuture.result());
    EXPECT_EQ(foundTagByNameFuture.result(), tag2);
}

// The test checks that TagsHandler properly considers affiliation when listing
// tags
TEST_F(TagsHandlerTest, ListTagsWithAffiliation)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_threadPool, m_notifier,
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
        m_connectionPool, m_threadPool, m_notifier,
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

    auto listTagsOptions = ILocalStorage::ListTagsOptions{};
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

// The test checks that TagsHandler properly considers TagNotesRelation when
// listing tags from user's own account
TEST_F(TagsHandlerTest, ListUserOwnTagsConsideringTagNotesRelation)
{
    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    qevercloud::Tag tag1;
    tag1.setGuid(UidGenerator::Generate());
    tag1.setUpdateSequenceNum(1);
    tag1.setName(QStringLiteral("Tag 1"));

    qevercloud::Tag tag2;
    tag2.setGuid(UidGenerator::Generate());
    tag2.setUpdateSequenceNum(2);
    tag2.setName(QStringLiteral("Tag 2"));

    qevercloud::Tag tag3;
    tag3.setGuid(UidGenerator::Generate());
    tag3.setUpdateSequenceNum(3);
    tag3.setName(QStringLiteral("Tag 3"));

    qevercloud::Tag tag4;
    tag4.setGuid(UidGenerator::Generate());
    tag4.setUpdateSequenceNum(4);
    tag4.setName(QStringLiteral("Tag 4"));

    auto tags = QList<qevercloud::Tag>{} << tag1 << tag2 << tag3 << tag4;
    for (const auto & tag: std::as_const(tags)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Notebook notebook1;
    notebook1.setGuid(UidGenerator::Generate());
    notebook1.setUpdateSequenceNum(5);
    notebook1.setName(QStringLiteral("Notebook 1"));

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook1);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Note note;
    note.setGuid(UidGenerator::Generate());
    note.setUpdateSequenceNum(6);
    note.setTitle(QStringLiteral("Note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook1.guid());
    note.setNotebookLocalId(notebook1.localId());
    note.setTagLocalIds(QStringList{} << tag1.localId() << tag2.localId());

    auto putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    auto listTagsOptions = ILocalStorage::ListTagsOptions{};
    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 4);
    EXPECT_TRUE(tags.contains(tag1));
    EXPECT_TRUE(tags.contains(tag2));
    EXPECT_TRUE(tags.contains(tag3));
    EXPECT_TRUE(tags.contains(tag4));

    listTagsOptions.m_tagNotesRelation =
        ILocalStorage::TagNotesRelation::WithNotes;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 2);
    EXPECT_TRUE(tags.contains(tag1));
    EXPECT_TRUE(tags.contains(tag2));

    listTagsOptions.m_tagNotesRelation =
        ILocalStorage::TagNotesRelation::WithoutNotes;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 2);
    EXPECT_TRUE(tags.contains(tag3));
    EXPECT_TRUE(tags.contains(tag4));

    listTagsOptions.m_affiliation =
        ILocalStorage::Affiliation::AnyLinkedNotebook;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_TRUE(tags.empty());

    listTagsOptions.m_tagNotesRelation =
        ILocalStorage::TagNotesRelation::WithNotes;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_TRUE(tags.empty());

    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_TRUE(tags.empty());
}

TEST_F(TagsHandlerTest, ListTagsFromLinkedNotebooksConsideringTagNotesRelation)
{
    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            m_writerThread, m_temporaryDir.path());

    qevercloud::LinkedNotebook linkedNotebook1;
    linkedNotebook1.setGuid(UidGenerator::Generate());
    linkedNotebook1.setUpdateSequenceNum(1);
    linkedNotebook1.setUsername(QStringLiteral("username1"));

    auto putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook1);

    putLinkedNotebookFuture.waitForFinished();

    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    qevercloud::Tag tag1;
    tag1.setGuid(UidGenerator::Generate());
    tag1.setUpdateSequenceNum(2);
    tag1.setName(QStringLiteral("Tag 1"));
    tag1.setLinkedNotebookGuid(linkedNotebook1.guid());

    qevercloud::Tag tag2;
    tag2.setGuid(UidGenerator::Generate());
    tag2.setUpdateSequenceNum(3);
    tag2.setName(QStringLiteral("Tag 2"));
    tag2.setLinkedNotebookGuid(linkedNotebook1.guid());

    qevercloud::Tag tag3;
    tag3.setGuid(UidGenerator::Generate());
    tag3.setUpdateSequenceNum(4);
    tag3.setName(QStringLiteral("Tag 3"));
    tag3.setLinkedNotebookGuid(linkedNotebook1.guid());

    qevercloud::Tag tag4;
    tag4.setGuid(UidGenerator::Generate());
    tag4.setUpdateSequenceNum(5);
    tag4.setName(QStringLiteral("Tag 4"));
    tag4.setLinkedNotebookGuid(linkedNotebook1.guid());

    auto tags = QList<qevercloud::Tag>{} << tag1 << tag2 << tag3 << tag4;
    for (const auto & tag: std::as_const(tags)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Notebook notebook1;
    notebook1.setGuid(UidGenerator::Generate());
    notebook1.setUpdateSequenceNum(6);
    notebook1.setName(QStringLiteral("Notebook 1"));
    notebook1.setLinkedNotebookGuid(linkedNotebook1.guid());

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook1);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Note note;
    note.setGuid(UidGenerator::Generate());
    note.setUpdateSequenceNum(7);
    note.setTitle(QStringLiteral("Note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook1.guid());
    note.setNotebookLocalId(notebook1.localId());
    note.setTagLocalIds(QStringList{} << tag1.localId() << tag2.localId());

    auto putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    auto listTagsOptions = ILocalStorage::ListTagsOptions{};
    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::Any;
    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    auto listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 4);
    EXPECT_TRUE(tags.contains(tag1));
    EXPECT_TRUE(tags.contains(tag2));
    EXPECT_TRUE(tags.contains(tag3));
    EXPECT_TRUE(tags.contains(tag4));

    listTagsOptions.m_tagNotesRelation =
        ILocalStorage::TagNotesRelation::WithNotes;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 2);
    EXPECT_TRUE(tags.contains(tag1));
    EXPECT_TRUE(tags.contains(tag2));

    listTagsOptions.m_tagNotesRelation =
        ILocalStorage::TagNotesRelation::WithoutNotes;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_EQ(tags.size(), 2);
    EXPECT_TRUE(tags.contains(tag3));
    EXPECT_TRUE(tags.contains(tag4));

    listTagsOptions.m_affiliation = ILocalStorage::Affiliation::User;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_TRUE(tags.empty());

    listTagsOptions.m_tagNotesRelation =
        ILocalStorage::TagNotesRelation::WithNotes;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_TRUE(tags.empty());

    listTagsOptions.m_tagNotesRelation = ILocalStorage::TagNotesRelation::Any;

    listTagsFuture = tagsHandler->listTags(listTagsOptions);
    listTagsFuture.waitForFinished();

    tags = listTagsFuture.result();
    EXPECT_TRUE(tags.empty());
}

Q_GLOBAL_STATIC_WITH_ARGS(
    qevercloud::Guid, gLinkedNotebookGuid1ForListGuidsTest,
    (UidGenerator::Generate()));

Q_GLOBAL_STATIC_WITH_ARGS(
    qevercloud::Guid, gLinkedNotebookGuid2ForListGuidsTest,
    (UidGenerator::Generate()));

const QList<qevercloud::Tag> gTagsForListGuidsTest =
    QList<qevercloud::Tag>{}
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 1"))
           .setLocallyModified(false)
           .setLocallyFavorited(false)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 2"))
           .setLocallyModified(true)
           .setLocallyFavorited(false)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 3"))
           .setLocallyModified(false)
           .setLocallyFavorited(true)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 4"))
           .setLocallyModified(true)
           .setLocallyFavorited(true)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 5"))
           .setLocallyModified(false)
           .setLocallyFavorited(false)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid1ForListGuidsTest)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 6"))
           .setLocallyModified(true)
           .setLocallyFavorited(false)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid1ForListGuidsTest)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 7"))
           .setLocallyModified(false)
           .setLocallyFavorited(true)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid1ForListGuidsTest)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 8"))
           .setLocallyModified(true)
           .setLocallyFavorited(true)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid1ForListGuidsTest)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 9"))
           .setLocallyModified(false)
           .setLocallyFavorited(false)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid2ForListGuidsTest)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 10"))
           .setLocallyModified(true)
           .setLocallyFavorited(false)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid2ForListGuidsTest)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 11"))
           .setLocallyModified(false)
           .setLocallyFavorited(true)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid2ForListGuidsTest)
           .build()
    << qevercloud::TagBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Tag 12"))
           .setLocallyModified(true)
           .setLocallyFavorited(true)
           .setLinkedNotebookGuid(*gLinkedNotebookGuid2ForListGuidsTest)
           .build();

struct ListTagGuidsTestData
{
    // Input data
    ILocalStorage::ListGuidsFilters filters;
    std::optional<qevercloud::Guid> linkedNotebookGuid;
    // Expected indexes of notebook guids
    QSet<int> expectedIndexes;
};

const QList<ListTagGuidsTestData> gListTagGuidsTestData =
    QList<ListTagGuidsTestData>{}
    << ListTagGuidsTestData{
        {}, // filters
        std::nullopt, // linked notebook guid
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7 << 8 << 9 << 10
                    << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 1 << 3 << 5 << 7 << 9 << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 0 << 2 << 4 << 6 << 8 << 10, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 2 << 3 << 6 << 7 << 10 << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 0 << 1 << 4 << 5 << 8 << 9, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 3 << 7 << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 0 << 4 << 8, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 1 << 5 << 9, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        std::nullopt, //linked notebook guid
        QSet<int>{} << 2 << 6 << 10, // expected indexes
    }
    << ListTagGuidsTestData{
        {}, // filters
        qevercloud::Guid{}, // linked notebook guid
        QSet<int>{} << 0 << 1 << 2 << 3, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 1 << 3, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 0 << 2, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 2 << 3, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 0 << 1, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 3, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 0, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 1, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        qevercloud::Guid{}, //linked notebook guid
        QSet<int>{} << 2, // expected indexes
    }
    << ListTagGuidsTestData{
        {}, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, // linked notebook guid
        QSet<int>{} << 4 << 5 << 6 << 7, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 5 << 7, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 4 << 6, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 6 << 7, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 4 << 5, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 7, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 4, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 5, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        *gLinkedNotebookGuid1ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 6, // expected indexes
    }
    << ListTagGuidsTestData{
        {}, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, // linked notebook guid
        QSet<int>{} << 8 << 9 << 10 << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 9 << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 8 << 10, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 10 << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 8 << 9, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 11, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 8, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 9, // expected indexes
    }
    << ListTagGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        *gLinkedNotebookGuid2ForListGuidsTest, //linked notebook guid
        QSet<int>{} << 10, // expected indexes
    };

class TagsHandlerListGuidsTest :
    public TagsHandlerTest,
    public testing::WithParamInterface<ListTagGuidsTestData>
{};

INSTANTIATE_TEST_SUITE_P(
    TagsHandlerListGuidsTestInstance, TagsHandlerListGuidsTest,
    testing::ValuesIn(gListTagGuidsTestData));

TEST_P(TagsHandlerListGuidsTest, ListTagGuids)
{
    // Set up linked notebooks and tags
    qevercloud::LinkedNotebook linkedNotebook1;
    linkedNotebook1.setGuid(*gLinkedNotebookGuid1ForListGuidsTest);
    linkedNotebook1.setUsername(QStringLiteral("username1"));

    qevercloud::LinkedNotebook linkedNotebook2;
    linkedNotebook2.setGuid(*gLinkedNotebookGuid2ForListGuidsTest);
    linkedNotebook2.setUsername(QStringLiteral("username2"));

    const auto linkedNotebooksHandler =
        std::make_shared<LinkedNotebooksHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            m_writerThread, m_temporaryDir.path());

    auto putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook1);

    putLinkedNotebookFuture.waitForFinished();

    putLinkedNotebookFuture =
        linkedNotebooksHandler->putLinkedNotebook(linkedNotebook2);

    putLinkedNotebookFuture.waitForFinished();

    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread);

    for (const auto & tag: std::as_const(gTagsForListGuidsTest)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    // Test the results of tag guids listing
    const auto testData = GetParam();
    auto listTagGuidsFuture = tagsHandler->listTagGuids(
        testData.filters, testData.linkedNotebookGuid);

    listTagGuidsFuture.waitForFinished();
    ASSERT_EQ(listTagGuidsFuture.resultCount(), 1);

    const QSet<qevercloud::Guid> expectedGuids = [&] {
        QSet<qevercloud::Guid> result;
        result.reserve(testData.expectedIndexes.size());
        for (const int index: testData.expectedIndexes) {
            result.insert(gTagsForListGuidsTest[index].guid().value());
        }
        return result;
    }();

    EXPECT_EQ(listTagGuidsFuture.result().size(), expectedGuids.size());
    EXPECT_EQ(listTagGuidsFuture.result(), expectedGuids);
}

} // namespace quentier::local_storage::sql::tests

#include "TagsHandlerTest.moc"
