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

#include "FullSyncStaleDataItemsExpunger.h"

#include "NotebookSyncCache.h"
#include "SavedSearchSyncCache.h"
#include "TagSyncCache.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#include <QStringList>

#define __FELOG_BASE(message, level)                                           \
    if (m_linkedNotebookGuid.isEmpty()) {                                      \
        __QNLOG_BASE(                                                          \
            "synchronization:full_sync_stale_expunge", message, level);        \
    }                                                                          \
    else {                                                                     \
        __QNLOG_BASE(                                                          \
            "synchronization:full_sync_stale_expunge",                         \
            "[linked notebook " << m_linkedNotebookGuid << "]: " << message,   \
            level);                                                            \
    }

#define FETRACE(message) __FELOG_BASE(message, Trace)

#define FEDEBUG(message) __FELOG_BASE(message, Debug)

#define FEWARNING(message) __FELOG_BASE(message, Warning)

namespace quentier {

FullSyncStaleDataItemsExpunger::FullSyncStaleDataItemsExpunger(
    LocalStorageManagerAsync & localStorageManagerAsync,
    NotebookSyncCache & notebookSyncCache, TagSyncCache & tagSyncCache,
    SavedSearchSyncCache & savedSearchSyncCache,
    const SyncedGuids & syncedGuids, const QString & linkedNotebookGuid,
    QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_pNotebookSyncCache(&notebookSyncCache), m_pTagSyncCache(&tagSyncCache),
    m_pSavedSearchSyncCache(&savedSearchSyncCache),
    m_noteSyncCache(localStorageManagerAsync, linkedNotebookGuid),
    m_syncedGuids(syncedGuids), m_linkedNotebookGuid(linkedNotebookGuid)
{}

void FullSyncStaleDataItemsExpunger::start()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::start");

    if (m_inProgress) {
        FEDEBUG("Already started");
        return;
    }

    m_inProgress = true;

    checkAndRequestCachesFilling();
    if (pendingCachesFilling()) {
        FEDEBUG("Pending caches filling");
        return;
    }

    analyzeDataAndSendRequestsOrResult();
}

void FullSyncStaleDataItemsExpunger::onNotebookCacheFilled()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::onNotebookCacheFilled");

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG("Not in progress at the moment");
        return;
    }

    m_pendingNotebookSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onTagCacheFilled()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::onTagCacheFilled");

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG("Not in progress at the moment");
        return;
    }

    m_pendingTagSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onSavedSearchCacheFilled()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::onSavedSearchCacheFilled");

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG("Not in progress at the moment");
        return;
    }

    m_pendingSavedSearchSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onNoteCacheFilled()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::onNoteCacheFilled");

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG("Not in progress at the moment");
        return;
    }

    m_pendingNoteSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onExpungeNotebookComplete(
    Notebook notebook, QUuid requestId)
{
    auto it = m_expungeNotebookRequestIds.find(requestId);
    if (it == m_expungeNotebookRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeNotebookComplete: "
        << "request id = " << requestId << ", notebook: " << notebook);

    Q_UNUSED(m_expungeNotebookRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_expungeNotebookRequestIds.find(requestId);
    if (it == m_expungeNotebookRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeNotebookFailed: "
        << "request id = " << requestId << ", error description = "
        << errorDescription << ", notebook: " << notebook);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onExpungeTagComplete(
    Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    auto it = m_expungeTagRequestIds.find(requestId);
    if (it == m_expungeTagRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeTagComplete: "
        << "request id = " << requestId << ", tag: " << tag
        << "\nExpunged child tag local uids: "
        << expungedChildTagLocalUids.join(QStringLiteral(", ")));

    Q_UNUSED(m_expungeTagRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_expungeTagRequestIds.find(requestId);
    if (it == m_expungeTagRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeTagFailed: "
        << "request id = " << requestId
        << ", error description = " << errorDescription << ", tag: " << tag);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onExpungeSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    auto it = m_expungeSavedSearchRequestIds.find(requestId);
    if (it == m_expungeSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeSavedSearchComplete: "
        << "request id = " << requestId << ", saved search: " << search);

    Q_UNUSED(m_expungeSavedSearchRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_expungeSavedSearchRequestIds.find(requestId);
    if (it == m_expungeSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeSavedSearchFailed: "
        << "request id = " << requestId << ", error description = "
        << errorDescription << ", search: " << search);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onExpungeNoteComplete(
    Note note, QUuid requestId)
{
    auto it = m_expungeNoteRequestIds.find(requestId);
    if (it == m_expungeNoteRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeNoteComplete: "
        << "request id = " << requestId << ", note: " << note);

    Q_UNUSED(m_expungeNoteRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeNoteFailed(
    Note note, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_expungeNoteRequestIds.find(requestId);
    if (it == m_expungeNoteRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onExpungeNoteFailed: "
        << "request id = " << requestId
        << ", error description = " << errorDescription << ", note: " << note);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateNotebookComplete(
    Notebook notebook, QUuid requestId)
{
    auto it = m_updateNotebookRequestId.find(requestId);
    if (it == m_updateNotebookRequestId.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateNotebookComplete: "
        << "request id = " << requestId << ", notebook: " << notebook);

    Q_UNUSED(m_updateNotebookRequestId.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onUpdateNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateNotebookRequestId.find(requestId);
    if (it == m_updateNotebookRequestId.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateNotebookFailed: "
        << "request id = " << requestId << ", error description = "
        << errorDescription << ", notebook: " << notebook);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateTagComplete(
    Tag tag, QUuid requestId)
{
    auto it = m_updateTagRequestIds.find(requestId);
    if (it == m_updateTagRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateTagComplete: "
        << "request id = " << requestId << ", tag: " << tag);

    Q_UNUSED(m_updateTagRequestIds.erase(it))
    checkTagUpdatesCompletionAndSendExpungeTagRequests();
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onUpdateTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateTagRequestIds.find(requestId);
    if (it == m_updateTagRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateTagFailed: "
        << "request id = " << requestId
        << ", error description = " << errorDescription << ", tag: " << tag);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateSavedSearchComplete(
    SavedSearch search, QUuid requestId)
{
    auto it = m_updateSavedSearchRequestIds.find(requestId);
    if (it == m_updateSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateSavedSearchComplete: "
        << "request id = " << requestId << ", saved search: " << search);

    Q_UNUSED(m_updateSavedSearchRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onUpdateSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateSavedSearchRequestIds.find(requestId);
    if (it == m_updateSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateSavedSearchFailed: "
        << "request id = " << requestId << ", error description = "
        << errorDescription << ", saved search: " << search);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateNoteComplete(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateNoteComplete: "
        << "request id = " << requestId << ", update resource metadata = "
        << ((options &
             LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata)
                ? "true"
                : "false")
        << ", update resource binary data = "
        << ((options &
             LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData)
                ? "true"
                : "false")
        << ", update tags = "
        << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags)
                ? "true"
                : "false")
        << ", note: " << note);

    Q_UNUSED(m_updateNoteRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onUpdateNoteFailed(
    Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::onUpdateNoteFailed: "
        << "request id = " << requestId << ", update resource metadata = "
        << ((options &
             LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata)
                ? "true"
                : "false")
        << ", update resource binary data = "
        << ((options &
             LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData)
                ? "true"
                : "false")
        << ", update tags = "
        << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags)
                ? "true"
                : "false")
        << ", error description = " << errorDescription << ", note: " << note);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::connectToLocalStorage()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::connectToLocalStorage");

    if (m_connectedToLocalStorage) {
        FEDEBUG("Already connected to the local storage");
        return;
    }

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::expungeNotebook,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNotebookRequest,
        Qt::QueuedConnection);

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::expungeTag,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeTagRequest, Qt::QueuedConnection);

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::expungeSavedSearch,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeSavedSearchRequest,
        Qt::QueuedConnection);

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::expungeNote,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNoteRequest, Qt::QueuedConnection);

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::updateNotebook,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNotebookRequest,
        Qt::QueuedConnection);

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::updateTag,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateTagRequest, Qt::QueuedConnection);

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::updateSavedSearch,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateSavedSearchRequest,
        Qt::QueuedConnection);

    QObject::connect(
        this, &FullSyncStaleDataItemsExpunger::updateNote,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest, Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNotebookComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNotebookFailed,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeTagComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeTagFailed,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeSavedSearchComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeSavedSearchFailed,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNoteComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNoteFailed,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNotebookComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookFailed, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNotebookFailed,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateTagComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateTagComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::updateTagFailed,
        this, &FullSyncStaleDataItemsExpunger::onUpdateTagFailed,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateSavedSearchComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchFailed, this,
        &FullSyncStaleDataItemsExpunger::onUpdateSavedSearchFailed,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNoteComplete,
        Qt::QueuedConnection);

    QObject::connect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteFailed, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNoteFailed,
        Qt::QueuedConnection);

    m_connectedToLocalStorage = true;
}

void FullSyncStaleDataItemsExpunger::disconnectFromLocalStorage()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::disconnectFromLocalStorage");

    if (!m_connectedToLocalStorage) {
        FEDEBUG("Not connected to local storage at the moment");
        return;
    }

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::expungeNotebook,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNotebookRequest);

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::expungeTag,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeTagRequest);

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::expungeSavedSearch,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeSavedSearchRequest);

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::expungeNote,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNoteRequest);

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::updateNotebook,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNotebookRequest);

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::updateTag,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateTagRequest);

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::updateSavedSearch,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateSavedSearchRequest);

    QObject::disconnect(
        this, &FullSyncStaleDataItemsExpunger::updateNote,
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNotebookComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNotebookFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNotebookFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeTagComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeTagFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeTagFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeSavedSearchComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeSavedSearchFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeSavedSearchFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNoteComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteFailed, this,
        &FullSyncStaleDataItemsExpunger::onExpungeNoteFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNotebookComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookFailed, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNotebookFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateTagComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateTagComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync, &LocalStorageManagerAsync::updateTagFailed,
        this, &FullSyncStaleDataItemsExpunger::onUpdateTagFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateSavedSearchComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchFailed, this,
        &FullSyncStaleDataItemsExpunger::onUpdateSavedSearchFailed);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNoteComplete);

    QObject::disconnect(
        &m_localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteFailed, this,
        &FullSyncStaleDataItemsExpunger::onUpdateNoteFailed);

    m_connectedToLocalStorage = false;
}

