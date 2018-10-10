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

#include "TagSyncConflictResolver.h"
#include "TagSyncCache.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

TagSyncConflictResolver::TagSyncConflictResolver(const qevercloud::Tag & remoteTag,
                                                 const QString & remoteTagLinkedNotebookGuid,
                                                 const Tag & localConflict, TagSyncCache & cache,
                                                 LocalStorageManagerAsync & localStorageManagerAsync,
                                                 QObject * parent) :
    QObject(parent),
    m_cache(cache),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_remoteTag(remoteTag),
    m_localConflict(localConflict),
    m_remoteTagLinkedNotebookGuid(remoteTagLinkedNotebookGuid),
    m_tagToBeRenamed(),
    m_state(State::Undefined),
    m_addTagRequestId(),
    m_updateTagRequestId(),
    m_findTagRequestId(),
    m_started(false),
    m_pendingCacheFilling(false)
{}

void TagSyncConflictResolver::start()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::start"));

    if (m_started) {
        QNDEBUG(QStringLiteral("Already started"));
        return;
    }

    m_started = true;

    if (Q_UNLIKELY(!m_remoteTag.guid.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local tags: "
                                     "the remote tag has no guid set"));
        QNWARNING(error << QStringLiteral(": ") << m_remoteTag);
        Q_EMIT failure(m_remoteTag, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteTag.name.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local tags: "
                                     "the remote tag has no guid set"));
        QNWARNING(error << QStringLiteral(": ") << m_remoteTag);
        Q_EMIT failure(m_remoteTag, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.hasGuid() && !m_localConflict.hasName())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notebooks: "
                                     "the local conflicting tag has neither guid nor name set"));
        QNWARNING(error << QStringLiteral(": ") << m_localConflict);
        Q_EMIT failure(m_remoteTag, error);
        return;
    }

    connectToLocalStorage();

    if (m_localConflict.hasName() && (m_localConflict.name() == m_remoteTag.name.ref())) {
        processTagsConflictByName(m_localConflict);
    }
    else {
        processTagsConflictByGuid();
    }
}

void TagSyncConflictResolver::onAddTagComplete(Tag tag, QUuid requestId)
{
    if (requestId != m_addTagRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onAddTagComplete: request id = ")
            << requestId << QStringLiteral(", tag: ") << tag);

    if (m_state == State::PendingRemoteTagAdoptionInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully added the remote tag to the local storage"));
        Q_EMIT finished(m_remoteTag);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the confirmation about the tag addition "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", tag: ") << tag);
        Q_EMIT failure(m_remoteTag, error);
    }
}

void TagSyncConflictResolver::onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addTagRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onAddTagFailed: request id = ")
            << requestId << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral("; tag: ") << tag);

    Q_EMIT failure(m_remoteTag, errorDescription);
}

void TagSyncConflictResolver::onUpdateTagComplete(Tag tag, QUuid requestId)
{
    if (requestId != m_updateTagRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onUpdateTagComplete: request id = ")
            << requestId << QStringLiteral(", tag: ") << tag);

    if (m_state == State::OverrideLocalChangesWithRemoteChanges)
    {
        QNDEBUG(QStringLiteral("Successfully overridden the local changes with remote changes"));
        Q_EMIT finished(m_remoteTag);
        return;
    }
    else if (m_state == State::PendingConflictingTagRenaming)
    {
        QNDEBUG(QStringLiteral("Successfully renamed the local tag conflicting by name with the remote tag"));

        // Now need to find the duplicate of the remote tag by guid:
        // 1) if one exists, update it from the remote changes - notwithstanding its "dirty" state
        // 2) if one doesn't exist, add it to the local storage

        // The cache should have been filled by that moment, otherwise how could the local tag conflicting by name
        // be renamed properly?
        if (Q_UNLIKELY(!m_cache.isFilled())) {
            ErrorString error(QT_TR_NOOP("Internal error: the cache of tag info is not filled while it should have been"));
            QNWARNING(error);
            Q_EMIT failure(m_remoteTag, error);
            return;
        }

        m_state = State::PendingRemoteTagAdoptionInLocalStorage;

        const QHash<QString,QString> & nameByGuidHash = m_cache.nameByGuidHash();
        auto it = nameByGuidHash.find(m_remoteTag.guid.ref());
        if (it == nameByGuidHash.end())
        {
            QNDEBUG(QStringLiteral("Found no duplicate of the remote tag by guid, adding new tag to the local storage"));

            Tag tag(m_remoteTag);
            tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
            tag.setDirty(false);
            tag.setLocal(false);

            m_addTagRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to add tag: request id = ") << m_addTagRequestId
                    << QStringLiteral(", tag: ") << tag);
            Q_EMIT addTag(tag, m_addTagRequestId);
        }
        else
        {
            QNDEBUG(QStringLiteral("The duplicate by guid exists in the local storage, updating it with the state "
                                   "of the remote tag"));

            Tag tag(m_localConflict);
            tag.qevercloudTag() = m_remoteTag;
            tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
            tag.setDirty(false);
            tag.setLocal(false);

            m_updateTagRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to update tag: request id = ") << m_updateTagRequestId
                    << QStringLiteral(", tag: ") << tag << QStringLiteral("\nLocal conflict: ") << m_localConflict);
            Q_EMIT updateTag(tag, m_updateTagRequestId);
        }
    }
    else if (m_state == State::PendingRemoteTagAdoptionInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully finalized the sequence of actions required for resolving the conflict of tags"));
        Q_EMIT finished(m_remoteTag);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the confirmation about the tag update "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", tag: ") << tag);
        Q_EMIT failure(m_remoteTag, error);
    }
}

