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

#include "NotebookSyncConflictResolver.h"
#include "NotebookSyncConflictResolutionCache.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

NotebookSyncConflictResolver::NotebookSyncConflictResolver(const qevercloud::Notebook & remoteNotebook,
                                                           const Notebook & localConflict,
                                                           NotebookSyncConflictResolutionCache & cache,
                                                           LocalStorageManagerAsync & localStorageManagerAsync) :
    m_cache(cache),
    m_remoteNotebook(remoteNotebook),
    m_localConflict(localConflict),
    m_state(State::Undefined),
    m_addNotebookRequestId(),
    m_updateNotebookRequestId(),
    m_findNotebookRequestId(),
    m_pendingCacheFilling(false)
{
    if (Q_UNLIKELY(!remoteNotebook.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notebooks: "
                                     "the remote notebook has no guid set"));
        QNWARNING(error << QStringLiteral(": ") << remoteNotebook);
        emit failure(remoteNotebook, error);
        return;
    }

    if (Q_UNLIKELY(!remoteNotebook.name.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notebooks: "
                                     "the remote notebook has no name set"));
        QNWARNING(error << QStringLiteral(": ") << remoteNotebook);
        emit failure(remoteNotebook, error);
        return;
    }

    if (Q_UNLIKELY(!localConflict.hasGuid() && !localConflict.hasName())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notebooks: "
                                     "the local conflicting notebook has neither guid nor name set"));
        QNWARNING(error << QStringLiteral(": ") << localConflict);
        emit failure(remoteNotebook, error);
        return;
    }

    connectToLocalStorage(localStorageManagerAsync);

    if (localConflict.hasName() && (localConflict.name() == remoteNotebook.name.ref())) {
        processNotebooksConflictByName();
    }
    else {
        overrideLocalChangesWithRemoteChanges();
    }
}

void NotebookSyncConflictResolver::onAddNotebookComplete(Notebook notebook, QUuid requestId)
{
    if (requestId != m_addNotebookRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::onAddNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    if (m_state == State::PendingRemoteNotebookAdoptionInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully added the remote notebook to the local storage"));
        emit finished(m_remoteNotebook);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the confirmation about the notebook addition "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", notebook: ") << notebook);
        emit failure(m_remoteNotebook, error);
    }
}

void NotebookSyncConflictResolver::onAddNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addNotebookRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::onAddNotebookFailed: request id = ")
            << requestId << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral("; notebook: ") << notebook);

    emit failure(m_remoteNotebook, errorDescription);
}

void NotebookSyncConflictResolver::onUpdateNotebookComplete(Notebook notebook, QUuid requestId)
{
    if (requestId != m_updateNotebookRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::onUpdateNotebookComplete: request id = ")
            << requestId << QStringLiteral(", notebook: ") << notebook);

    if (m_state == State::OverrideLocalChangesWithRemoteChanges)
    {
        QNDEBUG(QStringLiteral("Successfully overridden the local changes with remote changes"));
        emit finished(m_remoteNotebook);
        return;
    }
    else if (m_state == State::PendingConflictingNotebookRenaming)
    {
        QNDEBUG(QStringLiteral("Successfully renamed the local notebook conflicting by name with the remote notebook"));

        // Now need to find the duplicate of the remote notebook by guid:
        // 1) if one exists, update it from the remote changes - notwithstanding its "dirty" state
        // 2) if one doesn't exist, add it to the local storage

        // The cache should have been filled by that moment, otherwise how could the local notebook conflicting by name
        // be renamed properly?
        if (Q_UNLIKELY(!m_cache.isFilled())) {
            ErrorString error(QT_TR_NOOP("Internal error: the cache of notebook info is not filled while it should have been"));
            QNWARNING(error);
            emit failure(m_remoteNotebook, error);
            return;
        }

        m_state = State::PendingRemoteNotebookAdoptionInLocalStorage;

        const QHash<QString,QString> & nameByGuidHash = m_cache.nameByGuidHash();
        auto it = nameByGuidHash.find(m_remoteNotebook.guid.ref());
        if (it == nameByGuidHash.end())
        {
            QNDEBUG(QStringLiteral("Found no duplicate of the remote notebook by guid, adding new notebook to the local storage"));

            Notebook notebook(m_remoteNotebook);
            notebook.setDirty(false);
            notebook.setLocal(false);

            m_addNotebookRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to add notebook: request id = ") << m_addNotebookRequestId
                    << QStringLiteral(", notebook: ") << notebook);
            emit addNotebook(notebook, m_addNotebookRequestId);
        }
        else
        {
            QNDEBUG(QStringLiteral("The duplicate by guid exists in the local storage, updating it with the state "
                                   "of the remote notebook"));

            Notebook notebook(m_remoteNotebook);
            notebook.setDirty(false);
            notebook.setLocal(false);

            m_updateNotebookRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to update notebook: request id = ") << m_updateNotebookRequestId
                    << QStringLiteral(", notebook: ") << notebook);
            emit updateNotebook(notebook, m_updateNotebookRequestId);
        }
    }
    else if (m_state == State::PendingRemoteNotebookAdoptionInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully finalized the sequence of actions required for resolving the conflict of notebooks"));
        emit finished(m_remoteNotebook);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the confirmation about the notebook update "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", notebook: ") << notebook);
        emit failure(m_remoteNotebook, error);
    }
}