void FullSyncStaleDataItemsExpunger::checkAndRequestCachesFilling()
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::checkAndRequestCachesFilling");

    if (Q_UNLIKELY(m_pNotebookSyncCache.isNull())) {
        m_pNotebookSyncCache =
            QPointer<NotebookSyncCache>(new NotebookSyncCache(
                m_localStorageManagerAsync, m_linkedNotebookGuid, this));
    }

    if (!m_pNotebookSyncCache->isFilled()) {
        QObject::connect(
            m_pNotebookSyncCache.data(), &NotebookSyncCache::filled, this,
            &FullSyncStaleDataItemsExpunger::onNotebookCacheFilled,
            Qt::QueuedConnection);

        m_pendingNotebookSyncCache = true;
        m_pNotebookSyncCache->fill();
    }
    else {
        FEDEBUG("The notebook sync cache is already filled");
    }

    if (Q_UNLIKELY(m_pTagSyncCache.isNull())) {
        m_pTagSyncCache = QPointer<TagSyncCache>(new TagSyncCache(
            m_localStorageManagerAsync, m_linkedNotebookGuid, this));
    }

    if (!m_pTagSyncCache->isFilled()) {
        QObject::connect(
            m_pTagSyncCache.data(), &TagSyncCache::filled, this,
            &FullSyncStaleDataItemsExpunger::onTagCacheFilled,
            Qt::QueuedConnection);

        m_pendingTagSyncCache = true;
        m_pTagSyncCache->fill();
    }
    else {
        FEDEBUG("The tag sync cache is already filled");
    }

    // NOTE: saved searches are not a part of linked notebook content so there's
    // no need to do anything with saved searches if we are working with
    // a linked notebook
    if (m_linkedNotebookGuid.isEmpty()) {
        if (Q_UNLIKELY(m_pSavedSearchSyncCache.isNull())) {
            m_pSavedSearchSyncCache = QPointer<SavedSearchSyncCache>(
                new SavedSearchSyncCache(m_localStorageManagerAsync, this));
        }

        if (!m_pSavedSearchSyncCache->isFilled()) {
            QObject::connect(
                m_pSavedSearchSyncCache.data(), &SavedSearchSyncCache::filled,
                this, &FullSyncStaleDataItemsExpunger::onSavedSearchCacheFilled,
                Qt::QueuedConnection);

            m_pendingSavedSearchSyncCache = true;
            m_pSavedSearchSyncCache->fill();
        }
        else {
            FEDEBUG("The saved search sync cache is already filled");
        }
    }

    if (!m_noteSyncCache.isFilled()) {
        QObject::connect(
            &m_noteSyncCache, &NoteSyncCache::filled, this,
            &FullSyncStaleDataItemsExpunger::onNoteCacheFilled,
            Qt::QueuedConnection);

        m_pendingNoteSyncCache = true;
        m_noteSyncCache.fill();
    }
    else {
        FEDEBUG("The note sync cache is already filled");
    }
}

