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

#ifndef LIB_QUENTIER_TESTS_LINKED_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_LINKED_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/LinkedNotebook.h>

QT_FORWARD_DECLARE_CLASS(QDebug)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

namespace test {

class LinkedNotebookLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit LinkedNotebookLocalStorageManagerAsyncTester(
        QObject * parent = nullptr);

    virtual ~LinkedNotebookLocalStorageManagerAsyncTester() override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void getLinkedNotebookCountRequest(QUuid requestId);
    void addLinkedNotebookRequest(LinkedNotebook notebook, QUuid requestId);
    void updateLinkedNotebookRequest(LinkedNotebook notebook, QUuid requestId);
    void findLinkedNotebookRequest(LinkedNotebook notebook, QUuid requestId);

    void listAllLinkedNotebooksRequest(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection, QUuid requestId);

    void expungeLinkedNotebookRequest(LinkedNotebook notebook, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onGetLinkedNotebookCountCompleted(int count, QUuid requestId);

    void onGetLinkedNotebookCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void onAddLinkedNotebookCompleted(LinkedNotebook notebook, QUuid requestId);

    void onAddLinkedNotebookFailed(
        LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId);

    void onUpdateLinkedNotebookCompleted(
        LinkedNotebook notebook, QUuid requestId);

    void onUpdateLinkedNotebookFailed(
        LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId);

    void onFindLinkedNotebookCompleted(
        LinkedNotebook notebook, QUuid requestId);

    void onFindLinkedNotebookFailed(
        LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId);

    void onListAllLinkedNotebooksCompleted(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QList<LinkedNotebook> linkedNotebooks, QUuid requestId);

    void onListAllLinkedNotebooksFailed(
        size_t limit, size_t offset,
        LocalStorageManager::ListLinkedNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeLinkedNotebookCompleted(
        LinkedNotebook notebook, QUuid requestId);

    void onExpungeLinkedNotebookFailed(
        LinkedNotebook notebook, ErrorString errorDescription, QUuid requestId);

private:
    void createConnections();
    void clear();

    enum class State
    {
        STATE_UNINITIALIZED = 0,
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

    friend QDebug & operator<<(QDebug & dbg, const State state);

private:
    State m_state = State::STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    LinkedNotebook m_initialLinkedNotebook;
    LinkedNotebook m_foundLinkedNotebook;
    LinkedNotebook m_modifiedLinkedNotebook;
    QList<LinkedNotebook> m_initialLinkedNotebooks;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_LINKED_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
