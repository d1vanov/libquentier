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

#include "NotebookSyncCache.h"
#include "NotebookSyncConflictResolver.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/UidGenerator.h>

#include <QDebug>

namespace quentier {

NotebookSyncConflictResolver::NotebookSyncConflictResolver(
    const qevercloud::Notebook & remoteNotebook,
    const QString & remoteNotebookLinkedNotebookGuid,
    const qevercloud::Notebook & localConflict, NotebookSyncCache & cache,
    LocalStorageManagerAsync & localStorageManagerAsync, QObject * parent) :
    QObject(parent),
    m_cache(cache), m_localStorageManagerAsync(localStorageManagerAsync),
    m_remoteNotebook(remoteNotebook), m_localConflict(localConflict),
    m_remoteNotebookLinkedNotebookGuid(remoteNotebookLinkedNotebookGuid)
{}

void NotebookSyncConflictResolver::start()
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::start");

    if (Q_UNLIKELY(m_started)) {
        QNDEBUG("synchronization:notebook_conflict", "Already started");
        return;
    }

    m_started = true;

    if (Q_UNLIKELY(!m_remoteNotebook.guid())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote and local "
                       "notebooks: the remote notebook has no guid set"));
        QNWARNING(
            "synchronization:notebook_conflict",
            error << ": " << m_remoteNotebook);
        Q_EMIT failure(m_remoteNotebook, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteNotebook.name())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote and local "
                       "notebooks: the remote notebook has no name set"));
        QNWARNING(
            "synchronization:notebook_conflict",
            error << ": " << m_remoteNotebook);
        Q_EMIT failure(m_remoteNotebook, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.guid() && !m_localConflict.name())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote and local "
                       "notebooks: the local conflicting notebook has neither "
                       "guid nor name set"));
        QNWARNING(
            "synchronization:notebook_conflict",
            error << ": " << m_localConflict);
        Q_EMIT failure(m_remoteNotebook, error);
        return;
    }

    connectToLocalStorage();

    if (m_localConflict.name() &&
        (*m_localConflict.name() == *m_remoteNotebook.name()))
    {
        processNotebooksConflictByName(m_localConflict);
    }
    else {
        processNotebooksConflictByGuid();
    }
}

void NotebookSyncConflictResolver::onAddNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId)
{
    if (requestId != m_addNotebookRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onAddNotebookComplete: request id = "
            << requestId << ", notebook: " << notebook);

    if (m_state == State::PendingRemoteNotebookAdoptionInLocalStorage) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "Successfully added the remote notebook to the local storage");
        Q_EMIT finished(m_remoteNotebook);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving "
                       "the confirmation about the notebook addition from "
                       "the local storage"));
        QNWARNING(
            "synchronization:notebook_conflict",
            error << ", notebook: " << notebook);
        Q_EMIT failure(m_remoteNotebook, error);
    }
}

void NotebookSyncConflictResolver::onAddNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    if (requestId != m_addNotebookRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onAddNotebookFailed: request id = "
            << requestId << ", error description = " << errorDescription
            << "; notebook: " << notebook);

    Q_EMIT failure(m_remoteNotebook, errorDescription);
}

