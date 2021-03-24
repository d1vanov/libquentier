/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CONFLICT_RESOLVER_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CONFLICT_RESOLVER_H

#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Notebook.h>

#include <QObject>
#include <QUuid>

class QDebug;

namespace quentier {

class LocalStorageManagerAsync;
class NotebookSyncCache;

/**
 * The NotebookSyncConflictResolver class resolves the conflict between two
 * notebooks: the one downloaded from the remote server and the local one.
 * The conflict resolution might involve changes in other notebooks, seemingly
 * unrelated to the currently conflicting ones
 */
class Q_DECL_HIDDEN NotebookSyncConflictResolver final : public QObject
{
    Q_OBJECT
public:
    explicit NotebookSyncConflictResolver(
        qevercloud::Notebook remoteNotebook,
        QString remoteNotebookLinkedNotebookGuid,
        qevercloud::Notebook localConflict, NotebookSyncCache & cache,
        LocalStorageManagerAsync & localStorageManagerAsync,
        QObject * parent = nullptr);

    void start();

    [[nodiscard]] const qevercloud::Notebook & remoteNotebook() const noexcept
    {
        return m_remoteNotebook;
    }

    [[nodiscard]] const qevercloud::Notebook & localConflict() const noexcept
    {
        return m_localConflict;
    }

Q_SIGNALS:
    void finished(qevercloud::Notebook remoteNotebook);

    void failure(
        qevercloud::Notebook remoteNotebook, ErrorString errorDescription);

    // private signals
    void fillNotebooksCache();
    void addNotebook(qevercloud::Notebook notebook, QUuid requestId);
    void updateNotebook(qevercloud::Notebook notebook, QUuid requestId);
    void findNotebook(qevercloud::Notebook notebook, QUuid requestId);

private Q_SLOTS:
    void onAddNotebookComplete(qevercloud::Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onUpdateNotebookComplete(
        qevercloud::Notebook notebook, QUuid requestId);

    void onUpdateNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onFindNotebookComplete(qevercloud::Notebook notebook, QUuid requestId);

    void onFindNotebookFailed(
        qevercloud::Notebook notebook, ErrorString errorDescription,
        QUuid requestId);

    void onCacheFilled();
    void onCacheFailed(ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processNotebooksConflictByGuid();

    void processNotebooksConflictByName(
        const qevercloud::Notebook & localConflict);

    void overrideLocalChangesWithRemoteChanges();

    void renameConflictingLocalNotebook(
        const qevercloud::Notebook & localConflict);

    enum class State
    {
        Undefined,
        OverrideLocalChangesWithRemoteChanges,
        PendingConflictingNotebookRenaming,
        PendingRemoteNotebookAdoptionInLocalStorage
    };

    friend QDebug & operator<<(QDebug & dbg, State state);

private:
    NotebookSyncCache & m_cache;
    LocalStorageManagerAsync & m_localStorageManagerAsync;

    qevercloud::Notebook m_remoteNotebook;
    qevercloud::Notebook m_localConflict;

    QString m_remoteNotebookLinkedNotebookGuid;

    qevercloud::Notebook m_notebookToBeRenamed;

    State m_state = State::Undefined;

    QUuid m_addNotebookRequestId;
    QUuid m_updateNotebookRequestId;
    QUuid m_findNotebookRequestId;

    bool m_started = false;
    bool m_pendingCacheFilling = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CONFLICT_RESOLVER_H