void TagSyncConflictResolver::onUpdateTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_updateTagRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onUpdateTagFailed: request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription << QStringLiteral("; tag: ")
            << tag);

    Q_EMIT failure(m_remoteTag, errorDescription);
}

void TagSyncConflictResolver::onFindTagComplete(Tag tag, QUuid requestId)
{
    if (requestId != m_findTagRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onFindTagComplete: tag = ") << tag
            << QStringLiteral("\nRequest id = ") << requestId);

    m_findTagRequestId = QUuid();

    // Found the tag duplicate by name
    processTagsConflictByName(tag);
}

void TagSyncConflictResolver::onFindTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_findTagRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onFindTagFailed: tag = ") << tag
            << QStringLiteral("\nError description = ") << errorDescription
            << QStringLiteral("; request id = ") << requestId);

    m_findTagRequestId = QUuid();

    // Found no duplicate tag by name, can override the local changes with the remote changes
    overrideLocalChangesWithRemoteChanges();
}

void TagSyncConflictResolver::onCacheFilled()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onCacheFilled"));

    if (!m_pendingCacheFilling) {
        QNDEBUG(QStringLiteral("Not pending the cache filling"));
        return;
    }

    m_pendingCacheFilling = false;

    if (m_state == State::PendingConflictingTagRenaming)
    {
        renameConflictingLocalTag(m_tagToBeRenamed);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the tag info cache filling notification"));
        QNWARNING(error << QStringLiteral(", state = ") << m_state);
        Q_EMIT failure(m_remoteTag, error);
    }
}

void TagSyncConflictResolver::onCacheFailed(ErrorString errorDescription)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::onCacheFailed: ") << errorDescription);

    if (!m_pendingCacheFilling) {
        QNDEBUG(QStringLiteral("Not pending the cache filling"));
        return;
    }

    m_pendingCacheFilling = false;
    Q_EMIT failure(m_remoteTag, errorDescription);
}

void TagSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::connectToLocalStorage"));

    // Connect local signals to local storage manager async's slots
    QObject::connect(this, QNSIGNAL(TagSyncConflictResolver,addTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(this, QNSIGNAL(TagSyncConflictResolver,updateTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(this, QNSIGNAL(TagSyncConflictResolver,findTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindTagRequest,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onAddTagComplete,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onAddTagFailed,Tag,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onUpdateTagComplete,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onUpdateTagFailed,Tag,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onFindTagComplete,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onFindTagFailed,Tag,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

void TagSyncConflictResolver::processTagsConflictByGuid()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::processTagsConflictByGuid"));

    // Need to understand whether there's a duplicate by name in the local storage for the new state
    // of the remote tag

    if (m_cache.isFilled())
    {
        const QHash<QString,QString> & guidByNameHash = m_cache.guidByNameHash();
        auto it = guidByNameHash.find(m_remoteTag.name.ref().toLower());
        if (it == guidByNameHash.end()) {
            QNDEBUG(QStringLiteral("As deduced by the existing tag info cache, there is no local tag with the same name "
                                   "as the name from the new state of the remote tag, can safely override the local changes "
                                   "with the remote changes: ") << m_remoteTag);
            overrideLocalChangesWithRemoteChanges();
            return;
        }
        // NOTE: no else block because even if we know the duplicate tag by name exists, we still need to have its full
        // state in order to rename it
    }

    Tag dummyTag;
    dummyTag.unsetLocalUid();
    dummyTag.setName(m_remoteTag.name.ref());
    m_findTagRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to find tag by name: request id = ") << m_findTagRequestId
            << QStringLiteral(", tag = ") << dummyTag);
    Q_EMIT findTag(dummyTag, m_findTagRequestId);
}

void TagSyncConflictResolver::processTagsConflictByName(const Tag & localConflict)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::processTagsConflictByName: local conflict = ") << localConflict);

    if (localConflict.hasGuid() && (localConflict.guid() == m_remoteTag.guid.ref())) {
        QNDEBUG(QStringLiteral("The conflicting tags match by name and guid => the changes from the remote "
                               "tag should just override the local changes"));
        overrideLocalChangesWithRemoteChanges();
        return;
    }

    QNDEBUG(QStringLiteral("The conflicting tags match by name but not by guid"));

    QString localConflictLinkedNotebookGuid;
    if (localConflict.hasLinkedNotebookGuid()) {
        localConflictLinkedNotebookGuid = localConflict.linkedNotebookGuid();
    }

    if (localConflictLinkedNotebookGuid != m_remoteTagLinkedNotebookGuid)
    {
        QNDEBUG(QStringLiteral("The tags conflicting by name don't have matching linked notebook guids => "
                               "they are either from user's own account and a linked notebook or from two different "
                               "linked notebooks => can just add the remote tag to the local storage"));

        m_state = State::PendingRemoteTagAdoptionInLocalStorage;

        Tag tag(m_remoteTag);
        tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
        tag.setDirty(false);
        tag.setLocal(false);

        m_addTagRequestId = QUuid::createUuid();
        QNTRACE(QStringLiteral("Emitting the request to add tag: request id = ") << m_addTagRequestId
                << QStringLiteral(", tag: ") << tag);
        Q_EMIT addTag(tag, m_addTagRequestId);
        return;
    }

    QNDEBUG(QStringLiteral("Both conflicting tags are either from user's own account or from the same linked notebook "
                           "=> should rename the local conflicting tag to \"free\" the name it occupies"));

    m_state = State::PendingConflictingTagRenaming;

    if (!m_cache.isFilled())
    {
        QNDEBUG(QStringLiteral("The cache of tag info has not been filled yet"));

        QObject::connect(&m_cache, QNSIGNAL(TagSyncCache,filled),
                         this, QNSLOT(TagSyncConflictResolver,onCacheFilled));
        QObject::connect(&m_cache, QNSIGNAL(TagSyncCache,failure,ErrorString),
                         this, QNSLOT(TagSyncConflictResolver,onCacheFailed,ErrorString));
        QObject::connect(this, QNSIGNAL(TagSyncConflictResolver,fillTagsCache),
                         &m_cache, QNSLOT(TagSyncCache,fill));

        m_pendingCacheFilling = true;
        m_tagToBeRenamed = localConflict;
        QNTRACE(QStringLiteral("Emitting the request to fill the tags cache"));
        Q_EMIT fillTagsCache();
        return;
    }

    QNDEBUG(QStringLiteral("The cache of notebook info has already been filled"));
    renameConflictingLocalTag(localConflict);
}

void TagSyncConflictResolver::overrideLocalChangesWithRemoteChanges()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::overrideLocalChangesWithRemoteChanges"));

    m_state = State::OverrideLocalChangesWithRemoteChanges;

    Tag tag(m_localConflict);
    tag.qevercloudTag() = m_remoteTag;
    tag.setLinkedNotebookGuid(m_remoteTagLinkedNotebookGuid);
    tag.setDirty(false);
    tag.setLocal(false);

    // Clearing the parent local uid info: if this tag has parent guid,
    // the parent local uid would be complemented by the local storage;
    // otherwise the parent would be removed from this tag
    tag.setParentLocalUid(QString());

    m_updateTagRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update tag: request id = ")
            << m_updateTagRequestId << QStringLiteral(", tag: ") << tag);
    Q_EMIT updateTag(tag, m_updateTagRequestId);
}

void TagSyncConflictResolver::renameConflictingLocalTag(const Tag & localConflict)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::renameConflictingLocalTag: local conflict = ") << localConflict);

    QString name = (localConflict.hasName() ? localConflict.name() : m_remoteTag.name.ref());

    const QHash<QString,QString> & guidByNameHash = m_cache.guidByNameHash();

    QString conflictingName = name + QStringLiteral(" - ") + tr("conflicting");

    int suffix = 1;
    QString currentName = conflictingName;
    auto it = guidByNameHash.find(currentName.toLower());
    while(it != guidByNameHash.end()) {
        currentName = conflictingName + QStringLiteral(" (") + QString::number(suffix) + QStringLiteral(")");
        ++suffix;
        it = guidByNameHash.find(currentName.toLower());
    }

    conflictingName = currentName;

    Tag tag(localConflict);
    tag.setName(conflictingName);
    tag.setDirty(true);

    m_updateTagRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update tag: request id = ")
            << m_updateTagRequestId << QStringLiteral(", tag: ") << tag);
    Q_EMIT updateTag(tag, m_updateTagRequestId);
}

} // namespace quentier