void NotebookSyncConflictResolver::onUpdateNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId)
{
    if (requestId != m_updateNotebookRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onUpdateNotebookComplete: request id = "
            << requestId << ", notebook: " << notebook);

    if (m_state == State::OverrideLocalChangesWithRemoteChanges) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "Successfully overridden the local changes with remote changes");
        Q_EMIT finished(m_remoteNotebook);
        return;
    }
    else if (m_state == State::PendingConflictingNotebookRenaming) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "Successfully renamed the local notebook conflicting by name with "
                << "the remote notebook");

        // Now need to find the duplicate of the remote notebook by guid:
        // 1) if one exists, update it from the remote changes - notwithstanding
        //    its "dirty" state
        // 2) if one doesn't exist, add it to the local storage

        // The cache should have been filled by that moment, otherwise how could
        // the local notebook conflicting by name be renamed properly?
        if (Q_UNLIKELY(!m_cache.isFilled())) {
            ErrorString error(
                QT_TR_NOOP("Internal error: the cache of notebook info is not "
                           "filled while it should have been"));
            QNWARNING("synchronization:notebook_conflict", error);
            Q_EMIT failure(m_remoteNotebook, error);
            return;
        }

        m_state = State::PendingRemoteNotebookAdoptionInLocalStorage;

        const auto & nameByGuidHash = m_cache.nameByGuidHash();
        const auto it = nameByGuidHash.find(*m_remoteNotebook.guid());
        if (it == nameByGuidHash.end()) {
            QNDEBUG(
                "synchronization:notebook_conflict",
                "Found no duplicate of the remote notebook by guid, adding new "
                    << "notebook to the local storage");

            qevercloud::Notebook notebook = m_remoteNotebook;

            if (!m_remoteNotebookLinkedNotebookGuid.isEmpty()) {
                notebook.mutableLocalData()[
                    QStringLiteral("linkedNotebookGuid")] =
                        m_remoteNotebookLinkedNotebookGuid;
            }

            notebook.setLocallyModified(false);
            notebook.setLocalOnly(false);

            m_addNotebookRequestId = QUuid::createUuid();
            QNTRACE(
                "synchronization:notebook_conflict",
                "Emitting the request to add notebook: request id = "
                    << m_addNotebookRequestId << ", notebook: " << notebook);
            Q_EMIT addNotebook(notebook, m_addNotebookRequestId);
        }
        else {
            QNDEBUG(
                "synchronization:notebook_conflict",
                "The duplicate by guid exists in the local storage, updating "
                    << "it with the state of the remote notebook");

            qevercloud::Notebook notebook = m_remoteNotebook;

            notebook.setLocalId(m_localConflict.localId());
            notebook.setParentLocalId(m_localConflict.parentLocalId());
            notebook.mutableLocalData() = m_localConflict.localData();

            if (!m_remoteNotebookLinkedNotebookGuid.isEmpty()) {
                notebook.mutableLocalData()[
                    QStringLiteral("linkedNotebookGuid")] =
                        m_remoteNotebookLinkedNotebookGuid;
            }

            notebook.setLocallyModified(false);
            notebook.setLocalOnly(false);

            m_updateNotebookRequestId = QUuid::createUuid();

            QNTRACE(
                "synchronization:notebook_conflict",
                "Emitting the request to update notebook: request id = "
                    << m_updateNotebookRequestId << ", notebook: " << notebook);

            Q_EMIT updateNotebook(notebook, m_updateNotebookRequestId);
        }
    }
    else if (m_state == State::PendingRemoteNotebookAdoptionInLocalStorage) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "Successfully finalized the sequence of actions required for "
                << "resolving the conflict of notebooks");
        Q_EMIT finished(m_remoteNotebook);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving "
                       "the confirmation about the notebook update "
                       "from the local storage"));
        QNWARNING(
            "synchronization:notebook_conflict",
            error << ", notebook: " << notebook);
        Q_EMIT failure(m_remoteNotebook, error);
    }
}

void NotebookSyncConflictResolver::onUpdateNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    if (requestId != m_updateNotebookRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onUpdateNotebookFailed: request id = "
            << requestId << ", error description = " << errorDescription
            << "; notebook: " << notebook);

    Q_EMIT failure(m_remoteNotebook, errorDescription);
}

void NotebookSyncConflictResolver::onFindNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId)
{
    if (requestId != m_findNotebookRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onFindNotebookComplete: request id = "
            << requestId << ", notebook: " << notebook);

    m_findNotebookRequestId = QUuid();

    // Found the notebook duplicate by name
    processNotebooksConflictByName(notebook);
}

void NotebookSyncConflictResolver::onFindNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    if (requestId != m_findNotebookRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onFindNotebookFailed: request id = "
            << requestId << ", error description = " << errorDescription
            << ", notebook: " << notebook);

    m_findNotebookRequestId = QUuid();

    // Found no duplicate notebook by name, can override the local changes with
    // the remote changes
    overrideLocalChangesWithRemoteChanges();
}

void NotebookSyncConflictResolver::onCacheFilled()
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onCacheFilled");

    if (!m_pendingCacheFilling) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "Not pending the cache filling");
        return;
    }

    m_pendingCacheFilling = false;

    if (m_state == State::PendingConflictingNotebookRenaming) {
        renameConflictingLocalNotebook(m_notebookToBeRenamed);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving "
                       "the notebook info cache filling notification"));
        QNWARNING(
            "synchronization:notebook_conflict",
            error << ", state = " << m_state);
        Q_EMIT failure(m_remoteNotebook, error);
    }
}

void NotebookSyncConflictResolver::onCacheFailed(ErrorString errorDescription)
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::onCacheFailed: " << errorDescription);

    if (!m_pendingCacheFilling) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "Not pending the cache filling");
        return;
    }

    m_pendingCacheFilling = false;
    Q_EMIT failure(m_remoteNotebook, errorDescription);
}

void NotebookSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::connectToLocalStorage");

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &NotebookSyncConflictResolver::addNotebook,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &NotebookSyncConflictResolver::updateNotebook,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &NotebookSyncConflictResolver::findNotebook,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNotebookRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &NotebookSyncConflictResolver::onAddNotebookComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookFailed, this,
        &NotebookSyncConflictResolver::onAddNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &NotebookSyncConflictResolver::onUpdateNotebookComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookFailed, this,
        &NotebookSyncConflictResolver::onUpdateNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookComplete, this,
        &NotebookSyncConflictResolver::onFindNotebookComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::findNotebookFailed, this,
        &NotebookSyncConflictResolver::onFindNotebookFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

void NotebookSyncConflictResolver::processNotebooksConflictByGuid()
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::processNotebooksConflictByGuid");

    // Need to understand whether there's a duplicate by name in the local
    // storage for the new state of the remote notebook

    if (m_cache.isFilled()) {
        const auto & guidByNameHash = m_cache.guidByNameHash();

        const auto it = guidByNameHash.find(
            m_remoteNotebook.name().value().toLower());

        if (it == guidByNameHash.end()) {
            QNDEBUG(
                "synchronization:notebook_conflict",
                "As deduced by "
                    << "the existing notebook info cache, "
                    << "there is no local notebook with the same name "
                    << "as the name from the new state of the remote "
                    << "notebook, can safely override the local changes "
                    << "with the remote changes: " << m_remoteNotebook);
            overrideLocalChangesWithRemoteChanges();
            return;
        }
        // NOTE: no else block because even if we know the duplicate notebook by
        // name exists, we still need to have its full state in order to rename
        // it
    }

    qevercloud::Notebook dummyNotebook;
    dummyNotebook.setLocalId(QString{});
    dummyNotebook.setName(m_remoteNotebook.name().value());
    m_findNotebookRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:notebook_conflict",
        "Emitting the request to find notebook by name: request id = "
            << m_findNotebookRequestId << ", notebook: " << dummyNotebook);

    Q_EMIT findNotebook(dummyNotebook, m_findNotebookRequestId);
}

void NotebookSyncConflictResolver::processNotebooksConflictByName(
    const qevercloud::Notebook & localConflict)
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::processNotebooksConflictByName: "
            << "local conflict = " << localConflict);

    if (localConflict.guid() &&
        (*localConflict.guid() == m_remoteNotebook.guid().value()))
    {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "The conflicting notebooks match by name and guid => "
                << "the changes from the remote notebook should just "
                << "override the local changes");
        overrideLocalChangesWithRemoteChanges();
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "The conflicting notebooks match by name but not by guid");

    QString localConflictLinkedNotebookGuid;

    const auto linkedNotebookGuidIt = localConflict.localData().constFind(
        QStringLiteral("linkedNotebookGuid"));

    if (linkedNotebookGuidIt != localConflict.localData().constEnd()) {
        localConflictLinkedNotebookGuid =
            linkedNotebookGuidIt.value().toString();
    }

    if (localConflictLinkedNotebookGuid != m_remoteNotebookLinkedNotebookGuid) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "The notebooks conflicting by name don't have matching linked "
                << "notebook guids => they are either from user's own account "
                << "and a linked notebook or from two different linked "
                << "notebooks => can just add the remote linked notebook to "
                << "the local storage");

        m_state = State::PendingRemoteNotebookAdoptionInLocalStorage;

        qevercloud::Notebook notebook = m_remoteNotebook;

        if (!m_remoteNotebookLinkedNotebookGuid.isEmpty()) {
            notebook.mutableLocalData()[QStringLiteral("linkedNotebookGuid")] =
                m_remoteNotebookLinkedNotebookGuid;
        }

        notebook.setLocallyModified(false);
        notebook.setLocalOnly(false);

        m_addNotebookRequestId = QUuid::createUuid();

        QNTRACE(
            "synchronization:notebook_conflict",
            "Emitting the request to add notebook: request id = "
                << m_addNotebookRequestId << ", notebook: " << notebook);

        Q_EMIT addNotebook(notebook, m_addNotebookRequestId);
        return;
    }

    // NOTE: in theory one linked notebook should correspond to exactly one
    // notebook, however, there is no such constraint within the local storage,
    // so won't implement it here; who knows, maybe some day Evernote would
    // actually allow to map two notebooks to a single linked notebook

    QNDEBUG(
        "synchronization:notebook_conflict",
        "Both conflicting notebooks are from user's own account or from "
            << "the same linked notebook => should rename the local "
            << "conflicting notebook to \"free\" the name it occupies");

    m_state = State::PendingConflictingNotebookRenaming;

    if (!m_cache.isFilled()) {
        QNDEBUG(
            "synchronization:notebook_conflict",
            "The cache of notebook info has not been filled yet");

        QObject::connect(
            &m_cache, &NotebookSyncCache::filled, this,
            &NotebookSyncConflictResolver::onCacheFilled);

        QObject::connect(
            &m_cache, &NotebookSyncCache::failure, this,
            &NotebookSyncConflictResolver::onCacheFailed);

        QObject::connect(
            this, &NotebookSyncConflictResolver::fillNotebooksCache, &m_cache,
            &NotebookSyncCache::fill);

        m_pendingCacheFilling = true;
        m_notebookToBeRenamed = localConflict;

        QNTRACE(
            "synchronization:notebook_conflict",
            "Emitting the request to fill the notebooks cache");

        Q_EMIT fillNotebooksCache();
        return;
    }

    QNDEBUG(
        "synchronization:notebook_conflict",
        "The cache of notebook info has already been filled");

    renameConflictingLocalNotebook(localConflict);
}

