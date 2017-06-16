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
#include "TagSyncConflictResolutionCache.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

TagSyncConflictResolver::TagSyncConflictResolver(const qevercloud::Tag & remoteTag,
                                                 const Tag & localConflict,
                                                 TagSyncConflictResolutionCache & cache,
                                                 LocalStorageManagerAsync & localStorageManagerAsync,
                                                 QObject * parent) :
    QObject(parent),
    m_cache(cache),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_remoteTag(remoteTag),
    m_localConflict(localConflict),
    m_state(State::Undefined),
    m_addTagRequestId(),
    m_updateTagRequestId(),
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
        emit failure(m_remoteTag, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteTag.name.isSet())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local tags: "
                                     "the remote tag has no guid set"));
        QNWARNING(error << QStringLiteral(": ") << m_remoteTag);
        emit failure(m_remoteTag, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.hasGuid() && !m_localConflict.hasName())) {
        ErrorString error(QT_TR_NOOP("Can't resolve the conflict between remote and local notebooks: "
                                     "the local conflicting tag has neither guid nor name set"));
        QNWARNING(error << QStringLiteral(": ") << m_localConflict);
        emit failure(m_remoteTag, error);
        return;
    }

    connectToLocalStorage();

    if (m_localConflict.hasName() && (m_localConflict.name() == m_remoteTag.name.ref())) {
        processTagsConflictByName();
    }
    else {
        overrideLocalChangesWithRemoteChanges();
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
        emit finished(m_remoteTag);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the confirmation about the tag addition "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", tag: ") << tag);
        emit failure(m_remoteTag, error);
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

    emit failure(m_remoteTag, errorDescription);
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
        emit finished(m_remoteTag);
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
            emit failure(m_remoteTag, error);
            return;
        }

        m_state = State::PendingRemoteTagAdoptionInLocalStorage;

        const QHash<QString,QString> & nameByGuidHash = m_cache.nameByGuidHash();
        auto it = nameByGuidHash.find(m_remoteTag.guid.ref());
        if (it == nameByGuidHash.end())
        {
            QNDEBUG(QStringLiteral("Found no duplicate of the remote tag by guid, adding new tag to the local storage"));

            Tag tag(m_remoteTag);
            tag.setDirty(false);
            tag.setLocal(false);

            m_addTagRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to add tag: request id = ") << m_addTagRequestId
                    << QStringLiteral(", tag: ") << tag);
            emit addTag(tag, m_addTagRequestId);
        }
        else
        {
            QNDEBUG(QStringLiteral("The duplicate by guid exists in the local storage, updating it with the state "
                                   "of the remote tag"));

            Tag tag(m_remoteTag);
            tag.setDirty(false);
            tag.setLocal(false);

            m_updateTagRequestId = QUuid::createUuid();
            QNTRACE(QStringLiteral("Emitting the request to update tag: request id = ") << m_updateTagRequestId
                    << QStringLiteral(", tag: ") << tag);
            emit updateTag(tag, m_updateTagRequestId);
        }
    }
    else if (m_state == State::PendingRemoteTagAdoptionInLocalStorage)
    {
        QNDEBUG(QStringLiteral("Successfully finalized the sequence of actions required for resolving the conflict of tags"));
        emit finished(m_remoteTag);
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the confirmation about the tag update "
                                     "from the local storage"));
        QNWARNING(error << QStringLiteral(", tag: ") << tag);
        emit failure(m_remoteTag, error);
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

    emit failure(m_remoteTag, errorDescription);
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
        renameConflictingLocalTag();
    }
    else
    {
        ErrorString error(QT_TR_NOOP("Internal error: wrong state on receiving the tag info cache filling notification"));
        QNWARNING(error << QStringLiteral(", state = ") << m_state);
        emit failure(m_remoteTag, error);
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
    emit failure(m_remoteTag, errorDescription);
}

void TagSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::connectToLocalStorage"));

    // Connect local signals to local storage manager async's slots
    QObject::connect(this, QNSIGNAL(TagSyncConflictResolver,addTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid));
    QObject::connect(this, QNSIGNAL(TagSyncConflictResolver,updateTag,Tag,QUuid),
                     &m_localStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onAddTagComplete,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onAddTagFailed,Tag,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onUpdateTagComplete,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,Tag,ErrorString,QUuid),
                     this, QNSLOT(TagSyncConflictResolver,onUpdateTagFailed,Tag,ErrorString,QUuid));
}

void TagSyncConflictResolver::processTagsConflictByName()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::processTagsConflictByName"));

    if (m_localConflict.hasGuid() && (m_localConflict.guid() == m_remoteTag.guid.ref())) {
        QNDEBUG(QStringLiteral("The conflicting tags match by name and guid => the changes from the remote "
                               "tag should just override the local changes"));
        overrideLocalChangesWithRemoteChanges();
        return;
    }

    QNDEBUG(QStringLiteral("The conflicting tags match by name but not by guid => should rename "
                           "the local conflicting tag to \"free\" the name it occupies"));

    m_state = State::PendingConflictingTagRenaming;

    if (!m_cache.isFilled())
    {
        QNDEBUG(QStringLiteral("The cache of tag info has not been filled yet"));

        QObject::connect(&m_cache, QNSIGNAL(TagSyncConflictResolutionCache,filled),
                         this, QNSLOT(TagSyncConflictResolver,onCacheFilled));
        QObject::connect(&m_cache, QNSIGNAL(TagSyncConflictResolutionCache,failure,ErrorString),
                         this, QNSLOT(TagSyncConflictResolver,onCacheFailed,ErrorString));
        QObject::connect(this, QNSIGNAL(TagSyncConflictResolver,fillTagsCache),
                         &m_cache, QNSLOT(TagSyncConflictResolutionCache,fill));

        m_pendingCacheFilling = true;
        QNTRACE(QStringLiteral("Emitting the request to fill the tags cache"));
        emit fillTagsCache();
        return;
    }

    QNDEBUG(QStringLiteral("The cache of notebook info has already been filled"));
    renameConflictingLocalTag();
}

void TagSyncConflictResolver::overrideLocalChangesWithRemoteChanges()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::overrideLocalChangesWithRemoteChanges"));

    m_state = State::OverrideLocalChangesWithRemoteChanges;

    Tag tag(m_localConflict);
    tag = m_remoteTag;
    tag.setDirty(false);
    tag.setLocal(false);

    m_updateTagRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update tag: request id = ")
            << m_updateTagRequestId << QStringLiteral(", tag: ") << tag);
    emit updateTag(tag, m_updateTagRequestId);
}

void TagSyncConflictResolver::renameConflictingLocalTag()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolver::renameConflictingLocalTag"));

    QString name = (m_localConflict.hasName() ? m_localConflict.name() : m_remoteTag.name.ref());

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

    Tag tag(m_localConflict);
    tag.setName(conflictingName);
    tag.setDirty(true);

    m_updateTagRequestId = QUuid::createUuid();
    QNTRACE(QStringLiteral("Emitting the request to update tag: request id = ")
            << m_updateTagRequestId << QStringLiteral(", tag: ") << tag);
    emit updateTag(tag, m_updateTagRequestId);
}

} // namespace quentier
