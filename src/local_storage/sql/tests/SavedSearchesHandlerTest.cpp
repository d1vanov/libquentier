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
#include "../Notifier.h"
#include "../SavedSearchesHandler.h"
#include "../TablesInitializer.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/SavedSearchBuilder.h>

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

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

class SavedSearchesHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit SavedSearchesHandlerTestNotifierListener(
        QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::SavedSearch> & putSavedSearches()
        const noexcept
    {
        return m_putSavedSearches;
    }

    [[nodiscard]] const QStringList & expungedSavedSearchLocalIds()
        const noexcept
    {
        return m_expungedSavedSearchLocalIds;
    }

public Q_SLOTS:
    void onSavedSearchPut(qevercloud::SavedSearch savedSearch) // NOLINT
    {
        m_putSavedSearches << savedSearch;
    }

    void onSavedSearchExpunged(QString savedSearchLocalId) // NOLINT
    {
        m_expungedSavedSearchLocalIds << savedSearchLocalId;
    }

private:
    QList<qevercloud::SavedSearch> m_putSavedSearches;
    QStringList m_expungedSavedSearchLocalIds;
};

namespace {

class SavedSearchesHandlerTest : public testing::Test
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
    Notifier * m_notifier;
};

} // namespace

TEST_F(SavedSearchesHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto savedSearchesHandler =
            std::make_shared<SavedSearchesHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                m_writerThread));
}

TEST_F(SavedSearchesHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto savedSearchesHandler =
            std::make_shared<SavedSearchesHandler>(
                nullptr, QThreadPool::globalInstance(), m_notifier,
                m_writerThread),
        IQuentierException);
}

TEST_F(SavedSearchesHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto savedSearchesHandler =
            std::make_shared<SavedSearchesHandler>(
                m_connectionPool, nullptr, m_notifier, m_writerThread),
        IQuentierException);
}

TEST_F(SavedSearchesHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto savedSearchesHandler =
            std::make_shared<SavedSearchesHandler>(
                m_connectionPool, QThreadPool::globalInstance(), nullptr,
                m_writerThread),
        IQuentierException);
}

TEST_F(SavedSearchesHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto savedSearchesHandler =
            std::make_shared<SavedSearchesHandler>(
                m_connectionPool, QThreadPool::globalInstance(), m_notifier,
                nullptr),
        IQuentierException);
}

TEST_F(
    SavedSearchesHandlerTest,
    ShouldHaveZeroSavedSearchCountWhenThereAreNoSavedSearches)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto savedSearchCountFuture = savedSearchesHandler->savedSearchCount();
    savedSearchCountFuture.waitForFinished();
    EXPECT_EQ(savedSearchCountFuture.result(), 0U);
}

TEST_F(SavedSearchesHandlerTest, ShouldNotFindNonexistentSavedSearchByLocalId)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto savedSearchFuture = savedSearchesHandler->findSavedSearchByLocalId(
        UidGenerator::Generate());

    savedSearchFuture.waitForFinished();
    ASSERT_EQ(savedSearchFuture.resultCount(), 1);
    EXPECT_FALSE(savedSearchFuture.result());
}

TEST_F(SavedSearchesHandlerTest, ShouldNotFindNonexistentSavedSearchByGuid)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto savedSearchFuture =
        savedSearchesHandler->findSavedSearchByGuid(UidGenerator::Generate());

    savedSearchFuture.waitForFinished();
    ASSERT_EQ(savedSearchFuture.resultCount(), 1);
    EXPECT_FALSE(savedSearchFuture.result());
}

TEST_F(SavedSearchesHandlerTest, ShouldNotFindNonexistentSavedSearchByName)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto savedSearchFuture =
        savedSearchesHandler->findSavedSearchByName(QStringLiteral("search1"));

    savedSearchFuture.waitForFinished();
    ASSERT_EQ(savedSearchFuture.resultCount(), 1);
    EXPECT_FALSE(savedSearchFuture.result());
}

TEST_F(
    SavedSearchesHandlerTest,
    IgnoreAttemptToExpungeNonexistentSavedSearchByLocalId)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto expungeSavedSearchFuture =
        savedSearchesHandler->expungeSavedSearchByLocalId(
            UidGenerator::Generate());

    EXPECT_NO_THROW(expungeSavedSearchFuture.waitForFinished());
}

