/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_SAVED_SEARCH_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_SAVED_SEARCH_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/SavedSearch.h>

namespace quentier {

class LocalStorageManagerAsync;

namespace test {

class SavedSearchLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit SavedSearchLocalStorageManagerAsyncTester(
        QObject * parent = nullptr);

    ~SavedSearchLocalStorageManagerAsyncTester() override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void getSavedSearchCountRequest(QUuid requestId);
    void addSavedSearchRequest(qevercloud::SavedSearch search, QUuid requestId);

    void updateSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

    void findSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

    void listAllSavedSearchesRequest(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void expungeSavedSearchRequest(
        qevercloud::SavedSearch search, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onGetSavedSearchCountCompleted(int count, QUuid requestId);

    void onGetSavedSearchCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void onAddSavedSearchCompleted(
        qevercloud::SavedSearch search, QUuid requestId);

    void onAddSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateSavedSearchCompleted(
        qevercloud::SavedSearch search, QUuid requestId);

    void onUpdateSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void onFindSavedSearchCompleted(
        qevercloud::SavedSearch search, QUuid requestId);

    void onFindSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

    void onListAllSavedSearchesCompleted(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::SavedSearch> searches, QUuid requestId);

    void onListAllSavedSearchedFailed(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListSavedSearchesOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeSavedSearchCompleted(
        qevercloud::SavedSearch search, QUuid requestId);

    void onExpungeSavedSearchFailed(
        qevercloud::SavedSearch search, ErrorString errorDescription,
        QUuid requestId);

private:
    void createConnections();
    void clear();

    enum State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_ADD_EXTRA_SAVED_SEARCH_ONE_REQUEST,
        STATE_SENT_ADD_EXTRA_SAVED_SEARCH_TWO_REQUEST,
        STATE_SENT_LIST_SEARCHES_REQUEST
    };

    State m_state = STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    qevercloud::SavedSearch m_initialSavedSearch;
    qevercloud::SavedSearch m_foundSavedSearch;
    qevercloud::SavedSearch m_modifiedSavedSearch;
    QList<qevercloud::SavedSearch> m_initialSavedSearches;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SAVED_SEARCH_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