void NotebookSyncConflictResolver::onUpdateNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_updateNotebookRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::onUpdateNotebookFailed: request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription << QStringLiteral("; notebook: ")
            << notebook);

    emit failure(m_remoteNotebook, errorDescription);
}

void NotebookSyncConflictResolver::onCacheFilled()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::onCacheFilled"));

    if (!m_pendingCacheFilling) {
        QNDEBUG(QStringLiteral("Not pending the cache filling"));
        return;
    }

    m_pendingCacheFilling = false;

    if (m_state == State::PendingConflictingNotebookRenaming)
    {
        renameConflictingLocalNotebook();
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the notebook info cache filling notification"));
        QNWARNING(error << QStringLiteral(", state = ") << m_state);
        emit failure(m_remoteNotebook, error);
    }
}

void NotebookSyncConflictResolver::onCacheFailed(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::onCacheFailed: ") << errorDescription);

    if (!m_pendingCacheFilling) {
        QNDEBUG(QStringLiteral("Not pending the cache filling"));
        return;
    }

    m_pendingCacheFilling = false;
    emit failure(m_remoteNotebook, errorDescription);
}

void NotebookSyncConflictResolver::connectToLocalStorage(LocalStorageManagerAsync & localStorageManagerAsync)
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::connectToLocalStorage"));

    // Connect local signals to local storage manager async's slots
    QObject::connect(this, QNSIGNAL(NotebookSyncConflictResolver,addNotebook,Notebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(NotebookSyncConflictResolver,updateNotebook,Notebook,QUuid),
                     &localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,Notebook,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncConflictResolver,onAddNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(NotebookSyncConflictResolver,onAddNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(NotebookSyncConflictResolver,onUpdateNotebookComplete,Notebook,QUuid));
    QObject::connect(&localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(NotebookSyncConflictResolver,onUpdateNotebookFailed,Notebook,ErrorString,QUuid));
}

void NotebookSyncConflictResolver::processNotebooksConflictByName()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::processNotebooksConflictByName"));

    if (m_localConflict.hasGuid() && (m_localConflict.guid() == m_remoteNotebook.guid.ref())) {
        QNDEBUG(QStringLiteral("The conflicting notebooks match by name and guid => the changes from the remote "
                               "notebook should just override the local changes"));
        overrideLocalChangesWithRemoteChanges();
        return;
    }

    QNDEBUG(QStringLiteral("The conflicting notebooks match by name but not by guid => should rename "
                           "the local conflicting notebook to \"free\" the name it occupies"));

    m_state = State::PendingConflictingNotebookRenaming;

    if (!m_cache.isFilled())
    {
        QNDEBUG(QStringLiteral("The cache of notebook info has not been filled yet"));

        QObject::connect(&m_cache, QNSIGNAL(NotebookSyncConflictResolutionCache,filled),
                         this, QNSLOT(NotebookSyncConflictResolver,onCacheFilled));
        QObject::connect(&m_cache, QNSIGNAL(NotebookSyncConflictResolutionCache,failure,ErrorString),
                         this, QNSLOT(NotebookSyncConflictResolver,onCacheFailed,ErrorString));
        QObject::connect(this, QNSIGNAL(NotebookSyncConflictResolver,fillNotebooksCache),
                         &m_cache, QNSLOT(NotebookSyncConflictResolutionCache,fill));

        m_pendingCacheFilling = true;
        QNTRACE(QStringLiteral("Emitting the request to fill the notebooks cache"));
        emit fillNotebooksCache();
        return;
    }

    QNDEBUG(QStringLiteral("The cache of notebook info has already been filled"));
    renameConflictingLocalNotebook();
}

void NotebookSyncConflictResolver::overrideLocalChangesWithRemoteChanges()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::overrideLocalChangesWithRemoteChanges"));

    m_state = State::OverrideLocalChangesWithRemoteChanges;

    m_localConflict = m_remoteNotebook;
    m_localConflict.setDirty(false);
    m_localConflict.setLocal(false);

    m_updateNotebookRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update notebook: request id = ")
            << m_updateNotebookRequestId << QStringLiteral(", notebook: ") << m_localConflict);
    emit updateNotebook(m_localConflict, m_updateNotebookRequestId);
}

void NotebookSyncConflictResolver::renameConflictingLocalNotebook()
{
    QNDEBUG(QStringLiteral("NotebookSyncConflictResolver::renameConflictingLocalNotebook"));

    QString name = (m_localConflict.hasName() ? m_localConflict.name() : m_remoteNotebook.name.ref());

    const QHash<QString,QString> & guidByNameHash = m_cache.guidByNameHash();

    QString conflictingName = name + QStringLiteral(" - ") + tr("conflicting");

    int suffix = 1;
    QString currentName = conflictingName;
    auto it = guidByNameHash.find(currentName);
    while(it != guidByNameHash.end()) {
        currentName = conflictingName + QStringLiteral(" (") + QString::number(suffix) + QStringLiteral(")");
        ++suffix;
        it = guidByNameHash.find(currentName);
    }

    conflictingName = currentName;

    m_localConflict.setName(conflictingName);
    m_localConflict.setDirty(true);

    m_updateNotebookRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update notebook: request id = ")
            << m_updateNotebookRequestId << QStringLiteral(", notebook: ") << m_localConflict);
    emit updateNotebook(m_localConflict, m_updateNotebookRequestId);
}

} // namespace quentier