TEST_F(
    SavedSearchesHandlerTest,
    ShouldListNoSavedSearchesWhenThereAreNoSavedSearches)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    const auto listSavedSearchesOptions =
        ILocalStorage::ListSavedSearchesOptions{};

    auto listSavedSearchesFuture =
        savedSearchesHandler->listSavedSearches(listSavedSearchesOptions);

    listSavedSearchesFuture.waitForFinished();
    EXPECT_TRUE(listSavedSearchesFuture.result().isEmpty());
}

TEST_F(
    SavedSearchesHandlerTest,
    ShouldListNoSavedSearchGuidsWhenThereAreNoSavedSearches)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto listSavedSearchGuidsFilters = ILocalStorage::ListGuidsFilters{};
    listSavedSearchGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Include;

    auto listSavedSearchGuidsFuture =
        savedSearchesHandler->listSavedSearchGuids(listSavedSearchGuidsFilters);

    listSavedSearchGuidsFuture.waitForFinished();
    ASSERT_EQ(listSavedSearchGuidsFuture.resultCount(), 1);
    EXPECT_TRUE(listSavedSearchGuidsFuture.result().isEmpty());
}

enum class CreateSavedSearchOption
{
    WithScope = 1 << 0
};

Q_DECLARE_FLAGS(CreateSavedSearchOptions, CreateSavedSearchOption);

[[nodiscard]] qevercloud::SavedSearch createSavedSearch(
    const CreateSavedSearchOptions options = {})
{
    qevercloud::SavedSearch savedSearch;
    savedSearch.setLocallyModified(true);
    savedSearch.setGuid(UidGenerator::Generate());
    savedSearch.setName(QStringLiteral("Saved search"));
    savedSearch.setQuery(QStringLiteral("Query"));
    savedSearch.setFormat(qevercloud::QueryFormat::USER);
    savedSearch.setUpdateSequenceNum(42);

    if (options.testFlag(CreateSavedSearchOption::WithScope)) {
        qevercloud::SavedSearchScope scope;
        scope.setIncludeAccount(true);
        scope.setIncludeBusinessLinkedNotebooks(false);
        scope.setIncludePersonalLinkedNotebooks(true);
        savedSearch.setScope(std::move(scope));
    }

    return savedSearch;
}

class SavedSearchesHandlerSingleSavedSearchTest :
    public SavedSearchesHandlerTest,
    public testing::WithParamInterface<qevercloud::SavedSearch>
{};

const std::array gSavedSearchTestValues{
    createSavedSearch(),
    createSavedSearch(
        CreateSavedSearchOptions{CreateSavedSearchOption::WithScope})};

INSTANTIATE_TEST_SUITE_P(
    SavedSearchesHandlerSingleSavedSearchTestInstance,
    SavedSearchesHandlerSingleSavedSearchTest,
    testing::ValuesIn(gSavedSearchTestValues));

