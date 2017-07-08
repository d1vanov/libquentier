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

#include "TagSyncCache.h"
#include <quentier/logging/QuentierLogger.h>

#define __TCLOG_BASE(message, level) \
    if (m_linkedNotebookGuid.isEmpty()) { \
        __QNLOG_BASE(message, level); \
    } \
    else { \
        __QNLOG_BASE(QStringLiteral("[linked notebook ") << m_linkedNotebookGuid << QStringLiteral("]: ") << message, level); \
    }

#define TCTRACE(message) \
    __TCLOG_BASE(message, Trace)

#define TCDEBUG(message) \
    __TCLOG_BASE(message, Debug)

#define TCWARNING(message) \
    __TCLOG_BASE(message, Warn)

namespace quentier {

TagSyncCache::TagSyncCache(LocalStorageManagerAsync & localStorageManagerAsync,
                           const QString & linkedNotebookGuid, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_linkedNotebookGuid(linkedNotebookGuid),
    m_tagNameByLocalUid(),
    m_tagNameByGuid(),
    m_tagGuidByName(),
    m_dirtyTagsByGuid(),
    m_listTagsRequestId(),
    m_limit(50),
    m_offset(0)
{}

void TagSyncCache::clear()
{
    TCDEBUG(QStringLiteral("TagSyncCache::clear"));

    disconnectFromLocalStorage();

    m_tagNameByLocalUid.clear();
    m_tagNameByGuid.clear();
    m_tagGuidByName.clear();
    m_dirtyTagsByGuid.clear();
    m_listTagsRequestId = QUuid();
    m_offset = 0;
}

bool TagSyncCache::isFilled() const
{
    if (!m_connectedToLocalStorage) {
        return false;
    }

    if (m_listTagsRequestId.isNull()) {
        return true;
    }

    return false;
}

void TagSyncCache::fill()
{
    TCDEBUG(QStringLiteral("TagSyncCache::fill"));

    if (m_connectedToLocalStorage) {
        TCDEBUG(QStringLiteral("Already connected to the local storage, no need to do anything"));
        return;
    }

    connectToLocalStorage();
    requestTagsList();
}

void TagSyncCache::onListTagsComplete(LocalStorageManager::ListObjectsOptions flag,
                                      size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                                      LocalStorageManager::OrderDirection::type orderDirection,
                                      QString linkedNotebookGuid, QList<Tag> foundTags, QUuid requestId)
{
    if (requestId != m_listTagsRequestId) {
        return;
    }

    TCDEBUG(QStringLiteral("TagSyncCache::onListTagsComplete: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", request id = ") << requestId);

    for(auto it = foundTags.constBegin(), end = foundTags.constEnd(); it != end; ++it) {
        processTag(*it);
    }

    m_listTagsRequestId = QUuid();

    if (foundTags.size() == static_cast<int>(limit)) {
        TCTRACE(QStringLiteral("The number of found tags matches the limit, requesting more tags from the local storage"));
        m_offset += limit;
        requestTagsList();
        return;
    }

    emit filled();
}

void TagSyncCache::onListTagsFailed(LocalStorageManager::ListObjectsOptions flag,
                                    size_t limit, size_t offset, LocalStorageManager::ListTagsOrder::type order,
                                    LocalStorageManager::OrderDirection::type orderDirection,
                                    QString linkedNotebookGuid, ErrorString errorDescription,
                                    QUuid requestId)
{
    if (requestId != m_listTagsRequestId) {
        return;
    }

    TCDEBUG(QStringLiteral("TagSyncCache::onListTagsFailed: flag = ")
            << flag << QStringLiteral(", limit = ") << limit << QStringLiteral(", offset = ")
            << offset << QStringLiteral(", order = ") << order << QStringLiteral(", order direction = ")
            << orderDirection << QStringLiteral(", linked notebook guid = ") << linkedNotebookGuid
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", request id = ") << requestId);

    TCWARNING(QStringLiteral("Failed to cache the tag information required for the sync: ") << errorDescription);

    m_tagNameByLocalUid.clear();
    m_tagNameByGuid.clear();
    m_tagGuidByName.clear();
    m_dirtyTagsByGuid.clear();
    disconnectFromLocalStorage();

    emit failure(errorDescription);
}

void TagSyncCache::onAddTagComplete(Tag tag, QUuid requestId)
{
    TCDEBUG(QStringLiteral("TagSyncCache::onAddTagComplete: request id = ")
            << requestId << QStringLiteral(", tag: ") << tag);

    processTag(tag);
}

void TagSyncCache::onUpdateTagComplete(Tag tag, QUuid requestId)
{
    TCDEBUG(QStringLiteral("TagSyncCache::onUpdateTagComplete: request id = ")
            << requestId << QStringLiteral(", tag: ") << tag);

    removeTag(tag.localUid());
    processTag(tag);
}

void TagSyncCache::onExpungeTagComplete(Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    TCDEBUG(QStringLiteral("TagSyncCache::onExpungeTagComplete: request id = ")
            << requestId << QStringLiteral(", expunged child tag local uids: ")
            << expungedChildTagLocalUids.join(QStringLiteral(", ")) << QStringLiteral(", tag: ") << tag);

    removeTag(tag.localUid());

    for(auto it = expungedChildTagLocalUids.constBegin(), end = expungedChildTagLocalUids.constEnd(); it != end; ++it) {
        removeTag(*it);
    }
}

void TagSyncCache::connectToLocalStorage()
{
    TCDEBUG(QStringLiteral("TagSyncCache::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        TCDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(this,
                     QNSIGNAL(TagSyncCache,listTags,LocalStorageManager::ListObjectsOptions,
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
                     QNSLOT(TagSyncCache,onListTagsComplete,LocalStorageManager::ListObjectsOptions,
                            size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QList<Tag>,QUuid));
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listTagsFailed,LocalStorageManager::ListObjectsOptions,
                              size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(TagSyncCache,onListTagsFailed,LocalStorageManager::ListObjectsOptions,
                            size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncCache,onAddTagComplete,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this, QNSLOT(TagSyncCache,onUpdateTagComplete,Tag,QUuid));
    QObject::connect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QStringList,QUuid),
                     this, QNSLOT(TagSyncCache,onExpungeTagComplete,Tag,QStringList,QUuid));

    m_connectedToLocalStorage = true;
}

void TagSyncCache::disconnectFromLocalStorage()
{
    TCDEBUG(QStringLiteral("TagSyncCache::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        TCDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(this,
                        QNSIGNAL(TagSyncCache,listTags,LocalStorageManager::ListObjectsOptions,
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
                        QNSLOT(TagSyncCache,onListTagsComplete,LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QList<Tag>,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listTagsFailed,LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,ErrorString,QUuid),
                        this,
                        QNSLOT(TagSyncCache,onListTagsFailed,LocalStorageManager::ListObjectsOptions,
                               size_t,size_t, LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                        this, QNSLOT(TagSyncCache,onAddTagComplete,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                        this, QNSLOT(TagSyncCache,onUpdateTagComplete,Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,Tag,QUuid),
                        this, QNSLOT(TagSyncCache,onExpungeTagComplete,Tag,QUuid));

    m_connectedToLocalStorage = false;
}

void TagSyncCache::requestTagsList()
{
    TCDEBUG(QStringLiteral("TagSyncCache::requestTagsList"));

    m_listTagsRequestId = QUuid::createUuid();

    TCTRACE(QStringLiteral("Emitting the request to list tags: request id = ")
            << m_listTagsRequestId << QStringLiteral(", offset = ") << m_offset);
    emit listTags(LocalStorageManager::ListAll,
                  m_limit, m_offset, LocalStorageManager::ListTagsOrder::NoOrder,
                  LocalStorageManager::OrderDirection::Ascending,
                  m_linkedNotebookGuid, m_listTagsRequestId);
}

void TagSyncCache::removeTag(const QString & tagLocalUid)
{
    TCDEBUG(QStringLiteral("TagSyncCache::removeTag: local uid = ") << tagLocalUid);

    auto localUidIt = m_tagNameByLocalUid.find(tagLocalUid);
    if (Q_UNLIKELY(localUidIt == m_tagNameByLocalUid.end())) {
        TCDEBUG(QStringLiteral("The tag name was not found in the cache by local uid"));
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_tagNameByLocalUid.erase(localUidIt))

    auto guidIt = m_tagGuidByName.find(name);
    if (Q_UNLIKELY(guidIt == m_tagGuidByName.end())) {
        TCDEBUG(QStringLiteral("The tag guid was not found in the cache by name"));
        return;
    }

    QString guid = guidIt.value();
    Q_UNUSED(m_tagGuidByName.erase(guidIt))

    auto dirtyTagIt = m_dirtyTagsByGuid.find(guid);
    if (dirtyTagIt != m_dirtyTagsByGuid.end()) {
        Q_UNUSED(m_dirtyTagsByGuid.erase(dirtyTagIt))
    }

    auto nameIt = m_tagNameByGuid.find(guid);
    if (Q_UNLIKELY(nameIt == m_tagNameByGuid.end())) {
        TCDEBUG(QStringLiteral("The tag name was not found in the cache by guid"));
        return;
    }

    Q_UNUSED(m_tagNameByGuid.erase(nameIt))
}

void TagSyncCache::processTag(const Tag & tag)
{
    TCDEBUG(QStringLiteral("TagSyncCache::processTag: ") << tag);

    if (tag.hasGuid())
    {
        if (tag.isDirty())
        {
            m_dirtyTagsByGuid[tag.guid()] = tag;
        }
        else
        {
            auto it = m_dirtyTagsByGuid.find(tag.guid());
            if (it != m_dirtyTagsByGuid.end()) {
                Q_UNUSED(m_dirtyTagsByGuid.erase(it))
            }
        }
    }

    if (!tag.hasName()) {
        TCDEBUG(QStringLiteral("Skipping the tag without a name"));
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
