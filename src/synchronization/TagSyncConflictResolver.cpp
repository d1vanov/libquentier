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

#include "TagSyncCache.h"
#include "TagSyncConflictResolver.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

TagSyncConflictResolver::TagSyncConflictResolver(
    qevercloud::Tag remoteTag, QString remoteTagLinkedNotebookGuid,
    qevercloud::Tag localConflict, TagSyncCache & cache,
    LocalStorageManagerAsync & localStorageManagerAsync, QObject * parent) :
    QObject(parent),
    m_cache(cache), m_localStorageManagerAsync(localStorageManagerAsync),
    m_remoteTag(std::move(remoteTag)),
    m_localConflict(std::move(localConflict)),
    m_remoteTagLinkedNotebookGuid(std::move(remoteTagLinkedNotebookGuid))
{}

void TagSyncConflictResolver::start()
{
    QNDEBUG("synchronization:tag_conflict", "TagSyncConflictResolver::start");

    if (m_started) {
        QNDEBUG("synchronization:tag_conflict", "Already started");
        return;
    }

    m_started = true;

    if (Q_UNLIKELY(!m_remoteTag.guid())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote and local "
                       "tags: the remote tag has no guid set"));
        QNWARNING("synchronization:tag_conflict", error << ": " << m_remoteTag);
        Q_EMIT failure(m_remoteTag, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteTag.name())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote and local "
                       "tags: the remote tag has no guid set"));
        QNWARNING("synchronization:tag_conflict", error << ": " << m_remoteTag);
        Q_EMIT failure(m_remoteTag, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.guid() && !m_localConflict.name())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local tags: the local conflicting tag "
                       "has neither guid nor name set"));
        QNWARNING(
            "synchronization:tag_conflict", error << ": " << m_localConflict);
        Q_EMIT failure(m_remoteTag, error);
        return;
    }

    connectToLocalStorage();

    if (m_localConflict.name() &&
        (*m_localConflict.name() == *m_remoteTag.name()))
    {
        processTagsConflictByName(m_localConflict);
    }
    else {
        processTagsConflictByGuid();
    }
}

void TagSyncConflictResolver::onAddTagComplete(
    qevercloud::Tag tag, QUuid requestId) // NOLINT
{
    if (requestId != m_addTagRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onAddTagComplete: request id = "
            << requestId << ", tag: " << tag);

    if (m_state == State::PendingRemoteTagAdoptionInLocalStorage) {
        QNDEBUG(
            "synchronization:tag_conflict",
            "Successfully added the remote tag to the local storage");
        Q_EMIT finished(m_remoteTag);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving "
                       "the confirmation about the tag addition "
                       "from the local storage"));
        QNWARNING("synchronization:tag_conflict", error << ", tag: " << tag);
        Q_EMIT failure(m_remoteTag, error);
    }
}

void TagSyncConflictResolver::onAddTagFailed(
    qevercloud::Tag tag, ErrorString errorDescription, // NOLINT
    QUuid requestId)
{
    if (requestId != m_addTagRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onAddTagFailed: request id = "
            << requestId << ", error description = " << errorDescription
            << "; tag: " << tag);

    Q_EMIT failure(m_remoteTag, errorDescription);
}

