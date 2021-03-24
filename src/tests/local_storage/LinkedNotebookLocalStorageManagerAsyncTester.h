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

#ifndef LIB_QUENTIER_TESTS_LINKED_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_LINKED_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/LinkedNotebook.h>

class QDebug;

namespace quentier {

class LocalStorageManagerAsync;

namespace test {

class LinkedNotebookLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit LinkedNotebookLocalStorageManagerAsyncTester(
        QObject * parent = nullptr);

    ~LinkedNotebookLocalStorageManagerAsyncTester() noexcept override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void getLinkedNotebookCountRequest(QUuid requestId);

    void addLinkedNotebookRequest(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

    void updateLinkedNotebookRequest(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

    void findLinkedNotebookRequest(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

    void listAllLinkedNotebooksRequest(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void expungeLinkedNotebookRequest(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onGetLinkedNotebookCountCompleted(int count, QUuid requestId);

    void onGetLinkedNotebookCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void onAddLinkedNotebookCompleted(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

    void onAddLinkedNotebookFailed(
        qevercloud::LinkedNotebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateLinkedNotebookCompleted(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

    void onUpdateLinkedNotebookFailed(
        qevercloud::LinkedNotebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onFindLinkedNotebookCompleted(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

    void onFindLinkedNotebookFailed(
        qevercloud::LinkedNotebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onListAllLinkedNotebooksCompleted(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<qevercloud::LinkedNotebook> linkedNotebooks, QUuid requestId);

    void onListAllLinkedNotebooksFailed(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeLinkedNotebookCompleted(
        qevercloud::LinkedNotebook notebook, QUuid requestId);

    void onExpungeLinkedNotebookFailed(
        qevercloud::LinkedNotebook notebook, ErrorString errorDescription,
        QUuid requestId);

private:
    void createConnections();
    void clear() noexcept;

    enum class State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_ONE_REQUEST,
        STATE_SENT_ADD_EXTRA_LINKED_NOTEBOOK_TWO_REQUEST,
        STATE_SENT_LIST_LINKED_NOTEBOOKS_REQUEST
    };

    friend QDebug & operator<<(QDebug & dbg, State state);

private:
    State m_state = State::STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    qevercloud::LinkedNotebook m_initialLinkedNotebook;
    qevercloud::LinkedNotebook m_foundLinkedNotebook;
    qevercloud::LinkedNotebook m_modifiedLinkedNotebook;
    QList<qevercloud::LinkedNotebook> m_initialLinkedNotebooks;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_LINKED_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