bool FullSyncStaleDataItemsExpunger::pendingCachesFilling() const
{
    FEDEBUG("FullSyncStaleDataItemsExpunger::pendingCachesFilling");

    if (m_pendingNotebookSyncCache) {
        FEDEBUG("Still pending notebook sync cache");
        return true;
    }

    if (m_pendingTagSyncCache) {
        FEDEBUG("Still pending tag sync cache");
        return true;
    }

    if (m_pendingSavedSearchSyncCache) {
        FEDEBUG("Still pending saved search sync cache");
        return true;
    }

    if (m_pendingNoteSyncCache) {
        FEDEBUG("Still pending note sync cache");
        return true;
    }

    FEDEBUG("Found no pending sync caches");
    return false;
}

void FullSyncStaleDataItemsExpunger::analyzeDataAndSendRequestsOrResult()
{
    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::analyzeDataAndSendRequestsOrResult");

    QSet<QString> notebookGuidsToExpunge;
    QSet<QString> savedSearchGuidsToExpunge;
    QSet<QString> noteGuidsToExpunge;
    m_tagGuidsToExpunge.clear();

    QHash<QString, QSet<QString>> noteGuidsToExpungeByNotebookGuidsToExpunge;

    QList<Notebook> dirtyNotebooksToUpdate;
    QList<Tag> dirtyTagsToUpdate;
    QList<SavedSearch> dirtySavedSearchesToUpdate;
    QList<Note> dirtyNotesToUpdate;

    if (!m_pNotebookSyncCache.isNull()) {
        const auto & nameByGuidHash = m_pNotebookSyncCache->nameByGuidHash();

        const auto & dirtyNotebooksByGuidHash =
            m_pNotebookSyncCache->dirtyNotebooksByGuidHash();

        for (const auto git: qevercloud::toRange(qAsConst(nameByGuidHash))) {
            const QString & guid = git.key();

            if (m_syncedGuids.m_syncedNotebookGuids.find(guid) !=
                m_syncedGuids.m_syncedNotebookGuids.end())
            {
                FETRACE(
                    "Found notebook guid " << guid
                                           << " within the synced ones");
                continue;
            }

            auto dirtyNotebookIt = dirtyNotebooksByGuidHash.find(guid);
            if (dirtyNotebookIt == dirtyNotebooksByGuidHash.end()) {
                FETRACE(
                    "Notebook guid " << guid
                                     << " doesn't appear within the list of "
                                     << "dirty notebooks");
                Q_UNUSED(notebookGuidsToExpunge.insert(guid))
            }
            else {
                FETRACE(
                    "Notebook guid "
                    << guid << " appears within the list of dirty notebooks");
                dirtyNotebooksToUpdate << dirtyNotebookIt.value();
            }
        }
    }
    else {
        FEWARNING("Notebook sync cache is expired");
    }

    if (!m_pTagSyncCache.isNull()) {
        const auto & nameByGuidHash = m_pTagSyncCache->nameByGuidHash();

        const auto & dirtyTagsByGuidHash =
            m_pTagSyncCache->dirtyTagsByGuidHash();

        for (const auto git: qevercloud::toRange(qAsConst(nameByGuidHash))) {
            const QString & guid = git.key();
            if (m_syncedGuids.m_syncedTagGuids.find(guid) !=
                m_syncedGuids.m_syncedTagGuids.end())
            {
                FETRACE("Found tag guid " << guid << " within the synced ones");
                continue;
            }

            auto dirtyTagIt = dirtyTagsByGuidHash.find(guid);
            if (dirtyTagIt == dirtyTagsByGuidHash.end()) {
                FETRACE(
                    "Tag guid "
                    << guid << " doesn't appear within the list of dirty tags");
                Q_UNUSED(m_tagGuidsToExpunge.insert(guid))
            }
            else {
                FETRACE(
                    "Tag guid " << guid
                                << " appears within the list of dirty tags");
                dirtyTagsToUpdate << dirtyTagIt.value();
            }
        }
    }
    else {
        FEWARNING("Tag sync cache is expired");
    }

    // Need to check if dirty tags to update have parent guids corresponding
    // to tags which should be expunged; if they do, need to clear these parent
    // guids from them because dirty tags need to be preserved
    for (auto & tag: dirtyTagsToUpdate) {
        if (!tag.hasParentGuid()) {
            continue;
        }

        auto parentGuidIt = m_tagGuidsToExpunge.find(tag.parentGuid());
        if (parentGuidIt == m_tagGuidsToExpunge.end()) {
            continue;
        }

        FETRACE(
            "Clearing parent guid from dirty tag because "
            << "the parent tag is going to be expunged: " << tag);

        tag.setParentGuid(QString());
        tag.setParentLocalUid(QString());
    }

    if (m_linkedNotebookGuid.isEmpty() && !m_pSavedSearchSyncCache.isNull()) {
        const auto & savedSearchNameByGuidHash =
            m_pSavedSearchSyncCache->nameByGuidHash();

        const auto & dirtySavedSearchesByGuid =
            m_pSavedSearchSyncCache->dirtySavedSearchesByGuid();

        for (const auto it:
             qevercloud::toRange(qAsConst(savedSearchNameByGuidHash))) {
            const QString & guid = it.key();
            if (m_syncedGuids.m_syncedSavedSearchGuids.find(guid) !=
                m_syncedGuids.m_syncedSavedSearchGuids.end())
            {
                FETRACE(
                    "Found saved search guid " << guid
                                               << " within the synced ones");
                continue;
            }

            auto dirtySavedSearchIt = dirtySavedSearchesByGuid.find(guid);
            if (dirtySavedSearchIt == dirtySavedSearchesByGuid.end()) {
                FETRACE(
                    "Saved search guid "
                    << guid
                    << " doesn't appear within the list of dirty searches");
                Q_UNUSED(savedSearchGuidsToExpunge.insert(guid))
            }
            else {
                FETRACE(
                    "Saved search guid "
                    << guid
                    << " appears within the list of dirty saved searches");
                dirtySavedSearchesToUpdate << dirtySavedSearchIt.value();
            }
        }
    }
    else if (m_linkedNotebookGuid.isEmpty()) {
        FEWARNING("Saved search sync cache is expired");
    }

    const auto & noteGuidToLocalUidBimap =
        m_noteSyncCache.noteGuidToLocalUidBimap();

    const auto & dirtyNotesByGuid = m_noteSyncCache.dirtyNotesByGuid();

    for (const auto & pair: noteGuidToLocalUidBimap.left) {
        const QString & guid = pair.first;
        if (m_syncedGuids.m_syncedNoteGuids.find(guid) !=
            m_syncedGuids.m_syncedNoteGuids.end())
        {
            FETRACE("Found note guid " << guid << " within the synced ones");
            continue;
        }

        const auto & notebookGuidByNoteGuid =
            m_noteSyncCache.notebookGuidByNoteGuid();

        auto notebookGuidIt = notebookGuidByNoteGuid.find(guid);
        if (Q_UNLIKELY(notebookGuidIt == notebookGuidByNoteGuid.end())) {
            FEWARNING(
                "Failed to find cached notebook guid for note guid "
                << guid << ", won't do anything with this note");
            continue;
        }

        const QString & notebookGuid = notebookGuidIt.value();

        auto dirtyNoteIt = dirtyNotesByGuid.find(guid);
        if (dirtyNoteIt == dirtyNotesByGuid.end()) {
            FETRACE(
                "Note guid "
                << guid << " doesn't appear within the list of dirty notes");

            Q_UNUSED(noteGuidsToExpunge.insert(guid))
            Q_UNUSED(
                noteGuidsToExpungeByNotebookGuidsToExpunge[notebookGuid].insert(
                    guid))
        }
        else {
            bool foundActualNotebook = false;

            if (!m_pNotebookSyncCache.isNull()) {
                if (m_syncedGuids.m_syncedNotebookGuids.find(notebookGuid) !=
                    m_syncedGuids.m_syncedNotebookGuids.end())
                {
                    FEDEBUG("Found notebook for a dirty note: it is synced");
                    foundActualNotebook = true;
                }
                else {
                    const auto & dirtyNotebooksByGuidHash =
                        m_pNotebookSyncCache->dirtyNotebooksByGuidHash();

                    auto dirtyNotebookIt =
                        dirtyNotebooksByGuidHash.find(notebookGuid);

                    if (dirtyNotebookIt != dirtyNotebooksByGuidHash.end()) {
                        FEDEBUG(
                            "Found notebook for a dirty note: "
                            << "it is also marked dirty");
                        foundActualNotebook = true;
                    }
                }
            }
            else {
                FEWARNING("Notebook sync cache is expired");
            }

            if (foundActualNotebook) {
                // This means the notebook for the note won't be expunged and
                // hence we should include the note into the list of those that
                // need to be updated
                FETRACE(
                    "Note guid " << guid
                                 << " appears within the list of dirty notes");
                dirtyNotesToUpdate << dirtyNoteIt.value();
                continue;
            }

            FEDEBUG(
                "Found no notebook for the note which should "
                << "survive the purge; that means the note would "
                << "be expunged automatically so there's no need "
                << "to do anything with it; note guid = " << guid);
        }
    }

    if (notebookGuidsToExpunge.isEmpty() && m_tagGuidsToExpunge.isEmpty() &&
        savedSearchGuidsToExpunge.isEmpty() && noteGuidsToExpunge.isEmpty() &&
        dirtyNotebooksToUpdate.isEmpty() && dirtyTagsToUpdate.isEmpty() &&
        dirtySavedSearchesToUpdate.isEmpty() && dirtyNotesToUpdate.isEmpty())
    {
        FEDEBUG("Nothing is required to be updated or expunged");

        m_inProgress = false;

        FEDEBUG("Emitting the finished signal");
        Q_EMIT finished();

        return;
    }

    connectToLocalStorage();

    for (const auto & guid: qAsConst(notebookGuidsToExpunge)) {
        Notebook dummyNotebook;
        dummyNotebook.unsetLocalUid();
        dummyNotebook.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNotebookRequestIds.insert(requestId))
        FETRACE(
            "Emitting the request to expunge notebook: "
            << "request id = " << requestId << ", notebook guid = " << guid);
        Q_EMIT expungeNotebook(dummyNotebook, requestId);

        // If some notes to be expunged belong to the notebook being expunged,
        // we don't need to expunge these notes separately
        auto it = noteGuidsToExpungeByNotebookGuidsToExpunge.find(guid);
        if (it != noteGuidsToExpungeByNotebookGuidsToExpunge.end()) {
            for (const auto & noteGuid: qAsConst(it.value())) {
                auto noteIt = noteGuidsToExpunge.find(noteGuid);
                if (noteIt != noteGuidsToExpunge.end()) {
                    noteGuidsToExpunge.erase(noteIt);
                }
            }

            noteGuidsToExpungeByNotebookGuidsToExpunge.erase(it);
        }
    }

    // NOTE: won't expunge tags until the dirty ones are updated in order
    // to prevent the automatic expunging of child tags along with their parents
    // - updating the dirty tags would remove parents which are going to be
    // expunged

    for (const auto & guid: qAsConst(savedSearchGuidsToExpunge)) {
        SavedSearch dummySearch;
        dummySearch.unsetLocalUid();
        dummySearch.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeSavedSearchRequestIds.insert(requestId))
        FETRACE(
            "Emitting the request to expunge saved search: "
            << "request id = " << requestId
            << ", saved search guid = " << guid);
        Q_EMIT expungeSavedSearch(dummySearch, requestId);
    }

    for (const auto & guid: qAsConst(noteGuidsToExpunge)) {
        Note dummyNote;
        dummyNote.unsetLocalUid();
        dummyNote.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNoteRequestIds.insert(requestId))
        FETRACE(
            "Emitting the request to expunge note: request id = "
            << requestId << ", note guid = " << guid);
        Q_EMIT expungeNote(dummyNote, requestId);
    }

    for (auto & notebook: dirtyNotebooksToUpdate) {
        notebook.setGuid(QString());
        notebook.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateNotebookRequestId.insert(requestId))
        FETRACE(
            "Emitting the request to update notebook: request id = "
            << requestId << ", notebook: " << notebook);
        Q_EMIT updateNotebook(notebook, requestId);
    }

    for (auto & tag: dirtyTagsToUpdate) {
        tag.setGuid(QString());
        tag.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateTagRequestIds.insert(requestId))
        FETRACE(
            "Emitting the request to update tag: request id = "
            << requestId << ", tag: " << tag);
        Q_EMIT updateTag(tag, requestId);
    }

    checkTagUpdatesCompletionAndSendExpungeTagRequests();

    for (auto & search: dirtySavedSearchesToUpdate) {
        search.setGuid(QString());
        search.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateSavedSearchRequestIds.insert(requestId))
        FETRACE(
            "Emitting the request to update saved search: "
            << "request id = " << requestId << ", saved search: " << search);
        Q_EMIT updateSavedSearch(search, requestId);
    }

    for (auto & note: dirtyNotesToUpdate) {
        note.setGuid(QString());

        // NOTE: it is just in case one of notebooks stripped off the guid was
        // this note's notebook; it shouldn't be a problem since the note should
        // have notebook local uid set as well
        note.setNotebookGuid(QString());

        note.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
        FETRACE(
            "Emitting the request to update note: request id = "
            << requestId << ", note: " << note);

        Q_EMIT updateNote(
            note,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            LocalStorageManager::UpdateNoteOptions(),
#else
            LocalStorageManager::UpdateNoteOptions(0),
#endif
            requestId);
    }
}