void TagSyncConflictResolver::onUpdateTagComplete(
    qevercloud::Tag tag, QUuid requestId) // NOLINT
{
    if (requestId != m_updateTagRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onUpdateTagComplete: request id = "
            << requestId << ", tag: " << tag);

    if (m_state == State::OverrideLocalChangesWithRemoteChanges) {
        QNDEBUG(
            "synchronization:tag_conflict",
            "Successfully overridden the local changes with remote changes");
        Q_EMIT finished(m_remoteTag);
        return;
    }

    if (m_state == State::PendingConflictingTagRenaming) {
        QNDEBUG(
            "synchronization:tag_conflict",
            "Successfully renamed the local tag conflicting by name with the "
                << "remote tag");

        // Now need to find the duplicate of the remote tag by guid:
        // 1) if one exists, update it from the remote changes - notwithstanding
        //    its locally modified state
        // 2) if one doesn't exist, add it to the local storage

        // The cache should have been filled by that moment, otherwise how could
        // the local tag conflicting by name be renamed properly?
        if (Q_UNLIKELY(!m_cache.isFilled())) {
            ErrorString error(
                QT_TR_NOOP("Internal error: the cache of tag info "
                           "is not filled while it should have been"));
            QNWARNING("synchronization:tag_conflict", error);
            Q_EMIT failure(m_remoteTag, error);
            return;
        }

        m_state = State::PendingRemoteTagAdoptionInLocalStorage;

        const QHash<QString, QString> & nameByGuidHash =
            m_cache.nameByGuidHash();

        const auto it = nameByGuidHash.find(*m_remoteTag.guid());
        if (it == nameByGuidHash.end()) {
            QNDEBUG(
                "synchronization:tag_conflict",
                "Found no duplicate of the remote tag by guid, adding new tag "
                    << "to the local storage");

            qevercloud::Tag tag = m_remoteTag;
            tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
            tag.setLocallyModified(false);
            tag.setLocalOnly(false);

            m_addTagRequestId = QUuid::createUuid();

            QNTRACE(
                "synchronization:tag_conflict",
                "Emitting the request to add tag: request id = "
                    << m_addTagRequestId << ", tag: " << tag);

            Q_EMIT addTag(tag, m_addTagRequestId);
        }
        else {
            QNDEBUG(
                "synchronization:tag_conflict",
                "The duplicate by guid exists in the local storage, updating "
                    << "it with the state of the remote tag");

            qevercloud::Tag tag = m_remoteTag;
            tag.setLocalId(m_localConflict.localId());
            tag.setLocalData(m_localConflict.localData());
            tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
            tag.setLocallyModified(false);
            tag.setLocalOnly(false);

            m_updateTagRequestId = QUuid::createUuid();

            QNTRACE(
                "synchronization:tag_conflict",
                "Emitting the request to update tag: request id = "
                    << m_updateTagRequestId << ", tag: " << tag
                    << "\nLocal conflict: " << m_localConflict);

            Q_EMIT updateTag(tag, m_updateTagRequestId);
        }
    }
    else if (m_state == State::PendingRemoteTagAdoptionInLocalStorage) {
        QNDEBUG(
            "synchronization:tag_conflict",
            "Successfully finalized the sequence of actions required for "
                << "resolving the conflict of tags");

        Q_EMIT finished(m_remoteTag);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving "
                       "the confirmation about the tag update "
                       "from the local storage"));
        QNWARNING("synchronization:tag_conflict", error << ", tag: " << tag);
        Q_EMIT failure(m_remoteTag, error);
    }
}

void TagSyncConflictResolver::onUpdateTagFailed(
    qevercloud::Tag tag, ErrorString errorDescription, // NOLINT
    QUuid requestId)
{
    if (requestId != m_updateTagRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onUpdateTagFailed: request id = "
            << requestId << ", error description = " << errorDescription
            << "; tag: " << tag);

    Q_EMIT failure(m_remoteTag, errorDescription);
}

void TagSyncConflictResolver::onFindTagComplete(
    qevercloud::Tag tag, QUuid requestId) // NOLINT
{
    if (requestId != m_findTagRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onFindTagComplete: tag = "
            << tag << "\nRequest id = " << requestId);

    m_findTagRequestId = QUuid();

    // Found the tag duplicate by name
    processTagsConflictByName(tag);
}

void TagSyncConflictResolver::onFindTagFailed(
    qevercloud::Tag tag, ErrorString errorDescription, // NOLINT
    QUuid requestId)
{
    if (requestId != m_findTagRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onFindTagFailed: tag = "
            << tag << "\nError description = " << errorDescription
            << "; request id = " << requestId);

    m_findTagRequestId = QUuid();

    // Found no duplicate tag by name, can override the local changes with
    // the remote changes
    overrideLocalChangesWithRemoteChanges();
}