TEST_P(SavedSearchesHandlerSingleSavedSearchTest, HandleSingleSavedSearch)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    SavedSearchesHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::savedSearchPut, &notifierListener,
        &SavedSearchesHandlerTestNotifierListener::onSavedSearchPut);

    QObject::connect(
        m_notifier, &Notifier::savedSearchExpunged, &notifierListener,
        &SavedSearchesHandlerTestNotifierListener::onSavedSearchExpunged);

    const auto savedSearch = GetParam();

    // === Put ===

    auto putSavedSearchFuture =
        savedSearchesHandler->putSavedSearch(savedSearch);

    putSavedSearchFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putSavedSearches().size(), 1);
    EXPECT_EQ(notifierListener.putSavedSearches()[0], savedSearch);

    // === Count ===

    auto savedSearchCountFuture = savedSearchesHandler->savedSearchCount();
    savedSearchCountFuture.waitForFinished();
    EXPECT_EQ(savedSearchCountFuture.result(), 1U);

    // === Find by local id ===

    auto foundSavedSearchByLocalIdFuture =
        savedSearchesHandler->findSavedSearchByLocalId(savedSearch.localId());

    foundSavedSearchByLocalIdFuture.waitForFinished();
    ASSERT_EQ(foundSavedSearchByLocalIdFuture.resultCount(), 1);
    ASSERT_TRUE(foundSavedSearchByLocalIdFuture.result());
    EXPECT_EQ(foundSavedSearchByLocalIdFuture.result(), savedSearch);

    // === Find by guid ===

    auto foundSavedSearchByGuidFuture =
        savedSearchesHandler->findSavedSearchByGuid(savedSearch.guid().value());

    foundSavedSearchByGuidFuture.waitForFinished();
    ASSERT_EQ(foundSavedSearchByGuidFuture.resultCount(), 1);
    ASSERT_TRUE(foundSavedSearchByGuidFuture.result());
    EXPECT_EQ(foundSavedSearchByGuidFuture.result(), savedSearch);

    // === Find by name ===

    auto foundSavedSearchByNameFuture =
        savedSearchesHandler->findSavedSearchByName(savedSearch.name().value());

    foundSavedSearchByNameFuture.waitForFinished();
    ASSERT_EQ(foundSavedSearchByNameFuture.resultCount(), 1);
    ASSERT_TRUE(foundSavedSearchByNameFuture.result());
    EXPECT_EQ(foundSavedSearchByNameFuture.result(), savedSearch);

    // === List saved searches ===

    const auto listSavedSearchesOptions =
        ILocalStorage::ListSavedSearchesOptions{};

    auto listSavedSearchesFuture =
        savedSearchesHandler->listSavedSearches(listSavedSearchesOptions);

    listSavedSearchesFuture.waitForFinished();
    auto savedSearches = listSavedSearchesFuture.result();
    EXPECT_EQ(savedSearches.size(), 1);
    EXPECT_EQ(savedSearches[0], savedSearch);

    // === List saved search guids ===

    // == Including locally modified saved searches ==
    auto listSavedSearchGuidsFilters = ILocalStorage::ListGuidsFilters{};
    listSavedSearchGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Include;

    auto listSavedSearchGuidsFuture =
        savedSearchesHandler->listSavedSearchGuids(listSavedSearchGuidsFilters);

    listSavedSearchGuidsFuture.waitForFinished();
    ASSERT_EQ(listSavedSearchGuidsFuture.resultCount(), 1);

    auto savedSearchGuids = listSavedSearchGuidsFuture.result();
    ASSERT_EQ(savedSearchGuids.size(), 1);
    EXPECT_EQ(*savedSearchGuids.constBegin(), savedSearch.guid().value());

    // == Excluding locally modified saved searches ==
    listSavedSearchGuidsFilters.m_locallyModifiedFilter =
        ILocalStorage::ListObjectsFilter::Exclude;

    listSavedSearchGuidsFuture =
        savedSearchesHandler->listSavedSearchGuids(listSavedSearchGuidsFilters);

    listSavedSearchGuidsFuture.waitForFinished();
    ASSERT_EQ(listSavedSearchGuidsFuture.resultCount(), 1);

    savedSearchGuids = listSavedSearchGuidsFuture.result();
    EXPECT_TRUE(savedSearchGuids.isEmpty());

    // === Expunge saved search by local id ===

    auto expungeSavedSearchByLocalIdFuture =
        savedSearchesHandler->expungeSavedSearchByLocalId(
            savedSearch.localId());

    expungeSavedSearchByLocalIdFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedSavedSearchLocalIds().size(), 1);

    EXPECT_EQ(
        notifierListener.expungedSavedSearchLocalIds()[0],
        savedSearch.localId());

    auto checkSavedSearchDeleted = [&] {
        savedSearchCountFuture = savedSearchesHandler->savedSearchCount();
        savedSearchCountFuture.waitForFinished();
        EXPECT_EQ(savedSearchCountFuture.result(), 0U);

        foundSavedSearchByLocalIdFuture =
            savedSearchesHandler->findSavedSearchByLocalId(
                savedSearch.localId());

        foundSavedSearchByLocalIdFuture.waitForFinished();
        ASSERT_EQ(foundSavedSearchByLocalIdFuture.resultCount(), 1);
        EXPECT_FALSE(foundSavedSearchByLocalIdFuture.result());

        foundSavedSearchByGuidFuture =
            savedSearchesHandler->findSavedSearchByGuid(
                savedSearch.guid().value());

        foundSavedSearchByGuidFuture.waitForFinished();
        ASSERT_EQ(foundSavedSearchByGuidFuture.resultCount(), 1);
        EXPECT_FALSE(foundSavedSearchByGuidFuture.result());

        foundSavedSearchByNameFuture =
            savedSearchesHandler->findSavedSearchByName(
                savedSearch.name().value());

        foundSavedSearchByNameFuture.waitForFinished();
        ASSERT_EQ(foundSavedSearchByNameFuture.resultCount(), 1);
        EXPECT_FALSE(foundSavedSearchByNameFuture.result());

        listSavedSearchesFuture =
            savedSearchesHandler->listSavedSearches(listSavedSearchesOptions);

        listSavedSearchesFuture.waitForFinished();
        EXPECT_TRUE(listSavedSearchesFuture.result().isEmpty());

        listSavedSearchGuidsFuture = savedSearchesHandler->listSavedSearchGuids(
            ILocalStorage::ListGuidsFilters{});

        listSavedSearchGuidsFuture.waitForFinished();
        ASSERT_EQ(listSavedSearchGuidsFuture.resultCount(), 1);

        savedSearchGuids = listSavedSearchGuidsFuture.result();
        EXPECT_TRUE(savedSearchGuids.isEmpty());
    };

    checkSavedSearchDeleted();

    // === Put saved search ===

    putSavedSearchFuture = savedSearchesHandler->putSavedSearch(savedSearch);
    putSavedSearchFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putSavedSearches().size(), 2);
    EXPECT_EQ(notifierListener.putSavedSearches()[1], savedSearch);

    // === Expunge saved search by guid ===

    auto expungeSavedSearchByGuidFuture =
        savedSearchesHandler->expungeSavedSearchByGuid(
            savedSearch.guid().value());

    expungeSavedSearchByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedSavedSearchLocalIds().size(), 2);

    EXPECT_EQ(
        notifierListener.expungedSavedSearchLocalIds()[1],
        savedSearch.localId());

    checkSavedSearchDeleted();
}

