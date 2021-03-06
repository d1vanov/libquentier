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

#ifndef LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
#define LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

namespace test {

class ResourceLocalStorageManagerAsyncTester final : public QObject
{
    Q_OBJECT
public:
    explicit ResourceLocalStorageManagerAsyncTester(QObject * parent = nullptr);
    ~ResourceLocalStorageManagerAsyncTester();

public Q_SLOTS:
    void onInitTestCase();

Q_SIGNALS:
    void success();
    void failure(QString errorDescription);

    // private signals:
    void addNotebookRequest(Notebook notebook, QUuid requestId);
    void addNoteRequest(Note note, QUuid requestId);
    void getResourceCountRequest(QUuid requestId);
    void addResourceRequest(Resource resource, QUuid requestId);
    void updateResourceRequest(Resource resource, QUuid requestId);

    void findResourceRequest(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void expungeResourceRequest(Resource resource, QUuid requestId);

private Q_SLOTS:
    void initialize();
    void onAddNotebookCompleted(Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onAddNoteCompleted(Note note, QUuid requestId);

    void onAddNoteFailed(
        Note note, ErrorString errorDescription, QUuid requestId);

    void onGetResourceCountCompleted(int count, QUuid requestId);

    void onGetResourceCountFailed(
        ErrorString errorDescription, QUuid requestId);

    void onAddResourceCompleted(Resource resource, QUuid requestId);

    void onAddResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

    void onUpdateResourceCompleted(Resource resource, QUuid requestId);

    void onUpdateResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

    void onFindResourceCompleted(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        QUuid requestId);

    void onFindResourceFailed(
        Resource resource, LocalStorageManager::GetResourceOptions options,
        ErrorString errorDescription, QUuid requestId);

    void onExpungeResourceCompleted(Resource resource, QUuid requestId);

    void onExpungeResourceFailed(
        Resource resource, ErrorString errorDescription, QUuid requestId);

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

    Notebook m_notebook;
    Note m_note;
    Resource m_initialResource;
    Resource m_foundResource;
    Resource m_modifiedResource;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_RESOURCE_LOCAL_STORAGE_MANAGER_ASYNC_TESTER_H
