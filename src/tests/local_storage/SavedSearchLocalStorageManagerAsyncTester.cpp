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

#include "SavedSearchLocalStorageManagerAsyncTester.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

#include <QThread>

namespace quentier::test {

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
    const QString username =
        QStringLiteral("SavedSearchLocalStorageManagerAsyncTester");

    const qint32 userId = 0;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    const Account account{username, Account::Type::Evernote, userId};

    const LocalStorageManager::StartupOptions startupOptions(
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
    m_initialSavedSearch.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000046"));

    m_initialSavedSearch.setUpdateSequenceNum(1);
    m_initialSavedSearch.setName(QStringLiteral("Fake saved search name"));
    m_initialSavedSearch.setQuery(QStringLiteral("Fake saved search query"));
    m_initialSavedSearch.setFormat(qevercloud::QueryFormat::USER);
    m_initialSavedSearch.setScope(qevercloud::SavedSearchScope{});
    m_initialSavedSearch.mutableScope()->setIncludeAccount(true);

    m_initialSavedSearch.mutableScope()->setIncludeBusinessLinkedNotebooks(
        false);

    m_initialSavedSearch.mutableScope()->setIncludePersonalLinkedNotebooks(
        true);

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

        qevercloud::SavedSearch extraSavedSearch;

        extraSavedSearch.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000001"));

        extraSavedSearch.setUpdateSequenceNum(1);
        extraSavedSearch.setName(QStringLiteral("Extra SavedSearch"));

        extraSavedSearch.setQuery(
            QStringLiteral("Fake extra saved search query"));

        extraSavedSearch.setFormat(qevercloud::QueryFormat::USER);
        extraSavedSearch.setScope(qevercloud::SavedSearchScope{});
        extraSavedSearch.mutableScope()->setIncludeAccount(true);

        extraSavedSearch.mutableScope()->setIncludeBusinessLinkedNotebooks(
            true);

        extraSavedSearch.mutableScope()->setIncludePersonalLinkedNotebooks(
            true);

        m_state = STATE_SENT_ADD_EXTRA_SAVED_SEARCH_ONE_REQUEST;
        Q_EMIT addSavedSearchRequest(extraSavedSearch, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void SavedSearchLocalStorageManagerAsyncTester::onGetSavedSearchCountFailed(
    ErrorString errorDescription, QUuid requestId) // NOLINT
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onAddSavedSearchCompleted(
    qevercloud::SavedSearch search, QUuid requestId) // NOLINT
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

        m_foundSavedSearch = qevercloud::SavedSearch();
        m_foundSavedSearch.setLocalId(search.localId());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        Q_EMIT findSavedSearchRequest(m_foundSavedSearch, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_SAVED_SEARCH_ONE_REQUEST) {
        m_initialSavedSearches << search;

        qevercloud::SavedSearch extraSavedSearch;

        extraSavedSearch.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000002"));

        extraSavedSearch.setUpdateSequenceNum(2);
        extraSavedSearch.setName(QStringLiteral("Extra SavedSearch two"));

        extraSavedSearch.setQuery(
            QStringLiteral("Fake extra saved search query two"));

        extraSavedSearch.setFormat(qevercloud::QueryFormat::USER);

        extraSavedSearch.setScope(qevercloud::SavedSearchScope{});
        extraSavedSearch.mutableScope()->setIncludeAccount(true);

        extraSavedSearch.mutableScope()->setIncludeBusinessLinkedNotebooks(
            false);

        extraSavedSearch.mutableScope()->setIncludePersonalLinkedNotebooks(
            true);

        m_state = STATE_SENT_ADD_EXTRA_SAVED_SEARCH_TWO_REQUEST;
        Q_EMIT addSavedSearchRequest(extraSavedSearch, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_SAVED_SEARCH_TWO_REQUEST) {
        m_initialSavedSearches << search;

        m_state = STATE_SENT_LIST_SEARCHES_REQUEST;
        const std::size_t limit = 0, offset = 0;

        const LocalStorageManager::ListSavedSearchesOrder order =
            LocalStorageManager::ListSavedSearchesOrder::NoOrder;

        const LocalStorageManager::OrderDirection orderDirection =
            LocalStorageManager::OrderDirection::Ascending;

        Q_EMIT listAllSavedSearchesRequest(
            limit, offset, order, orderDirection, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void SavedSearchLocalStorageManagerAsyncTester::onAddSavedSearchFailed(
    qevercloud::SavedSearch search, ErrorString errorDescription, // NOLINT
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onUpdateSavedSearchCompleted(
    qevercloud::SavedSearch search, QUuid requestId) // NOLINT
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

        m_foundSavedSearch = qevercloud::SavedSearch();
        m_foundSavedSearch.setLocalId(search.localId());

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        Q_EMIT findSavedSearchRequest(m_foundSavedSearch, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void SavedSearchLocalStorageManagerAsyncTester::onUpdateSavedSearchFailed(
    qevercloud::SavedSearch search, ErrorString errorDescription, // NOLINT
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void SavedSearchLocalStorageManagerAsyncTester::onFindSavedSearchCompleted(
    qevercloud::SavedSearch search, QUuid requestId) // NOLINT
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
        qevercloud::SavedSearch searchToFindByName;
        searchToFindByName.setLocalId(QString{});
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

        m_modifiedSavedSearch.setUpdateSequenceNum(
            m_initialSavedSearch.updateSequenceNum().value() + 1);

        m_modifiedSavedSearch.setName(
            m_initialSavedSearch.name().value() + QStringLiteral("_modified"));

        m_modifiedSavedSearch.setQuery(
            m_initialSavedSearch.query().value() + QStringLiteral("_modified"));

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
    qevercloud::SavedSearch search, ErrorString errorDescription, // NOLINT
    QUuid requestId)
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
    std::size_t limit, std::size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QList<qevercloud::SavedSearch> searches, QUuid requestId) // NOLINT
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
    std::size_t limit, std::size_t offset,
    LocalStorageManager::ListSavedSearchesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    ErrorString errorDescription, QUuid requestId) // NOLINT
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
    qevercloud::SavedSearch search, QUuid requestId) // NOLINT
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

    m_foundSavedSearch = qevercloud::SavedSearch();
    m_foundSavedSearch.setLocalId(search.localId());

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    Q_EMIT findSavedSearchRequest(m_foundSavedSearch, QUuid::createUuid());
}

void SavedSearchLocalStorageManagerAsyncTester::onExpungeSavedSearchFailed(
    qevercloud::SavedSearch search, ErrorString errorDescription, // NOLINT
    QUuid requestId)
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

} // namespace quentier::test
