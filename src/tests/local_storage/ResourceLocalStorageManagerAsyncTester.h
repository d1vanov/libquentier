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

#ifndef LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/generated/types/Note.h>
#include <qevercloud/generated/types/Notebook.h>
#include <qevercloud/generated/types/Resource.h>

namespace quentier {

class LocalStorageManagerAsync;

namespace test {

class ResourceLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit ResourceLocalStorageManagerAsyncTester(QObject * parent = nullptr);
    ~ResourceLocalStorageManagerAsyncTester() override;

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void addNotebookRequest(qevercloud::Notebook notebook, QUuid requestId);
    void addNoteRequest(qevercloud::Note note, QUuid requestId);
    void getResourceCountRequest(QUuid requestId);
    void addResourceRequest(qevercloud::Resource resource, QUuid requestId);
    void updateResourceRequest(qevercloud::Resource resource, QUuid requestId);

    void findResourceRequest(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options, QUuid requestId);

    void expungeResourceRequest(qevercloud::Resource resource, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onAddNotebookCompleted(qevercloud::Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onAddNoteCompleted(qevercloud::Note note, QUuid requestId);

    void onAddNoteFailed(
        qevercloud::Note note, ErrorString errorDescription, QUuid requestId);

    void onGetResourceCountCompleted(int count, QUuid requestId);

    void onGetResourceCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void onAddResourceCompleted(qevercloud::Resource resource, QUuid requestId);

    void onAddResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateResourceCompleted(
        qevercloud::Resource resource, QUuid requestId);

    void onUpdateResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

    void onFindResourceCompleted(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options, QUuid requestId);

    void onFindResourceFailed(
        qevercloud::Resource resource,
        LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeResourceCompleted(
        qevercloud::Resource resource, QUuid requestId);

    void onExpungeResourceFailed(
        qevercloud::Resource resource, ErrorString errorDescription,
        QUuid requestId);

private:
    void createConnections();
    void clear();

    enum State
    {
        STATE_UNINITIALIZED,
        STATE_SENT_ADD_NOTEBOOK_REQUEST,
        STATE_SENT_ADD_NOTE_REQUEST,
        STATE_SENT_ADD_REQUEST,
        STATE_SENT_FIND_AFTER_ADD_REQUEST,
        STATE_SENT_UPDATE_REQUEST,
        STATE_SENT_FIND_AFTER_UPDATE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST,
        STATE_SENT_EXPUNGE_REQUEST,
        STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST,
        STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST
    };

    State m_state = STATE_UNINITIALIZED;

    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    QThread * m_pLocalStorageManagerThread = nullptr;

    qevercloud::Notebook m_notebook;
    qevercloud::Note m_note;
    qevercloud::Resource m_initialResource;
    qevercloud::Resource m_foundResource;
    qevercloud::Resource m_modifiedResource;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