void FullSyncStaleDataItemsExpunger::checkRequestsCompletionAndSendResult()
{
    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::checkRequestsCompletionAndSendResult");

    if (!m_expungeNotebookRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_expungeNotebookRequestIds.size()
                             << " expunge notebook requests");
        return;
    }

    if (!m_expungeTagRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_expungeTagRequestIds.size()
                             << " expunge tag requests");
        return;
    }

    if (!m_expungeNoteRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_expungeNoteRequestIds.size()
                             << " expunge note requests");
        return;
    }

    if (!m_expungeSavedSearchRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_expungeSavedSearchRequestIds.size()
                             << " expunge saved search requests");
        return;
    }

    if (!m_updateNotebookRequestId.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_updateNotebookRequestId.size()
                             << " update notebook requests");
        return;
    }

    if (!m_updateTagRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_updateTagRequestIds.size()
                             << " update tag requests");
        return;
    }

    if (!m_updateNoteRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_updateNoteRequestIds.size()
                             << " update note requests");
        return;
    }

    if (!m_updateSavedSearchRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_updateSavedSearchRequestIds.size()
                             << " update saved search requests");
        return;
    }

    disconnectFromLocalStorage();
    m_inProgress = false;

    FEDEBUG("Emitting the finished signal");
    Q_EMIT finished();
}

