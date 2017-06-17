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

#include "TagSyncConflictResolutionCache.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

TagSyncConflictResolutionCache::TagSyncConflictResolutionCache(LocalStorageManagerAsync & localStorageManagerAsync) :
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_tagNameByLocalUid(),
    m_tagNameByGuid(),
    m_tagGuidByName(),
    m_listTagsRequestId(),
    m_limit(50),
    m_offset(0)
{}

bool TagSyncConflictResolutionCache::isFilled() const
{
    if (!m_connectedToLocalStorage) {
        return false;
    }

    if (m_listTagsRequestId.isNull()) {
        return true;
    }

    return false;
}

void TagSyncConflictResolutionCache::fill()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::fill"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage, no need to do anything"));
        return;
    }

    connectToLocalStorage();
    requestTagsList();
}

void TagSyncConflictResolutionCache::onListTagsComplete(LocalStorageManager::ListObjectsOptions flag,
                                                        size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                                                        LocalStorageManager::OrderDirection::type orderDirection,
                                                        QString linkedNotebookGuid, QList<Tag> foundTags, QUuid requestId)
{
    if (requestId != m_listTagsRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::onListTagsComplete: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", request id = ") << requestId);

    for(auto it = foundTags.constBegin(), end = foundTags.constEnd(); it != end; ++it) {
        processTag(*it);
    }

    m_listTagsRequestId = QUuid();

    if (foundTags.size() == static_cast<int>(limit)) {
        QNTRACE(QStringLiteral("The number of found tags matches the limit, requesting more tags from the local storage"));
        m_offset += limit;
        requestTagsList();
        return;
    }

    emit filled();
}

void TagSyncConflictResolutionCache::onListTagsFailed(LocalStorageManager::ListObjectsOptions flag,
                                                      size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                                                      LocalStorageManager::OrderDirection::type orderDirection,
                                                      QString linkedNotebookGuid, ErrorString errorDescription,
                                                      QUuid requestId)
{
    if (requestId != m_listTagsRequestId) {
        return;
    }

    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::onListTagsFailed: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", request id = ") << requestId);

    QNWARNING(QStringLiteral("Failed to cache the tag information required for the sync conflicts resolution: ")
              << errorDescription);

    m_tagNameByLocalUid.clear();
    m_tagNameByGuid.clear();
    m_tagGuidByName.clear();
    disconnectFromLocalStorage();

    emit failure(errorDescription);
}

void TagSyncConflictResolutionCache::onAddTagComplete(Tag tag, QUuid requestId)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::onAddTagComplete: request id = ")
            << requestId << QStringLiteral(", tag: ") << tag);

    processTag(tag);
}

void TagSyncConflictResolutionCache::onUpdateTagComplete(Tag tag, QUuid requestId)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::onUpdateTagComplete: request id = ")
            << requestId << QStringLiteral(", tag: ") << tag);

    removeTag(tag.localUid());
    processTag(tag);
}

void TagSyncConflictResolutionCache::onExpungeTagComplete(Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::onExpungeTagComplete: request id = ")
            << requestId << QStringLiteral(", expunged child tag local uids: ")
            << expungedChildTagLocalUids.join(QStringLiteral(", ")) << QStringLiteral(", tag: ") << tag);

    removeTag(tag.localUid());

    for(auto it = expungedChildTagLocalUids.constBegin(), end = expungedChildTagLocalUids.constEnd(); it != end; ++it) {
        removeTag(*it);
    }
}

void TagSyncConflictResolutionCache::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(this,
                     QNSIGNAL(TagSyncConflictResolutionCache,listTags,LocalStorageManager::ListObjectsOptions,
                              size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,QString,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListTagsRequest,LocalStorageManager::ListObjectsOptions,
                            size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,QString,QUuid));

    // Connect local storage manager async's signals to local slots
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listTagsComplete,LocalStorageManager::ListObjectsOptions,
                              size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QList<Tag>,QUuid),
                     this,
                     QNSLOT(TagSyncConflictResolutionCache,onListTagsComplete,LocalStorageManager::ListObjectsOptions,
                            size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QList<Tag>,QUuid));
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listTagsFailed,LocalStorageManager::ListObjectsOptions,
                              size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(TagSyncConflictResolutionCache,onListTagsFailed,LocalStorageManager::ListObjectsOptions,
                            size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncConflictResolutionCache,onAddTagComplete,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncConflictResolutionCache,onUpdateTagComplete,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QStringList,QUuid),
                     this, QNSLOT(TagSyncConflictResolutionCache,onExpungeTagComplete,Tag,QStringList,QUuid));

    m_connectedToLocalStorage = true;
}

