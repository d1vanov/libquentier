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

#include "../SavedSearchesHandler.h"
#include "../ConnectionPool.h"
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

// clazy:excludeall=non-pod-global-static

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
        const
    {
        return m_putSavedSearches;
    }

    [[nodiscard]] const QStringList & expungedSavedSearchLocalIds() const
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
    QThreadPtr m_writerThread;
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
    EXPECT_EQ(savedSearchFuture.resultCount(), 0);
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

    auto listSavedSearchesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListSavedSearchesOrder>{};

    listSavedSearchesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listSavedSearchesFuture =
        savedSearchesHandler->listSavedSearches(listSavedSearchesOptions);

    listSavedSearchesFuture.waitForFinished();
    EXPECT_TRUE(listSavedSearchesFuture.result().isEmpty());
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
        CreateSavedSearchOptions{CreateSavedSearchOption::WithScope})
};

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
        m_notifier,
        &Notifier::savedSearchPut,
        &notifierListener,
        &SavedSearchesHandlerTestNotifierListener::onSavedSearchPut);

    QObject::connect(
        m_notifier,
        &Notifier::savedSearchExpunged,
        &notifierListener,
        &SavedSearchesHandlerTestNotifierListener::onSavedSearchExpunged);

    const auto savedSearch = GetParam();

    auto putSavedSearchFuture = savedSearchesHandler->putSavedSearch(
        savedSearch);

    putSavedSearchFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.putSavedSearches().size(), 1);
    EXPECT_EQ(notifierListener.putSavedSearches()[0], savedSearch);

    auto savedSearchCountFuture = savedSearchesHandler->savedSearchCount();
    savedSearchCountFuture.waitForFinished();
    EXPECT_EQ(savedSearchCountFuture.result(), 1U);

    auto foundSavedSearchByLocalIdFuture =
        savedSearchesHandler->findSavedSearchByLocalId(savedSearch.localId());

    foundSavedSearchByLocalIdFuture.waitForFinished();
    EXPECT_EQ(foundSavedSearchByLocalIdFuture.result(), savedSearch);

    auto listSavedSearchesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListSavedSearchesOrder>{};

    listSavedSearchesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listSavedSearchesFuture = savedSearchesHandler->listSavedSearches(
        listSavedSearchesOptions);

    listSavedSearchesFuture.waitForFinished();
    auto savedSearches = listSavedSearchesFuture.result();
    EXPECT_EQ(savedSearches.size(), 1);
    EXPECT_EQ(savedSearches[0], savedSearch);

    auto expungeSavedSearchFuture =
        savedSearchesHandler->expungeSavedSearchByLocalId(
            savedSearch.localId());

    expungeSavedSearchFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedSavedSearchLocalIds().size(), 1);

    EXPECT_EQ(
        notifierListener.expungedSavedSearchLocalIds()[0],
        savedSearch.localId());

    savedSearchCountFuture = savedSearchesHandler->savedSearchCount();
    savedSearchCountFuture.waitForFinished();
    EXPECT_EQ(savedSearchCountFuture.result(), 0U);

    foundSavedSearchByLocalIdFuture =
        savedSearchesHandler->findSavedSearchByLocalId(savedSearch.localId());

    foundSavedSearchByLocalIdFuture.waitForFinished();
    EXPECT_EQ(foundSavedSearchByLocalIdFuture.resultCount(), 0U);

    listSavedSearchesFuture = savedSearchesHandler->listSavedSearches(
        listSavedSearchesOptions);

    listSavedSearchesFuture.waitForFinished();
    EXPECT_TRUE(listSavedSearchesFuture.result().isEmpty());
}

} // namespace quentier::local_storage::sql::tests

#include "SavedSearchesHandlerTest.moc"