TEST_F(SavedSearchesHandlerTest, HandleMultipleSavedSearches)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    SavedSearchesHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::savedSearchPut, &notifierListener,
        &SavedSearchesHandlerTestNotifierListener::onSavedSearchPut);

    QObject::connect(
        m_notifier, &Notifier::savedSearchExpunged, &notifierListener,
        &SavedSearchesHandlerTestNotifierListener::onSavedSearchExpunged);

    auto savedSearches = gSavedSearchTestValues;
    int savedSearchCounter = 2;
    for (auto it = std::next(savedSearches.begin()); // NOLINT
         it != savedSearches.end(); ++it)
    {
        auto & savedSearch = *it;
        savedSearch.setLocalId(UidGenerator::Generate());
        savedSearch.setGuid(UidGenerator::Generate());

        savedSearch.setName(
            savedSearches.begin()->name().value() + QStringLiteral(" #") +
            QString::number(savedSearchCounter));

        savedSearch.setUpdateSequenceNum(savedSearchCounter);
        ++savedSearchCounter;
    }

    QFutureSynchronizer<void> putSavedSearchesSynchronizer;
    for (auto savedSearch: savedSearches) {
        auto putSavedSearchFuture =
            savedSearchesHandler->putSavedSearch(std::move(savedSearch));

        putSavedSearchesSynchronizer.addFuture(putSavedSearchFuture);
    }

    EXPECT_NO_THROW(putSavedSearchesSynchronizer.waitForFinished());

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putSavedSearches().size(), savedSearches.size());

    auto savedSearchCountFuture = savedSearchesHandler->savedSearchCount();
    savedSearchCountFuture.waitForFinished();
    EXPECT_EQ(savedSearchCountFuture.result(), savedSearches.size());

    for (const auto & savedSearch: qAsConst(savedSearches)) {
        auto foundByLocalIdSavedSearchFuture =
            savedSearchesHandler->findSavedSearchByLocalId(
                savedSearch.localId());
        foundByLocalIdSavedSearchFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdSavedSearchFuture.resultCount(), 1);
        ASSERT_TRUE(foundByLocalIdSavedSearchFuture.result());
        EXPECT_EQ(foundByLocalIdSavedSearchFuture.result(), savedSearch);

        auto foundByGuidSavedSearchFuture =
            savedSearchesHandler->findSavedSearchByGuid(
                savedSearch.guid().value());
        foundByGuidSavedSearchFuture.waitForFinished();
        ASSERT_EQ(foundByGuidSavedSearchFuture.resultCount(), 1);
        ASSERT_TRUE(foundByGuidSavedSearchFuture.result());
        EXPECT_EQ(foundByGuidSavedSearchFuture.result(), savedSearch);

        auto foundByNameSavedSearchFuture =
            savedSearchesHandler->findSavedSearchByName(
                savedSearch.name().value());
        foundByNameSavedSearchFuture.waitForFinished();
        ASSERT_EQ(foundByNameSavedSearchFuture.resultCount(), 1);
        ASSERT_TRUE(foundByNameSavedSearchFuture.result());
        EXPECT_EQ(foundByNameSavedSearchFuture.result(), savedSearch);
    }

    for (const auto & savedSearch: qAsConst(savedSearches)) {
        auto expungeSavedSearchByLocalIdFuture =
            savedSearchesHandler->expungeSavedSearchByLocalId(
                savedSearch.localId());
        expungeSavedSearchByLocalIdFuture.waitForFinished();
    }

    QCoreApplication::processEvents();

    EXPECT_EQ(
        notifierListener.expungedSavedSearchLocalIds().size(),
        savedSearches.size());

    savedSearchCountFuture = savedSearchesHandler->savedSearchCount();
    savedSearchCountFuture.waitForFinished();
    EXPECT_EQ(savedSearchCountFuture.result(), 0U);

    for (const auto & savedSearch: qAsConst(savedSearches)) {
        auto foundByLocalIdSavedSearchFuture =
            savedSearchesHandler->findSavedSearchByLocalId(
                savedSearch.localId());
        foundByLocalIdSavedSearchFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdSavedSearchFuture.resultCount(), 1);
        EXPECT_FALSE(foundByLocalIdSavedSearchFuture.result());

        auto foundByGuidSavedSearchFuture =
            savedSearchesHandler->findSavedSearchByGuid(
                savedSearch.guid().value());
        foundByGuidSavedSearchFuture.waitForFinished();
        ASSERT_EQ(foundByGuidSavedSearchFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidSavedSearchFuture.result());

        auto foundByNameSavedSearchFuture =
            savedSearchesHandler->findSavedSearchByName(
                savedSearch.name().value());
        foundByNameSavedSearchFuture.waitForFinished();
        ASSERT_EQ(foundByNameSavedSearchFuture.resultCount(), 1);
        EXPECT_FALSE(foundByNameSavedSearchFuture.result());
    }
}

