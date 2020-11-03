/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "SavedSearchLocalStorageManagerAsyncTester.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#include <QThread>

namespace quentier {
namespace test {

SavedSearchLocalStorageManagerAsyncTester::
    SavedSearchLocalStorageManagerAsyncTester(QObject * parent) :
    QObject(parent)
{}

SavedSearchLocalStorageManagerAsyncTester::
    ~SavedSearchLocalStorageManagerAsyncTester()
{
    clear();
}

void SavedSearchLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username =
        QStringLiteral("SavedSearchLocalStorageManagerAsyncTester");

    qint32 userId = 0;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(QStringLiteral(
        "SavedSearchLocalStorageManagerAsyncTester-local-storage-thread"));

    m_pLocalStorageManagerThread->start();
}

void SavedSearchLocalStorageManagerAsyncTester::initialize()
{
    m_initialSavedSearch = SavedSearch();

    m_initialSavedSearch.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000046"));

    m_initialSavedSearch.setUpdateSequenceNumber(1);
    m_initialSavedSearch.setName(QStringLiteral("Fake saved search name"));
    m_initialSavedSearch.setQuery(QStringLiteral("Fake saved search query"));
    m_initialSavedSearch.setQueryFormat(1);
    m_initialSavedSearch.setIncludeAccount(true);
    m_initialSavedSearch.setIncludeBusinessLinkedNotebooks(false);
    m_initialSavedSearch.setIncludePersonalLinkedNotebooks(true);

    ErrorString errorDescription;
    if (!m_initialSavedSearch.checkParameters(errorDescription)) {
        QNWARNING(
            "tests:local_storage",
            "Found invalid SavedSearch: " << m_initialSavedSearch
                                          << ", error: " << errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_ADD_REQUEST;
    Q_EMIT addSavedSearchRequest(m_initialSavedSearch, QUuid::createUuid());
}

void SavedSearchLocalStorageManagerAsyncTester::onGetSavedSearchCountCompleted(
    int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else {                                                                     \
        errorDescription.setBase(                                              \
            "Internal error in SavedSearchLocalStorageManagerAsyncTester: "    \
            "found wrong state");                                              \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
        return;                                                                \
    }

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST) {
        if (count != 1) {
            errorDescription.setBase(
                "GetSavedSearchCount returned result "
                "different from the expected one (1)");

            errorDescription.details() = QString::number(count);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_EXPUNGE_REQUEST;

        Q_EMIT expungeSavedSearchRequest(
            m_modifiedSavedSearch, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST) {
        if (count != 0) {
            errorDescription.setBase(
                "GetSavedSearchCount returned result "
                "different from the expected one (0)");

            errorDescription.details() = QString::number(count);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        SavedSearch extraSavedSearch;

        extraSavedSearch.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000001"));

        extraSavedSearch.setUpdateSequenceNumber(1);
        extraSavedSearch.setName(QStringLiteral("Extra SavedSearch"));

        extraSavedSearch.setQuery(
            QStringLiteral("Fake extra saved search query"));

        extraSavedSearch.setQueryFormat(1);
        extraSavedSearch.setIncludeAccount(true);
        extraSavedSearch.setIncludeBusinessLinkedNotebooks(true);
        extraSavedSearch.setIncludePersonalLinkedNotebooks(true);

        m_state = STATE_SENT_ADD_EXTRA_SAVED_SEARCH_ONE_REQUEST;
        Q_EMIT addSavedSearchRequest(extraSavedSearch, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void SavedSearchLocalStorageManagerAsyncTester::onGetSavedSearchCountFailed(
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onAddSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_REQUEST) {
        if (m_initialSavedSearch != search) {
            errorDescription.setBase(
                "Internal error in SavedSearchLocalStorageManagerAsyncTester: "
                "search in onAddSavedSearchCompleted slot "
                "doesn't match the original SavedSearch");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundSavedSearch = SavedSearch();
        m_foundSavedSearch.setLocalUid(search.localUid());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        Q_EMIT findSavedSearchRequest(m_foundSavedSearch, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_SAVED_SEARCH_ONE_REQUEST) {
        m_initialSavedSearches << search;

        SavedSearch extraSavedSearch;

        extraSavedSearch.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000002"));

        extraSavedSearch.setUpdateSequenceNumber(2);
        extraSavedSearch.setName(QStringLiteral("Extra SavedSearch two"));

        extraSavedSearch.setQuery(
            QStringLiteral("Fake extra saved search query two"));

        extraSavedSearch.setQueryFormat(1);
        extraSavedSearch.setIncludeAccount(true);
        extraSavedSearch.setIncludeBusinessLinkedNotebooks(false);
        extraSavedSearch.setIncludePersonalLinkedNotebooks(true);

        m_state = STATE_SENT_ADD_EXTRA_SAVED_SEARCH_TWO_REQUEST;
        Q_EMIT addSavedSearchRequest(extraSavedSearch, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_SAVED_SEARCH_TWO_REQUEST) {
        m_initialSavedSearches << search;

        m_state = STATE_SENT_LIST_SEARCHES_REQUEST;
        size_t limit = 0, offset = 0;

        LocalStorageManager::ListSavedSearchesOrder order =
            LocalStorageManager::ListSavedSearchesOrder::NoOrder;

        LocalStorageManager::OrderDirection orderDirection =
            LocalStorageManager::OrderDirection::Ascending;

        Q_EMIT listAllSavedSearchesRequest(
            limit, offset, order, orderDirection, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void SavedSearchLocalStorageManagerAsyncTester::onAddSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onUpdateSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST) {
        if (m_modifiedSavedSearch != search) {
            errorDescription.setBase(
                "Internal error in SavedSearchLocalStorageManagerAsyncTester: "
                "search in onUpdateSavedSearchCompleted slot doesn't match "
                "the original modified SavedSearch");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundSavedSearch = SavedSearch();
        m_foundSavedSearch.setLocalUid(search.localUid());

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        Q_EMIT findSavedSearchRequest(m_foundSavedSearch, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void SavedSearchLocalStorageManagerAsyncTester::onUpdateSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onFindSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST) {
        if (search != m_initialSavedSearch) {
            errorDescription.setBase(
                "Added and found saved searches in "
                "the local storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": SavedSearch added to the local storage: "
                    << m_initialSavedSearch
                    << "\nSavedSearch found in the local storage: " << search);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Attempt to find saved search by name now
        SavedSearch searchToFindByName;
        searchToFindByName.unsetLocalUid();
        searchToFindByName.setName(search.name());

        m_state = STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST;
        Q_EMIT findSavedSearchRequest(searchToFindByName, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_BY_NAME_AFTER_ADD_REQUEST) {
        if (search != m_initialSavedSearch) {
            errorDescription.setBase(
                "Added and found by name saved searches "
                "in the local storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": SavedSearch added to the local storage: "
                    << m_initialSavedSearch
                    << "\nSavedSearch found by name in the local storage: "
                    << search);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found search is good, updating it now
        m_modifiedSavedSearch = m_initialSavedSearch;

        m_modifiedSavedSearch.setUpdateSequenceNumber(
            m_initialSavedSearch.updateSequenceNumber() + 1);

        m_modifiedSavedSearch.setName(
            m_initialSavedSearch.name() + QStringLiteral("_modified"));

        m_modifiedSavedSearch.setQuery(
            m_initialSavedSearch.query() + QStringLiteral("_modified"));

        m_state = STATE_SENT_UPDATE_REQUEST;

        Q_EMIT updateSavedSearchRequest(
            m_modifiedSavedSearch, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST) {
        if (search != m_modifiedSavedSearch) {
            errorDescription.setBase(
                "Updated and found saved searches "
                "in the local storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": SavedSearch updated in the local storage: "
                    << m_modifiedSavedSearch
                    << "\nSavedSearch found in the local storage: " << search);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getSavedSearchCountRequest(QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        errorDescription.setBase(
            "Error: found saved search which should have "
            "been expunged from the local storage");

        QNWARNING(
            "tests:local_storage",
            errorDescription
                << ": SavedSearch expunged from the local storage: "
                << m_modifiedSavedSearch
                << "\nSavedSearch found in the local storage: " << search);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void SavedSearchLocalStorageManagerAsyncTester::onFindSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getSavedSearchCountRequest(QUuid::createUuid());
        return;
    }

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onListAllSavedSearchesCompleted(
    size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QList<SavedSearch> searches, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(requestId)

    int numInitialSearches = m_initialSavedSearches.size();
    int numFoundSearches = searches.size();

    ErrorString errorDescription;

    if (numInitialSearches != numFoundSearches) {
        errorDescription.setBase(
            "Number of found saved searches does not correspond to the number "
            "of original added saved searches");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    for (const auto & search: qAsConst(m_initialSavedSearches)) {
        if (!searches.contains(search)) {
            errorDescription.setBase(
                "One of initial saved searches was not "
                "found within the found saved searches");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
    }

    Q_EMIT success();
}

void SavedSearchLocalStorageManagerAsyncTester::onListAllSavedSearchedFailed(
    size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onExpungeSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    ErrorString errorDescription;

    if (m_modifiedSavedSearch != search) {
        errorDescription.setBase(
            "Internal error in SavedSearchLocalStorageManagerAsyncTester: "
            "search in onExpungeSavedSearchCompleted slot doesn't match "
            "the original expunged SavedSearch");

        QNWARNING(
            "tests:local_storage",
            errorDescription << ", requestId = " << requestId);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_foundSavedSearch = SavedSearch();
    m_foundSavedSearch.setLocalUid(search.localUid());

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    Q_EMIT findSavedSearchRequest(m_foundSavedSearch, QUuid::createUuid());
}

void SavedSearchLocalStorageManagerAsyncTester::onExpungeSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &QThread::deleteLater);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &SavedSearchLocalStorageManagerAsyncTester::initialize);

    // Request --> slot connections
    QObject::connect(
        this,
        &SavedSearchLocalStorageManagerAsyncTester::getSavedSearchCountRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onGetSavedSearchCountRequest);

    QObject::connect(
        this, &SavedSearchLocalStorageManagerAsyncTester::addSavedSearchRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddSavedSearchRequest);

    QObject::connect(
        this,
        &SavedSearchLocalStorageManagerAsyncTester::updateSavedSearchRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateSavedSearchRequest);

    QObject::connect(
        this,
        &SavedSearchLocalStorageManagerAsyncTester::findSavedSearchRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onFindSavedSearchRequest);

    QObject::connect(
        this,
        &SavedSearchLocalStorageManagerAsyncTester::listAllSavedSearchesRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onListAllSavedSearchesRequest);

    QObject::connect(
        this,
        &SavedSearchLocalStorageManagerAsyncTester::expungeSavedSearchRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeSavedSearchRequest);

    // Slot <-- result connections
    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getSavedSearchCountComplete, this,
        &SavedSearchLocalStorageManagerAsyncTester::
            onGetSavedSearchCountCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getSavedSearchCountFailed, this,
        &SavedSearchLocalStorageManagerAsyncTester::
            onGetSavedSearchCountFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchComplete, this,
        &SavedSearchLocalStorageManagerAsyncTester::onAddSavedSearchCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchFailed, this,
        &SavedSearchLocalStorageManagerAsyncTester::onAddSavedSearchFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &SavedSearchLocalStorageManagerAsyncTester::
            onUpdateSavedSearchCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchFailed, this,
        &SavedSearchLocalStorageManagerAsyncTester::onUpdateSavedSearchFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchComplete, this,
        &SavedSearchLocalStorageManagerAsyncTester::onFindSavedSearchCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findSavedSearchFailed, this,
        &SavedSearchLocalStorageManagerAsyncTester::onFindSavedSearchFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllSavedSearchesComplete, this,
        &SavedSearchLocalStorageManagerAsyncTester::
            onListAllSavedSearchesCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listAllSavedSearchesFailed, this,
        &SavedSearchLocalStorageManagerAsyncTester::
            onListAllSavedSearchedFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchComplete, this,
        &SavedSearchLocalStorageManagerAsyncTester::
            onExpungeSavedSearchCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchFailed, this,
        &SavedSearchLocalStorageManagerAsyncTester::onExpungeSavedSearchFailed);
}

void SavedSearchLocalStorageManagerAsyncTester::clear()
{
    if (m_pLocalStorageManagerThread) {
        m_pLocalStorageManagerThread->quit();
        m_pLocalStorageManagerThread->wait();
        m_pLocalStorageManagerThread->deleteLater();
        m_pLocalStorageManagerThread = nullptr;
    }

    if (m_pLocalStorageManagerAsync) {
        m_pLocalStorageManagerAsync->deleteLater();
        m_pLocalStorageManagerThread = nullptr;
    }

    m_state = STATE_UNINITIALIZED;
}

#undef HANDLE_WRONG_STATE

} // namespace test
} // namespace quentier