void FullSyncStaleDataItemsExpunger::
    checkTagUpdatesCompletionAndSendExpungeTagRequests()
{
    FEDEBUG(
        "FullSyncStaleDataItemsExpunger::"
        << "checkTagUpdatesCompletionAndSendExpungeTagRequests");

    if (!m_updateTagRequestIds.isEmpty()) {
        FEDEBUG(
            "Still pending " << m_updateTagRequestIds.size()
                             << " tag update requests");
        return;
    }

    if (m_tagGuidsToExpunge.isEmpty()) {
        FEDEBUG(
            "Detected no pending tag update requests but "
            << "there are no tags meant to be expunged - "
            << "either there are no such ones or because expunge "
            << "requests have already been sent");
        return;
    }

    FEDEBUG(
        "Detected no pending tag update requests, "
        << "expunging the tags meant to be expunged");

    for (const auto & guid: qAsConst(m_tagGuidsToExpunge)) {
        Tag dummyTag;
        dummyTag.unsetLocalUid();
        dummyTag.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeTagRequestIds.insert(requestId))
        FETRACE(
            "Emitting the request to expunge tag: request id = "
            << requestId << ", tag guid = " << guid);
        Q_EMIT expungeTag(dummyTag, requestId);
    }

    m_tagGuidsToExpunge.clear();
}

} // namespace quentier
