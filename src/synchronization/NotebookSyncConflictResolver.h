/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#include <quentier/types/Notebook.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QObject>

QT_FORWARD_DECLARE_CLASS(QDebug)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)
QT_FORWARD_DECLARE_CLASS(NotebookSyncCache)

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
        const qevercloud::Notebook & remoteNotebook,
        const QString & remoteNotebookLinkedNotebookGuid,
        const Notebook & localConflict, NotebookSyncCache & cache,
        LocalStorageManagerAsync & localStorageManagerAsync,
        QObject * parent = nullptr);

    void start();

    const qevercloud::Notebook & remoteNotebook() const
    {
        return m_remoteNotebook;
    }

    const Notebook & localConflict() const
    {
        return m_localConflict;
    }

Q_SIGNALS:
    void finished(qevercloud::Notebook remoteNotebook);

    void failure(
        qevercloud::Notebook remoteNotebook, ErrorString errorDescription);

    // private signals
    void fillNotebooksCache();
    void addNotebook(Notebook notebook, QUuid requestId);
    void updateNotebook(Notebook notebook, QUuid requestId);
    void findNotebook(Notebook notebook, QUuid requestId);

private Q_SLOTS:
    void onAddNotebookComplete(Notebook notebook, QUuid requestId);

    void onAddNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onUpdateNotebookComplete(Notebook notebook, QUuid requestId);

    void onUpdateNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onFindNotebookComplete(Notebook notebook, QUuid requestId);

    void onFindNotebookFailed(
        Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onCacheFilled();
    void onCacheFailed(ErrorString errorDescription);

private:
    void connectToLocalStorage();
    void processNotebooksConflictByGuid();
    void processNotebooksConflictByName(const Notebook & localConflict);
    void overrideLocalChangesWithRemoteChanges();
    void renameConflictingLocalNotebook(const Notebook & localConflict);

    enum class State
    {
        Undefined = 0,
        OverrideLocalChangesWithRemoteChanges,
        PendingConflictingNotebookRenaming,
        PendingRemoteNotebookAdoptionInLocalStorage
    };

    friend QDebug & operator<<(QDebug & dbg, const State state);

private:
    NotebookSyncCache & m_cache;
    LocalStorageManagerAsync & m_localStorageManagerAsync;

    qevercloud::Notebook m_remoteNotebook;
    Notebook m_localConflict;

    QString m_remoteNotebookLinkedNotebookGuid;

    Notebook m_notebookToBeRenamed;

    State m_state = State::Undefined;

    QUuid m_addNotebookRequestId;
    QUuid m_updateNotebookRequestId;
    QUuid m_findNotebookRequestId;

    bool m_started = false;
    bool m_pendingCacheFilling = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CONFLICT_RESOLVER_H