// The test checks that SavedSearchesHandler doesn't confuse saved searches
// which names are very similar and differ only by the presence of diacritics
// in one of names
TEST_F(SavedSearchesHandlerTest, FindSavedSearchByNameWithDiacritics)
{
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    qevercloud::SavedSearch search1;
    search1.setGuid(UidGenerator::Generate());
    search1.setUpdateSequenceNum(1);
    search1.setName(QStringLiteral("search"));

    qevercloud::SavedSearch search2;
    search2.setGuid(UidGenerator::Generate());
    search2.setUpdateSequenceNum(2);
    search2.setName(QStringLiteral("séarch"));

    auto putSavedSearchFuture = savedSearchesHandler->putSavedSearch(search1);
    putSavedSearchFuture.waitForFinished();

    putSavedSearchFuture = savedSearchesHandler->putSavedSearch(search2);
    putSavedSearchFuture.waitForFinished();

    auto foundSavedSearchByNameFuture =
        savedSearchesHandler->findSavedSearchByName(search1.name().value());

    foundSavedSearchByNameFuture.waitForFinished();
    ASSERT_EQ(foundSavedSearchByNameFuture.resultCount(), 1);
    ASSERT_TRUE(foundSavedSearchByNameFuture.result());
    EXPECT_EQ(foundSavedSearchByNameFuture.result(), search1);

    foundSavedSearchByNameFuture =
        savedSearchesHandler->findSavedSearchByName(search2.name().value());

    foundSavedSearchByNameFuture.waitForFinished();
    ASSERT_EQ(foundSavedSearchByNameFuture.resultCount(), 1);
    ASSERT_TRUE(foundSavedSearchByNameFuture.result());
    EXPECT_EQ(foundSavedSearchByNameFuture.result(), search2);
}

