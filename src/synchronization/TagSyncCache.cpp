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

#include "TagSyncCache.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#define __TCLOG_BASE(message, level)                                           \
    if (m_linkedNotebookGuid.isEmpty()) {                                      \
        __QNLOG_BASE("synchronization:tag_cache", message, level);             \
    }                                                                          \
    else {                                                                     \
        __QNLOG_BASE(                                                          \
            "synchronization:tag_cache",                                       \
            "[linked notebook " << m_linkedNotebookGuid << "]: " << message,   \
            level);                                                            \
    }

#define TCTRACE(message) __TCLOG_BASE(message, Trace)

#define TCDEBUG(message) __TCLOG_BASE(message, Debug)

#define TCWARNING(message) __TCLOG_BASE(message, Warning)

namespace quentier {

TagSyncCache::TagSyncCache(
    LocalStorageManagerAsync & localStorageManagerAsync,
    QString linkedNotebookGuid, QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_linkedNotebookGuid(std::move(linkedNotebookGuid))
{}

void TagSyncCache::clear()
{
    TCDEBUG("TagSyncCache::clear");

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
    TCDEBUG("TagSyncCache::fill");

    if (m_connectedToLocalStorage) {
        TCDEBUG(
            "Already connected to the local storage, "
            << "no need to do anything");
        return;
    }

    connectToLocalStorage();
    requestTagsList();
}

void TagSyncCache::onListTagsComplete(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListTagsOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, QList<Tag> foundTags, QUuid requestId)
{
    if (requestId != m_listTagsRequestId) {
        return;
    }

    TCDEBUG(
        "TagSyncCache::onListTagsComplete: flag = "
        << flag << ", limit = " << limit << ", offset = " << offset
        << ", order = " << order << ", order direction = " << orderDirection
        << ", linked notebook guid = " << linkedNotebookGuid
        << ", request id = " << requestId);

    for (const auto & tag: qAsConst(foundTags)) {
        processTag(tag);
    }

    m_listTagsRequestId = QUuid();

    if (foundTags.size() == static_cast<int>(limit)) {
        TCTRACE(
            "The number of found tags matches the limit, "
            << "requesting more tags from the local storage");
        m_offset += limit;
        requestTagsList();
        return;
    }

    Q_EMIT filled();
}

void TagSyncCache::onListTagsFailed(
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListTagsOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listTagsRequestId) {
        return;
    }

    TCDEBUG(
        "TagSyncCache::onListTagsFailed: flag = "
        << flag << ", limit = " << limit << ", offset = " << offset
        << ", order = " << order << ", order direction = " << orderDirection
        << ", linked notebook guid = " << linkedNotebookGuid
        << ", error description = " << errorDescription
        << ", request id = " << requestId);

    TCWARNING(
        "Failed to cache the tag information required for the sync: "
        << errorDescription);

    m_tagNameByLocalUid.clear();
    m_tagNameByGuid.clear();
    m_tagGuidByName.clear();
    m_dirtyTagsByGuid.clear();
    disconnectFromLocalStorage();

    Q_EMIT failure(errorDescription);
}

void TagSyncCache::onAddTagComplete(Tag tag, QUuid requestId)
{
    TCDEBUG(
        "TagSyncCache::onAddTagComplete: request id = " << requestId
                                                        << ", tag: " << tag);

    processTag(tag);
}

void TagSyncCache::onUpdateTagComplete(Tag tag, QUuid requestId)
{
    TCDEBUG(
        "TagSyncCache::onUpdateTagComplete: request id = " << requestId
                                                           << ", tag: " << tag);

    removeTag(tag.localUid());
    processTag(tag);
}

void TagSyncCache::onExpungeTagComplete(
    Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    TCDEBUG(
        "TagSyncCache::onExpungeTagComplete: request id = "
        << requestId << ", expunged child tag local uids: "
        << expungedChildTagLocalUids.join(QStringLiteral(", "))
        << ", tag: " << tag);

    removeTag(tag.localUid());

    for (const auto & tagLocalUid: qAsConst(expungedChildTagLocalUids)) {
        removeTag(tagLocalUid);
    }
}

void TagSyncCache::connectToLocalStorage()
{
    TCDEBUG("TagSyncCache::connectToLocalStorage");

    if (m_connectedToLocalStorage) {
        TCDEBUG("Already connected to the local storage");
        return;
    }

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &TagSyncCache::listTags, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListTagsRequest,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listTagsComplete, this,
        &TagSyncCache::onListTagsComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::listTagsFailed,
        this, &TagSyncCache::onListTagsFailed,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &TagSyncCache::onAddTagComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateTagComplete, this,
        &TagSyncCache::onUpdateTagComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagComplete, this,
        &TagSyncCache::onExpungeTagComplete,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_connectedToLocalStorage = true;
}

void TagSyncCache::disconnectFromLocalStorage()
{
    TCDEBUG("TagSyncCache::disconnectFromLocalStorage");

    if (!m_connectedToLocalStorage) {
        TCDEBUG("Not connected to local storage at the moment");
        return;
    }

    // Disconnect local signals from local storage manager async's slots
    QObject::disconnect(
        this, &TagSyncCache::listTags, &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onListTagsRequest);

    // Disconnect local storage manager async's signals from local slots
    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::listTagsComplete, this,
        &TagSyncCache::onListTagsComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::listTagsFailed,
        this, &TagSyncCache::onListTagsFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &TagSyncCache::onAddTagComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateTagComplete, this,
        &TagSyncCache::onUpdateTagComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagComplete, this,
        &TagSyncCache::onExpungeTagComplete);

    m_connectedToLocalStorage = false;
}

void TagSyncCache::requestTagsList()
{
    TCDEBUG("TagSyncCache::requestTagsList");

    m_listTagsRequestId = QUuid::createUuid();

    TCTRACE(
        "Emitting the request to list tags: request id = "
        << m_listTagsRequestId << ", offset = " << m_offset);

    Q_EMIT listTags(
        LocalStorageManager::ListObjectsOption::ListAll, m_limit, m_offset,
        LocalStorageManager::ListTagsOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending, m_linkedNotebookGuid,
        m_listTagsRequestId);
}

void TagSyncCache::removeTag(const QString & tagLocalUid)
{
    TCDEBUG("TagSyncCache::removeTag: local uid = " << tagLocalUid);

    auto localUidIt = m_tagNameByLocalUid.find(tagLocalUid);
    if (Q_UNLIKELY(localUidIt == m_tagNameByLocalUid.end())) {
        TCDEBUG("The tag name was not found in the cache by local uid");
        return;
    }

    QString name = localUidIt.value();
    Q_UNUSED(m_tagNameByLocalUid.erase(localUidIt))

    auto guidIt = m_tagGuidByName.find(name);
    if (Q_UNLIKELY(guidIt == m_tagGuidByName.end())) {
        TCDEBUG("The tag guid was not found in the cache by name");
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
        TCDEBUG("The tag name was not found in the cache by guid");
        return;
    }

    Q_UNUSED(m_tagNameByGuid.erase(nameIt))
}

void TagSyncCache::processTag(const Tag & tag)
{
    TCDEBUG("TagSyncCache::processTag: " << tag);

    if (tag.hasGuid()) {
        if (tag.isDirty()) {
            m_dirtyTagsByGuid[tag.guid()] = tag;
        }
        else {
            auto it = m_dirtyTagsByGuid.find(tag.guid());
            if (it != m_dirtyTagsByGuid.end()) {
                Q_UNUSED(m_dirtyTagsByGuid.erase(it))
            }
        }
    }

    if (!tag.hasName()) {
        TCDEBUG("Skipping the tag without a name");
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