void TagSyncConflictResolver::onCacheFilled()
{
    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onCacheFilled");

    if (!m_pendingCacheFilling) {
        QNDEBUG(
            "synchronization:tag_conflict", "Not pending the cache filling");
        return;
    }

    m_pendingCacheFilling = false;

    if (m_state == State::PendingConflictingTagRenaming) {
        renameConflictingLocalTag(m_tagToBeRenamed);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Internal error: wrong state on receiving "
                       "the tag info cache filling notification"));
        QNWARNING(
            "synchronization:tag_conflict", error << ", state = " << m_state);
        Q_EMIT failure(m_remoteTag, error);
    }
}

void TagSyncConflictResolver::onCacheFailed(
    ErrorString errorDescription) // NOLINT
{
    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::onCacheFailed: " << errorDescription);

    if (!m_pendingCacheFilling) {
        QNDEBUG(
            "synchronization:tag_conflict", "Not pending the cache filling");
        return;
    }

    m_pendingCacheFilling = false;
    Q_EMIT failure(m_remoteTag, errorDescription);
}

void TagSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::connectToLocalStorage");

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &TagSyncConflictResolver::addTag, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddTagRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &TagSyncConflictResolver::updateTag, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateTagRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &TagSyncConflictResolver::findTag, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onFindTagRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &TagSyncConflictResolver::onAddTagComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::addTagFailed,
        this, &TagSyncConflictResolver::onAddTagFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateTagComplete, this,
        &TagSyncConflictResolver::onUpdateTagComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::updateTagFailed,
        this, &TagSyncConflictResolver::onUpdateTagFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::findTagComplete,
        this, &TagSyncConflictResolver::onFindTagComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::findTagFailed,
        this, &TagSyncConflictResolver::onFindTagFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

void TagSyncConflictResolver::processTagsConflictByGuid()
{
    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::processTagsConflictByGuid");

    // Need to understand whether there's a duplicate by name in the local
    // storage for the new state of the remote tag

    if (m_cache.isFilled()) {
        const QHash<QString, QString> & guidByNameHash =
            m_cache.guidByNameHash();

        const auto it = guidByNameHash.find(
            m_remoteTag.name().value().toLower());

        if (it == guidByNameHash.end()) {
            QNDEBUG(
                "synchronization:tag_conflict",
                "As deduced by the existing tag info cache, there is no local "
                    << "tag with the same name as the name from the new state "
                    << "of the remote tag, can safely override the local "
                    << "changes with remote changes: " << m_remoteTag);
            overrideLocalChangesWithRemoteChanges();
            return;
        }
        // NOTE: no else block because even if we know the duplicate tag by name
        // exists, we still need to have its full state in order to rename it
    }

    qevercloud::Tag dummyTag;
    dummyTag.setLocalId(QString{});
    dummyTag.setName(m_remoteTag.name().value());
    m_findTagRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:tag_conflict",
        "Emitting the request to find tag by name: request id = "
            << m_findTagRequestId << ", tag = " << dummyTag);

    Q_EMIT findTag(dummyTag, m_findTagRequestId);
}