void NotebookSyncConflictResolver::overrideLocalChangesWithRemoteChanges()
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::overrideLocalChangesWithRemoteChanges");

    m_state = State::OverrideLocalChangesWithRemoteChanges;

    qevercloud::Notebook notebook = m_remoteNotebook;
    notebook.setLocalId(m_localConflict.localId());
    notebook.setParentLocalId(m_localConflict.parentLocalId());
    notebook.mutableLocalData() = m_localConflict.localData();

    if (!m_remoteNotebookLinkedNotebookGuid.isEmpty()) {
        notebook.mutableLocalData()[QStringLiteral("linkedNotebookGuid")] =
            m_remoteNotebookLinkedNotebookGuid;
    }

    notebook.setLocallyModified(false);
    notebook.setLocalOnly(false);

    if (!m_remoteNotebookLinkedNotebookGuid.isEmpty()) {
        // NOTE: the notebook coming from the linked notebook might be marked as
        // default and/or last used which might not make much sense in
        // the context of the user's own default and/or last used notebooks so
        // removing these two properties
        notebook.mutableLocalData()[QStringLiteral("lastUsed")] = false;
        notebook.setDefaultNotebook(false);
    }

    m_updateNotebookRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:notebook_conflict",
        "Emitting the request to "
            << "update notebook: request id = " << m_updateNotebookRequestId
            << ", notebook: " << notebook);

    Q_EMIT updateNotebook(notebook, m_updateNotebookRequestId);
}

void NotebookSyncConflictResolver::renameConflictingLocalNotebook(
    const qevercloud::Notebook & localConflict)
{
    QNDEBUG(
        "synchronization:notebook_conflict",
        "NotebookSyncConflictResolver::renameConflictingLocalNotebook: "
            << "local conflict = " << localConflict);

    const QString name =
        (localConflict.name() ? *localConflict.name()
                              : m_remoteNotebook.name().value());

    const auto & guidByNameHash = m_cache.guidByNameHash();

    QString conflictingName =
        name + QStringLiteral(" - ") + tr("conflicting");

    int suffix = 1;
    QString currentName = conflictingName;
    auto it = guidByNameHash.find(currentName.toLower());
    while (it != guidByNameHash.end()) {
        currentName = conflictingName + QStringLiteral(" (") +
            QString::number(suffix) + QStringLiteral(")");

        ++suffix;
        it = guidByNameHash.find(currentName.toLower());
    }

    conflictingName = currentName;

    qevercloud::Notebook notebook = localConflict;
    notebook.setName(conflictingName);
    notebook.setLocallyModified(true);

    m_updateNotebookRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:notebook_conflict",
        "Emitting the request to update notebook: request id = "
            << m_updateNotebookRequestId << ", notebook: " << notebook);

    Q_EMIT updateNotebook(notebook, m_updateNotebookRequestId);
}

QDebug & operator<<(
    QDebug & dbg, const NotebookSyncConflictResolver::State state)
{
    using State = NotebookSyncConflictResolver::State;

    switch (state) {
    case State::Undefined:
        dbg << "Undefined";
        break;
    case State::OverrideLocalChangesWithRemoteChanges:
        dbg << "Override local changes with remote changes";
        break;
    case State::PendingConflictingNotebookRenaming:
        dbg << "Pending conflicting notebook renaming";
        break;
    case State::PendingRemoteNotebookAdoptionInLocalStorage:
        dbg << "Pending remote notebook adoption in local storage";
        break;
    default:
        dbg << "Unknown (" << static_cast<qint64>(state) << ")";
        break;
    }

    return dbg;
}

} // namespace quentier