void TagSyncConflictResolutionCache::disconnectFromLocalStorage()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(this,
                        QNSIGNAL(TagSyncConflictResolutionCache,listTags,LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                                 LocalStorageManager::OrderDirection::type,QString,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListTagsRequest,LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,QString,QUuid));

    // Disconnect local storage manager async's signals from local slots
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listTagsComplete,LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QList<Tag>,QUuid),
                        this,
                        QNSLOT(TagSyncConflictResolutionCache,onListTagsComplete,LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QList<Tag>,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listTagsFailed,LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,ErrorString,QUuid),
                        this,
                        QNSLOT(TagSyncConflictResolutionCache,onListTagsFailed,LocalStorageManager::ListObjectsOptions,
                               size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                        this, QNSLOT(TagSyncConflictResolutionCache,onAddTagComplete,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                        this, QNSLOT(TagSyncConflictResolutionCache,onUpdateTagComplete,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QUuid),
                        this, QNSLOT(TagSyncConflictResolutionCache,onExpungeTagComplete,Tag,QUuid));

    m_connectedToLocalStorage = false;
}

void TagSyncConflictResolutionCache::requestTagsList()
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::requestTagsList"));

    m_listTagsRequestId = QUuid::createUuid();

    QNTRACE(QStringLiteral("Emitting the request to list tags: request id = ")
            << m_listTagsRequestId << QStringLiteral(", offset = ") << m_offset);
    emit listTags(LocalStorageManager::ListObjectsOption::ListAll,
                  m_limit, m_offset, LocalStorageManager::ListTagsOrder::NoOrder,
                  LocalStorageManager::OrderDirection::Ascending,
                  QString(), m_listTagsRequestId);
}

void TagSyncConflictResolutionCache::removeTag(const QString & tagLocalUid)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::removeTag: local uid = ") << tagLocalUid);

    auto localUidIt = m_tagNameByLocalUid.find(tagLocalUid);
    if (Q_UNLIKELY(localUidIt == m_tagNameByLocalUid.end())) {
        QNDEBUG(QStringLiteral("The tag name was not found in the cache by local uid"));
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_tagNameByLocalUid.erase(localUidIt))

    auto guidIt = m_tagGuidByName.find(name);
    if (Q_UNLIKELY(guidIt == m_tagGuidByName.end())) {
        QNDEBUG(QStringLiteral("The tag guid was not found in the cache by name"));
        return;
    }

    QString guid = guidIt.value();
    Q_UNUSED(m_tagGuidByName.erase(guidIt))

    auto nameIt = m_tagNameByGuid.find(guid);
    if (Q_UNLIKELY(nameIt == m_tagNameByGuid.end())) {
        QNDEBUG(QStringLiteral("The tag name was not found in the cache by guid"));
        return;
    }

    Q_UNUSED(m_tagNameByGuid.erase(nameIt))
}

void TagSyncConflictResolutionCache::processTag(const Tag & tag)
{
    QNDEBUG(QStringLiteral("TagSyncConflictResolutionCache::processTag: ") << tag);

    if (!tag.hasName()) {
        QNDEBUG(QStringLiteral("Skipping the tag without a name"));
        return;
    }

    QString name = tag.name().toLower();
    m_tagNameByLocalUid[tag.localUid()] = name;

    if (!tag.hasGuid()) {
        return;
    }

    const QString & guid = tag.guid();
    m_tagNameByGuid[guid] = name;
    m_tagGuidByName[name] = guid;
}

} // namespace quentier