void TagSyncConflictResolver::processTagsConflictByName(
    const qevercloud::Tag & localConflict)
{
    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::processTagsConflictByName: local conflict = "
            << localConflict);

    if (localConflict.guid() &&
        (*localConflict.guid() == m_remoteTag.guid().value())) {
        QNDEBUG(
            "synchronization:tag_conflict",
            "The conflicting tags match by name and guid => the changes from "
                << "the remote tag should just override the local changes");
        overrideLocalChangesWithRemoteChanges();
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "The conflicting tags match by name but not by guid");

    const QString localConflictLinkedNotebookGuid =
        localConflict.linkedNotebookGuid().value_or(QString{});

    if (localConflictLinkedNotebookGuid != m_remoteTagLinkedNotebookGuid) {
        QNDEBUG(
            "synchronization:tag_conflict",
            "The tags conflicting by name don't have matching linked notebook "
                << "guids => they are either from user's own account and a "
                << "linked notebook or from two different linked notebooks => "
                << "can just add the remote tag to the local storage");

        m_state = State::PendingRemoteTagAdoptionInLocalStorage;

        qevercloud::Tag tag = m_remoteTag;
        tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
        tag.setLocallyModified(false);
        tag.setLocalOnly(false);

        m_addTagRequestId = QUuid::createUuid();

        QNTRACE(
            "synchronization:tag_conflict",
            "Emitting the request to add tag: request id = "
                << m_addTagRequestId << ", tag: " << tag);

        Q_EMIT addTag(tag, m_addTagRequestId);
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "Both conflicting tags are either from user's own account or from the "
            << "same linked notebook => should rename the local conflicting "
            << "tag to \"free\" the name it occupies");

    m_state = State::PendingConflictingTagRenaming;

    if (!m_cache.isFilled()) {
        QNDEBUG(
            "synchronization:tag_conflict",
            "The cache of tag info has not been filled yet");

        QObject::connect(
            &m_cache, &TagSyncCache::filled, this,
            &TagSyncConflictResolver::onCacheFilled);

        QObject::connect(
            &m_cache, &TagSyncCache::failure, this,
            &TagSyncConflictResolver::onCacheFailed);

        QObject::connect(
            this, &TagSyncConflictResolver::fillTagsCache, &m_cache,
            &TagSyncCache::fill);

        m_pendingCacheFilling = true;
        m_tagToBeRenamed = localConflict;

        QNTRACE(
            "synchronization:tag_conflict",
            "Emitting the request to fill the tags cache");

        Q_EMIT fillTagsCache();
        return;
    }

    QNDEBUG(
        "synchronization:tag_conflict",
        "The cache of notebook info has already been filled");

    renameConflictingLocalTag(localConflict);
}

void TagSyncConflictResolver::overrideLocalChangesWithRemoteChanges()
{
    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::overrideLocalChangesWithRemoteChanges");

    m_state = State::OverrideLocalChangesWithRemoteChanges;

    qevercloud::Tag tag = m_remoteTag;
    tag.setLocalId(m_localConflict.localId());
    tag.setLocalData(m_localConflict.localData());
    tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
    tag.setLocallyModified(false);
    tag.setLocalOnly(false);

    // Clearing the parent local id info: if this tag has parent guid,
    // the parent local id would be complemented by the local storage;
    // otherwise the parent would be removed from this tag
    tag.setParentTagLocalId(QString{});

    m_updateTagRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:tag_conflict",
        "Emitting the request to update tag: request id = "
            << m_updateTagRequestId << ", tag: " << tag);

    Q_EMIT updateTag(tag, m_updateTagRequestId);
}

void TagSyncConflictResolver::renameConflictingLocalTag(
    const qevercloud::Tag & localConflict)
{
    QNDEBUG(
        "synchronization:tag_conflict",
        "TagSyncConflictResolver::renameConflictingLocalTag: local conflict = "
            << localConflict);

    const QString name =
        (localConflict.name() ? *localConflict.name()
                              : m_remoteTag.name().value());

    const auto & guidByNameHash = m_cache.guidByNameHash();

    QString conflictingName = name + QStringLiteral(" - ") + tr("conflicting");

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

    qevercloud::Tag tag = localConflict;
    tag.setName(conflictingName);
    tag.setLocallyModified(true);

    m_updateTagRequestId = QUuid::createUuid();

    QNTRACE(
        "synchronization:tag_conflict",
        "Emitting the request to update tag: request id = "
            << m_updateTagRequestId << ", tag: " << tag);

    Q_EMIT updateTag(tag, m_updateTagRequestId);
}

QDebug & operator<<(QDebug & dbg, const TagSyncConflictResolver::State state)
{
    using State = TagSyncConflictResolver::State;

    switch (state) {
    case State::Undefined:
        dbg << "Undefined";
        break;
    case State::OverrideLocalChangesWithRemoteChanges:
        dbg << "Override local changes with remote changes";
        break;
    case State::PendingConflictingTagRenaming:
        dbg << "Pending conflicting tag renaming";
        break;
    case State::PendingRemoteTagAdoptionInLocalStorage:
        dbg << "Pending remote tag adoption in local storage";
        break;
    default:
        dbg << "Unknown (" << static_cast<qint64>(state) << ")";
        break;
    }

    return dbg;
}

} // namespace quentier
