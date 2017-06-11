/*
 * Copyright 2017 Dmitry Ivanov
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
#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloudOAuth.h>
#else
#include <qt4qevercloud/QEverCloudOAuth.h>
#endif

namespace quentier {

QT_FORWARD_DECLARE_CLASS(NotebookSyncConflictResolutionCache)
QT_FORWARD_DECLARE_CLASS(LocalStorageManagerAsync)

/**
 * The NotebookSyncConflictResolver class resolves the conflict between two notebooks: the one downloaded
 * from the remote server and the local one changes somehow. The conflict resolution might involve
 * changes in other notebooks, seemingly unrelated to the currently conflicting ones
 */
class NotebookSyncConflictResolver: public QObject
{
    Q_OBJECT
public:
    explicit NotebookSyncConflictResolver(const qevercloud::Notebook & remoteNotebook,
                                          const Notebook & localConflict,
                                          NotebookSyncConflictResolutionCache & cache,
                                          LocalStorageManagerAsync & localStorageManagerAsync);

Q_SIGNALS:
    void finished(qevercloud::Notebook remoteNotebook);
    void failure(qevercloud::Notebook remoteNotebook, ErrorString errorDescription);

// private signals
    void fillNotebooksCache();
    void addNotebook(Notebook notebook, QUuid requestId);
    void updateNotebook(Notebook notebook, QUuid requestId);

private Q_SLOTS:
    void onAddNotebookComplete(Notebook notebook, QUuid requestId);
    void onAddNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);
    void onUpdateNotebookComplete(Notebook notebook, QUuid requestId);
    void onUpdateNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId);

    void onCacheFilled();
    void onCacheFailed(ErrorString errorDescription);

private:
    void connectToLocalStorage(LocalStorageManagerAsync & localStorageManagerAsync);
    void processNotebooksConflictByName();
    void overrideLocalChangesWithRemoteChanges();
    void renameConflictingLocalNotebook();

    struct State
    {
        enum type
        {
            Undefined = 0,
            OverrideLocalChangesWithRemoteChanges,
            PendingConflictingNotebookRenaming,
            PendingRemoteNotebookAdoptionInLocalStorage
        };
    };

private:
    NotebookSyncConflictResolutionCache &   m_cache;

    qevercloud::Notebook        m_remoteNotebook;
    Notebook                    m_localConflict;

    State::type                 m_state;

    QUuid                       m_addNotebookRequestId;
    QUuid                       m_updateNotebookRequestId;
    QUuid                       m_findNotebookRequestId;

    bool                        m_pendingCacheFilling;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTEBOOK_SYNC_CONFLICT_RESOLVER_H