const QList<qevercloud::SavedSearch> gSavedSearchesForListGuidsTest =
    QList<qevercloud::SavedSearch>{}
    << qevercloud::SavedSearchBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Saved search 1"))
           .setLocallyModified(false)
           .setLocallyFavorited(false)
           .build()
    << qevercloud::SavedSearchBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Saved search 2"))
           .setLocallyModified(true)
           .setLocallyFavorited(false)
           .build()
    << qevercloud::SavedSearchBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Saved search 3"))
           .setLocallyModified(false)
           .setLocallyFavorited(true)
           .build()
    << qevercloud::SavedSearchBuilder{}
           .setLocalId(UidGenerator::Generate())
           .setGuid(UidGenerator::Generate())
           .setName(QStringLiteral("Saved search 4"))
           .setLocallyModified(true)
           .setLocallyFavorited(true)
           .build();

struct ListSavedSearchGuidsTestData
{
    // Input data
    ILocalStorage::ListGuidsFilters filters;
    // Expected indexes of notebook guids
    QSet<int> expectedIndexes;
};

const QList<ListSavedSearchGuidsTestData> gListSavedSearchGuidsTestData =
    QList<ListSavedSearchGuidsTestData>{}
    << ListSavedSearchGuidsTestData{
        {}, // filters
        QSet<int>{} << 0 << 1 << 2 << 3, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        QSet<int>{} << 1 << 3, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            std::nullopt, // locally favorited
        }, // filters
        QSet<int>{} << 0 << 2, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        QSet<int>{} << 2 << 3, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            std::nullopt, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        QSet<int>{} << 0 << 1, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        QSet<int>{} << 3, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        QSet<int>{} << 0, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Include, // locally modified
            ILocalStorage::ListObjectsFilter::Exclude, // locally favorited
        }, // filters
        QSet<int>{} << 1, // expected indexes
    }
    << ListSavedSearchGuidsTestData{
        ILocalStorage::ListGuidsFilters{
            ILocalStorage::ListObjectsFilter::Exclude, // locally modified
            ILocalStorage::ListObjectsFilter::Include, // locally favorited
        }, // filters
        QSet<int>{} << 2, // expected indexes
    };

class SavedSearchesHandlerListGuidsTest :
    public SavedSearchesHandlerTest,
    public testing::WithParamInterface<ListSavedSearchGuidsTestData>
{};

INSTANTIATE_TEST_SUITE_P(
    SavedSearchesHandlerListGuidsTestInstance,
    SavedSearchesHandlerListGuidsTest,
    testing::ValuesIn(gListSavedSearchGuidsTestData));

TEST_P(SavedSearchesHandlerListGuidsTest, ListSavedSearchGuids)
{
    // Set up saved searches
    const auto savedSearchesHandler = std::make_shared<SavedSearchesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    for (const auto & savedSearch: qAsConst(gSavedSearchesForListGuidsTest)) {
        auto putSavedSearchFuture =
            savedSearchesHandler->putSavedSearch(savedSearch);

        putSavedSearchFuture.waitForFinished();
    }

    // Test the results of tag guids listing
    const auto testData = GetParam();
    auto listSavedSearchGuidsFuture =
        savedSearchesHandler->listSavedSearchGuids(testData.filters);

    listSavedSearchGuidsFuture.waitForFinished();
    ASSERT_EQ(listSavedSearchGuidsFuture.resultCount(), 1);

    const QSet<qevercloud::Guid> expectedGuids = [&] {
        QSet<qevercloud::Guid> result;
        result.reserve(testData.expectedIndexes.size());
        for (const int index: testData.expectedIndexes) {
            result.insert(gSavedSearchesForListGuidsTest[index].guid().value());
        }
        return result;
    }();

    EXPECT_EQ(listSavedSearchGuidsFuture.result().size(), expectedGuids.size());
    EXPECT_EQ(listSavedSearchGuidsFuture.result(), expectedGuids);
}

} // namespace quentier::local_storage::sql::tests

#include "SavedSearchesHandlerTest.moc"
