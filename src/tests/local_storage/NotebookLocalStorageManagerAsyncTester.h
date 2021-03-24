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

#ifndef LIB_QUENTIER_TESTS_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Notebook.h>

#include <cstddef>

namespace quentier {

class LocalStorageManagerAsync;

namespace test {

class NotebookLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit NotebookLocalStorageManagerAsyncTester(QObject * parent = nullptr);
    ~NotebookLocalStorageManagerAsyncTester() override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void getNotebookCountRequest(QUuid requestId);
    void addNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);
    void updateNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);
    void findNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);

    void findDefaultNotebookRequest(
        qevercloud::Notebook notebook, QUuid requestId);

    void listAllNotebooksRequest(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QUuid requestId);

    void listAllSharedNotebooksRequest(QUuid requestId);

    void listSharedNotebooksPerNotebookRequest(
        QString notebookGuid, QUuid requestId);

    void expungeNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onGetNotebookCountCompleted(int count, QUuid requestId);

    void onGetNotebookCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void onAddNotebookCompleted(qevercloud::Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateNotebookCompleted(
        qevercloud::Notebook notebook, QUuid requestId);

    void onUpdateNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onFindNotebookCompleted(
        qevercloud::Notebook notebook, QUuid requestId);

    void onFindNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onFindDefaultNotebookCompleted(
        qevercloud::Notebook notebook, QUuid requestId);

    void onFindDefaultNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onListAllNotebooksCompleted(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, QList<qevercloud::Notebook> notebooks,
        QUuid requestId);

    void onListAllNotebooksFailed(
        std::size_t limit, std::size_t offset,
        LocalStorageManager::ListNotebooksOrder order,
        LocalStorageManager::OrderDirection orderDirection,
        QString linkedNotebookGuid, ErrorString errorDescription,
        QUuid requestId);

    void onListAllSharedNotebooksCompleted(
        QList<qevercloud::SharedNotebook> sharedNotebooks, QUuid requestId);

    void onListAllSharedNotebooksFailed(
        ErrorString errorDescription, QUuid requestId);

    void onListSharedNotebooksPerNotebookGuidCompleted(
        QString notebookGuid, QList<qevercloud::SharedNotebook> sharedNotebooks,
        QUuid requestId);

    void onListSharedNotebooksPerNotebookGuidFailed(
        QString notebookGuid, ErrorString errorDescription, QUuid requestId);

    void onExpungeNotebookCompleted(
        qevercloud::Notebook notebook, QUuid requestId);

    void onExpungeNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
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
        STATE_SENT_DELETE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTEBOOK_ONE_REQUEST,
        STATE_SENT_ADD_EXTRA_NOTEBOOK_TWO_REQUEST,
        STATE_SENT_LIST_NOTEBOOKS_REQUEST,
        STATE_SENT_LIST_ALL_SHARED_NOTEBOOKS_REQUEST,
        STATE_SENT_LIST_SHARED_NOTEBOOKS_PER_NOTEBOOK_REQUEST,
        STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_ADD,
        STATE_SENT_FIND_DEFAULT_NOTEBOOK_AFTER_UPDATE
    };

    State m_state = STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    qint32 m_userId = 4;

    qevercloud::Notebook m_initialNotebook;
    qevercloud::Notebook m_foundNotebook;
    qevercloud::Notebook m_modifiedNotebook;
    QList<qevercloud::Notebook> m_initialNotebooks;
    QList<qevercloud::SharedNotebook> m_allInitialSharedNotebooks;
    QList<qevercloud::SharedNotebook> m_initialSharedNotebooksPerNotebook;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_NOTEBOOK_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
