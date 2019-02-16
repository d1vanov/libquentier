/*
 * Copyright 2017-2019 Dmitry Ivanov
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
#include "TagSyncCache.h"
#include "SavedSearchSyncCache.h"
#include <quentier/logging/QuentierLogger.h>
#include <QStringList>

#define __FELOG_BASE(message, level) \
    if (m_linkedNotebookGuid.isEmpty()) { \
        __QNLOG_BASE(message, level); \
    } \
    else { \
        __QNLOG_BASE(QStringLiteral("[linked notebook ") << m_linkedNotebookGuid \
                     << QStringLiteral("]: ") << message, level); \
    }

#define FETRACE(message) \
    __FELOG_BASE(message, Trace)

#define FEDEBUG(message) \
    __FELOG_BASE(message, Debug)

#define FEWARNING(message) \
    __FELOG_BASE(message, Warn)

namespace quentier {

FullSyncStaleDataItemsExpunger::FullSyncStaleDataItemsExpunger(
        LocalStorageManagerAsync & localStorageManagerAsync,
        NotebookSyncCache & notebookSyncCache,
        TagSyncCache & tagSyncCache,
        SavedSearchSyncCache & savedSearchSyncCache,
        const SyncedGuids & syncedGuids,
        const QString & linkedNotebookGuid,
        QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_inProgress(false),
    m_pNotebookSyncCache(&notebookSyncCache),
    m_pTagSyncCache(&tagSyncCache),
    m_pSavedSearchSyncCache(&savedSearchSyncCache),
    m_noteSyncCache(localStorageManagerAsync, linkedNotebookGuid),
    m_syncedGuids(syncedGuids),
    m_linkedNotebookGuid(linkedNotebookGuid),
    m_pendingNotebookSyncCache(false),
    m_pendingTagSyncCache(false),
    m_pendingSavedSearchSyncCache(false),
    m_pendingNoteSyncCache(false),
    m_expungeNotebookRequestIds(),
    m_expungeTagRequestIds(),
    m_expungeNoteRequestIds(),
    m_expungeSavedSearchRequestIds(),
    m_updateNotebookRequestId(),
    m_updateTagRequestIds(),
    m_updateNoteRequestIds(),
    m_updateSavedSearchRequestIds()
{}

void FullSyncStaleDataItemsExpunger::start()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::start"));

    if (m_inProgress) {
        FEDEBUG(QStringLiteral("Already started"));
        return;
    }

    m_inProgress = true;

    checkAndRequestCachesFilling();
    if (pendingCachesFilling()) {
        FEDEBUG(QStringLiteral("Pending caches filling"));
        return;
    }

    analyzeDataAndSendRequestsOrResult();
}

void FullSyncStaleDataItemsExpunger::onNotebookCacheFilled()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onNotebookCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    m_pendingNotebookSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onTagCacheFilled()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onTagCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    m_pendingTagSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onSavedSearchCacheFilled()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onSavedSearchCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    m_pendingSavedSearchSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onNoteCacheFilled()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onNoteCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        FEDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    m_pendingNoteSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onExpungeNotebookComplete(Notebook notebook,
                                                               QUuid requestId)
{
    auto it = m_expungeNotebookRequestIds.find(requestId);
    if (it == m_expungeNotebookRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeNotebookComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", notebook: ") << notebook);

    Q_UNUSED(m_expungeNotebookRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeNotebookFailed(Notebook notebook,
                                                             ErrorString errorDescription,
                                                             QUuid requestId)
{
    auto it = m_expungeNotebookRequestIds.find(requestId);
    if (it == m_expungeNotebookRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeNotebookFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", notebook: ") << notebook);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onExpungeTagComplete(
    Tag tag, QStringList expungedChildTagLocalUids, QUuid requestId)
{
    auto it = m_expungeTagRequestIds.find(requestId);
    if (it == m_expungeTagRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeTagComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", tag: ") << tag
            << QStringLiteral("\nExpunged child tag local uids: ")
            << expungedChildTagLocalUids.join(QStringLiteral(", ")));

    Q_UNUSED(m_expungeTagRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeTagFailed(Tag tag,
                                                        ErrorString errorDescription,
                                                        QUuid requestId)
{
    auto it = m_expungeTagRequestIds.find(requestId);
    if (it == m_expungeTagRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeTagFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", tag: ") << tag);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onExpungeSavedSearchComplete(SavedSearch search,
                                                                  QUuid requestId)
{
    auto it = m_expungeSavedSearchRequestIds.find(requestId);
    if (it == m_expungeSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeSavedSearchComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", saved search: ") << search);

    Q_UNUSED(m_expungeSavedSearchRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeSavedSearchFailed(SavedSearch search,
                                                                ErrorString errorDescription,
                                                                QUuid requestId)
{
    auto it = m_expungeSavedSearchRequestIds.find(requestId);
    if (it == m_expungeSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeSavedSearchFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", search: ") << search);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onExpungeNoteComplete(Note note,
                                                           QUuid requestId)
{
    auto it = m_expungeNoteRequestIds.find(requestId);
    if (it == m_expungeNoteRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeNoteComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", note: ") << note);

    Q_UNUSED(m_expungeNoteRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onExpungeNoteFailed(Note note,
                                                         ErrorString errorDescription,
                                                         QUuid requestId)
{
    auto it = m_expungeNoteRequestIds.find(requestId);
    if (it == m_expungeNoteRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onExpungeNoteFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", note: ") << note);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateNotebookComplete(Notebook notebook,
                                                              QUuid requestId)
{
    auto it = m_updateNotebookRequestId.find(requestId);
    if (it == m_updateNotebookRequestId.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateNotebookComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", notebook: ") << notebook);

    Q_UNUSED(m_updateNotebookRequestId.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onUpdateNotebookFailed(Notebook notebook,
                                                            ErrorString errorDescription,
                                                            QUuid requestId)
{
    auto it = m_updateNotebookRequestId.find(requestId);
    if (it == m_updateNotebookRequestId.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateNotebookFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", notebook: ") << notebook);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateTagComplete(Tag tag, QUuid requestId)
{
    auto it = m_updateTagRequestIds.find(requestId);
    if (it == m_updateTagRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateTagComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", tag: ") << tag);

    Q_UNUSED(m_updateTagRequestIds.erase(it))
    checkTagUpdatesCompletionAndSendExpungeTagRequests();
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onUpdateTagFailed(Tag tag,
                                                       ErrorString errorDescription,
                                                       QUuid requestId)
{
    auto it = m_updateTagRequestIds.find(requestId);
    if (it == m_updateTagRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateTagFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", tag: ") << tag);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateSavedSearchComplete(SavedSearch search,
                                                                 QUuid requestId)
{
    auto it = m_updateSavedSearchRequestIds.find(requestId);
    if (it == m_updateSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateSavedSearchComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", saved search: ") << search);

    Q_UNUSED(m_updateSavedSearchRequestIds.erase(it))
    checkRequestsCompletionAndSendResult();
}

void FullSyncStaleDataItemsExpunger::onUpdateSavedSearchFailed(SavedSearch search,
                                                               ErrorString errorDescription,
                                                               QUuid requestId)
{
    auto it = m_updateSavedSearchRequestIds.find(requestId);
    if (it == m_updateSavedSearchRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateSavedSearchFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", saved search: ") << search);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::onUpdateNoteComplete(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateNoteComplete: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", update resource metadata = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata)
                ? QStringLiteral("true")
                : QStringLiteral("false"))
            << QStringLiteral(", update resource binary data = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData)
                ? QStringLiteral("true")
                : QStringLiteral("false"))
            << QStringLiteral(", update tags = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags)
                ? QStringLiteral("true")
                : QStringLiteral("false"))
            << QStringLiteral(", note: ") << note);

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

    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onUpdateNoteFailed: ")
            << QStringLiteral("request id = ") << requestId
            << QStringLiteral(", update resource metadata = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata)
                ? QStringLiteral("true")
                : QStringLiteral("false"))
            << QStringLiteral(", update resource binary data = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData)
                ? QStringLiteral("true")
                : QStringLiteral("false"))
            << QStringLiteral(", update tags = ")
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags)
                ? QStringLiteral("true")
                : QStringLiteral("false"))
            << QStringLiteral(", error description = ") << errorDescription
            << QStringLiteral(", note: ") << note);

    Q_EMIT failure(errorDescription);
}

void FullSyncStaleDataItemsExpunger::connectToLocalStorage()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        FEDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,
                              expungeNotebook,Notebook,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onExpungeNotebookRequest,
                            Notebook,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,expungeTag,Tag,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onExpungeTagRequest,Tag,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,
                              expungeSavedSearch,SavedSearch,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onExpungeSavedSearchRequest,SavedSearch,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,
                              expungeNote,Note,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onExpungeNoteRequest,Note,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,
                              updateNotebook,Notebook,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onUpdateNotebookRequest,Notebook,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,updateTag,Tag,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,updateSavedSearch,
                              SavedSearch,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,
                            onUpdateSavedSearchRequest,SavedSearch,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(this,
                     QNSIGNAL(FullSyncStaleDataItemsExpunger,updateNote,Note,
                              LocalStorageManager::UpdateNoteOptions,QUuid),
                     &m_localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,
                            Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     Qt::QueuedConnection);

    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onExpungeNotebookComplete,Notebook,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,onExpungeNotebookFailed,
                            Notebook,ErrorString,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,
                              Tag,QStringList,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onExpungeTagComplete,Tag,QStringList,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeTagFailed,
                              Tag,ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,onExpungeTagFailed,
                            Tag,ErrorString,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              expungeSavedSearchComplete,SavedSearch,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onExpungeSavedSearchComplete,SavedSearch,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchFailed,
                              SavedSearch,ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onExpungeSavedSearchFailed,
                            SavedSearch,ErrorString,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,
                              expungeNoteComplete,Note,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onExpungeNoteComplete,Note,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,expungeNoteFailed,
                              Note,ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,onExpungeNoteFailed,
                            Note,ErrorString,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onUpdateNotebookComplete,Notebook,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onUpdateNotebookFailed,Notebook,ErrorString,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,
                              Tag,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateTagComplete,
                            Tag,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,
                              Tag,ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateTagFailed,
                            Tag,ErrorString,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,
                              SavedSearch,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onUpdateSavedSearchComplete,SavedSearch,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,
                              SavedSearch,ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,
                            onUpdateSavedSearchFailed,
                            SavedSearch,ErrorString,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,
                              Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateNoteComplete,
                            Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     Qt::QueuedConnection);
    QObject::connect(&m_localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,
                              Note,LocalStorageManager::UpdateNoteOptions,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateNoteFailed,
                            Note,LocalStorageManager::UpdateNoteOptions,
                            ErrorString,QUuid),
                     Qt::QueuedConnection);

    m_connectedToLocalStorage = true;
}

void FullSyncStaleDataItemsExpunger::disconnectFromLocalStorage()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        FEDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,
                                 expungeNotebook,Notebook,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,
                               onExpungeNotebookRequest,Notebook,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,
                                 expungeTag,Tag,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onExpungeTagRequest,
                               Tag,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,expungeSavedSearch,
                                 SavedSearch,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onExpungeSavedSearchRequest,
                               SavedSearch,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,expungeNote,
                                 Note,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onExpungeNoteRequest,
                               Note,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,updateNotebook,
                                 Notebook,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,
                               Notebook,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,updateTag,
                                 Tag,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,
                               Tag,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,updateSavedSearch,
                                 SavedSearch,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,
                               SavedSearch,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(FullSyncStaleDataItemsExpunger,updateNote,
                                 Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                        &m_localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,
                               Note,LocalStorageManager::UpdateNoteOptions,QUuid));

    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,expungeNotebookComplete,
                                 Notebook,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onExpungeNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,expungeNotebookFailed,
                                 Notebook,ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onExpungeNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,expungeTagComplete,
                                 Tag,QStringList,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,onExpungeTagComplete,
                               Tag,QStringList,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,expungeTagFailed,
                                 Tag,ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,onExpungeTagFailed,
                               Tag,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchComplete,
                                 SavedSearch,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onExpungeSavedSearchComplete,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,expungeSavedSearchFailed,
                                 SavedSearch,ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onExpungeSavedSearchFailed,
                               SavedSearch,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,
                                 expungeNoteComplete,Note,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onExpungeNoteComplete,Note,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,expungeNoteFailed,
                                 Note,ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,onExpungeNoteFailed,
                               Note,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,
                                 Notebook,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onUpdateNotebookComplete,Notebook,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,
                                 Notebook,ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onUpdateNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,
                                 Tag,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateTagComplete,
                               Tag,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,
                                 Tag,ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateTagFailed,
                               Tag,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,
                                 updateSavedSearchComplete,SavedSearch,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onUpdateSavedSearchComplete,SavedSearch,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,
                                 SavedSearch,ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,
                               onUpdateSavedSearchFailed,
                               SavedSearch,ErrorString,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,
                                 Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateNoteComplete,
                               Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::disconnect(&m_localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,
                                 Note,LocalStorageManager::UpdateNoteOptions,
                                 ErrorString,QUuid),
                        this,
                        QNSLOT(FullSyncStaleDataItemsExpunger,onUpdateNoteFailed,
                               Note,LocalStorageManager::UpdateNoteOptions,
                               ErrorString,QUuid));

    m_connectedToLocalStorage = false;
}

void FullSyncStaleDataItemsExpunger::checkAndRequestCachesFilling()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::"
                           "checkAndRequestCachesFilling"));

    if (Q_UNLIKELY(m_pNotebookSyncCache.isNull())) {
        m_pNotebookSyncCache =
            QPointer<NotebookSyncCache>(
                new NotebookSyncCache(m_localStorageManagerAsync,
                                      m_linkedNotebookGuid, this));
    }

    if (!m_pNotebookSyncCache->isFilled())
    {
        QObject::connect(m_pNotebookSyncCache.data(),
                         QNSIGNAL(NotebookSyncCache,filled),
                         this,
                         QNSLOT(FullSyncStaleDataItemsExpunger,onNotebookCacheFilled),
                         Qt::QueuedConnection);
        m_pendingNotebookSyncCache = true;
        m_pNotebookSyncCache->fill();
    }
    else
    {
        FEDEBUG(QStringLiteral("The notebook sync cache is already filled"));
    }

    if (Q_UNLIKELY(m_pTagSyncCache.isNull())) {
        m_pTagSyncCache =
            QPointer<TagSyncCache>(
                new TagSyncCache(m_localStorageManagerAsync,
                                 m_linkedNotebookGuid, this));
    }

    if (!m_pTagSyncCache->isFilled())
    {
        QObject::connect(m_pTagSyncCache.data(),
                         QNSIGNAL(TagSyncCache,filled),
                         this,
                         QNSLOT(FullSyncStaleDataItemsExpunger,onTagCacheFilled),
                         Qt::QueuedConnection);
        m_pendingTagSyncCache = true;
        m_pTagSyncCache->fill();
    }
    else
    {
        FEDEBUG(QStringLiteral("The tag sync cache is already filled"));
    }

    // NOTE: saved searches are not a part of linked notebook content so there's
    // no need to do anything with saved searches if we are working with
    // a linked notebook
    if (m_linkedNotebookGuid.isEmpty())
    {
        if (Q_UNLIKELY( m_pSavedSearchSyncCache.isNull())) {
            m_pSavedSearchSyncCache =
                QPointer<SavedSearchSyncCache>(
                    new SavedSearchSyncCache(m_localStorageManagerAsync, this));
        }

        if (!m_pSavedSearchSyncCache->isFilled())
        {
            QObject::connect(m_pSavedSearchSyncCache.data(),
                             QNSIGNAL(SavedSearchSyncCache,filled),
                             this,
                             QNSLOT(FullSyncStaleDataItemsExpunger,
                                    onSavedSearchCacheFilled),
                             Qt::QueuedConnection);
            m_pendingSavedSearchSyncCache = true;
            m_pSavedSearchSyncCache->fill();
        }
        else
        {
            FEDEBUG(QStringLiteral("The saved search sync cache is already filled"));
        }
    }

    if (!m_noteSyncCache.isFilled())
    {
        QObject::connect(&m_noteSyncCache,
                         QNSIGNAL(NoteSyncCache,filled),
                         this,
                         QNSLOT(FullSyncStaleDataItemsExpunger,onNoteCacheFilled),
                         Qt::QueuedConnection);
        m_pendingNoteSyncCache = true;
        m_noteSyncCache.fill();
    }
    else
    {
        FEDEBUG(QStringLiteral("The note sync cache is already filled"));
    }
}

bool FullSyncStaleDataItemsExpunger::pendingCachesFilling() const
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::pendingCachesFilling"));

    if (m_pendingNotebookSyncCache) {
        FEDEBUG(QStringLiteral("Still pending notebook sync cache"));
        return true;
    }

    if (m_pendingTagSyncCache) {
        FEDEBUG(QStringLiteral("Still pending tag sync cache"));
        return true;
    }

    if (m_pendingSavedSearchSyncCache) {
        FEDEBUG(QStringLiteral("Still pending saved search sync cache"));
        return true;
    }

    if (m_pendingNoteSyncCache) {
        FEDEBUG(QStringLiteral("Still pending note sync cache"));
        return true;
    }

    FEDEBUG(QStringLiteral("Found no pending sync caches"));
    return false;
}

void FullSyncStaleDataItemsExpunger::analyzeDataAndSendRequestsOrResult()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::"
                           "analyzeDataAndSendRequestsOrResult"));

    QSet<QString>   notebookGuidsToExpunge;
    QSet<QString>   savedSearchGuidsToExpunge;
    QSet<QString>   noteGuidsToExpunge;
    m_tagGuidsToExpunge.clear();

    QList<Notebook>     dirtyNotebooksToUpdate;
    QList<Tag>          dirtyTagsToUpdate;
    QList<SavedSearch>  dirtySavedSearchesToUpdate;
    QList<Note>         dirtyNotesToUpdate;

    if (!m_pNotebookSyncCache.isNull())
    {
        const QHash<QString,QString> & nameByGuidHash =
            m_pNotebookSyncCache->nameByGuidHash();
        const QHash<QString,Notebook> & dirtyNotebooksByGuidHash =
            m_pNotebookSyncCache->dirtyNotebooksByGuidHash();

        for(auto git = nameByGuidHash.constBegin(),
            gend = nameByGuidHash.constEnd(); git != gend; ++git)
        {
            const QString & guid = git.key();
            if (m_syncedGuids.m_syncedNotebookGuids.find(guid) !=
                m_syncedGuids.m_syncedNotebookGuids.end())
            {
                FETRACE(QStringLiteral("Found notebook guid ") << guid
                        << QStringLiteral(" within the synced ones"));
                continue;
            }

            auto dirtyNotebookIt = dirtyNotebooksByGuidHash.find(guid);
            if (dirtyNotebookIt == dirtyNotebooksByGuidHash.end())
            {
                FETRACE(QStringLiteral("Notebook guid ") << guid
                        << QStringLiteral(" doesn't appear within the list of "
                                          "dirty notebooks"));
                Q_UNUSED(notebookGuidsToExpunge.insert(guid))
            }
            else
            {
                FETRACE(QStringLiteral("Notebook guid ") << guid
                        << QStringLiteral(" appears within the list of "
                                          "dirty notebooks"));
                dirtyNotebooksToUpdate << dirtyNotebookIt.value();
            }
        }
    }
    else
    {
        FEWARNING(QStringLiteral("Notebook sync cache is expired"));
    }

    if (!m_pTagSyncCache.isNull())
    {
        const QHash<QString,QString> & nameByGuidHash =
            m_pTagSyncCache->nameByGuidHash();
        const QHash<QString,Tag> & dirtyTagsByGuidHash =
            m_pTagSyncCache->dirtyTagsByGuidHash();

        for(auto git = nameByGuidHash.constBegin(),
            gend = nameByGuidHash.constEnd(); git != gend; ++git)
        {
            const QString & guid = git.key();
            if (m_syncedGuids.m_syncedTagGuids.find(guid) !=
                m_syncedGuids.m_syncedTagGuids.end())
            {
                FETRACE(QStringLiteral("Found tag guid ") << guid
                        << QStringLiteral(" within the synced ones"));
                continue;
            }

            auto dirtyTagIt = dirtyTagsByGuidHash.find(guid);
            if (dirtyTagIt == dirtyTagsByGuidHash.end())
            {
                FETRACE(QStringLiteral("Tag guid ") << guid
                        << QStringLiteral(" doesn't appear within the list of "
                                          "dirty tags"));
                Q_UNUSED(m_tagGuidsToExpunge.insert(guid))
            }
            else
            {
                FETRACE(QStringLiteral("Tag guid ") << guid
                        << QStringLiteral(" appears within the list of dirty tags"));
                dirtyTagsToUpdate << dirtyTagIt.value();
            }
        }
    }
    else
    {
        FEWARNING(QStringLiteral("Tag sync cache is expired"));
    }

    // Need to check if dirty tags to update have parent guids corresponding
    // to tags which should be expunged; if they do, need to clear these parent
    // guids from them because dirty tags need to be preserved
    for(auto it = dirtyTagsToUpdate.begin(),
        end = dirtyTagsToUpdate.end(); it != end; ++it)
    {
        Tag & tag = *it;

        if (!tag.hasParentGuid()) {
            continue;
        }

        auto parentGuidIt = m_tagGuidsToExpunge.find(tag.parentGuid());
        if (parentGuidIt == m_tagGuidsToExpunge.end()) {
            continue;
        }

        FETRACE(QStringLiteral("Clearing parent guid from dirty tag because "
                               "the parent tag is going to be expunged: ") << tag);

        tag.setParentGuid(QString());
        tag.setParentLocalUid(QString());
    }

    if (m_linkedNotebookGuid.isEmpty() && !m_pSavedSearchSyncCache.isNull())
    {
        const QHash<QString,QString> & savedSearchNameByGuidHash =
            m_pSavedSearchSyncCache->nameByGuidHash();
        const QHash<QString,SavedSearch> & dirtySavedSearchesByGuid =
            m_pSavedSearchSyncCache->dirtySavedSearchesByGuid();

        for(auto it = savedSearchNameByGuidHash.constBegin(),
            end = savedSearchNameByGuidHash.constEnd(); it != end; ++it)
        {
            const QString & guid = it.key();
            if (m_syncedGuids.m_syncedSavedSearchGuids.find(guid) !=
                m_syncedGuids.m_syncedSavedSearchGuids.end())
            {
                FETRACE(QStringLiteral("Found saved search guid ") << guid
                        << QStringLiteral(" within the synced ones"));
                continue;
            }

            auto dirtySavedSearchIt = dirtySavedSearchesByGuid.find(guid);
            if (dirtySavedSearchIt == dirtySavedSearchesByGuid.end())
            {
                FETRACE(QStringLiteral("Saved search guid ") << guid
                        << QStringLiteral(" doesn't appear within the list of "
                                          "dirty searches"));
                Q_UNUSED(savedSearchGuidsToExpunge.insert(guid))
            }
            else
            {
                FETRACE(QStringLiteral("Saved search guid ") << guid
                        << QStringLiteral(" appears within the list of dirty "
                                          "saved searches"));
                dirtySavedSearchesToUpdate << dirtySavedSearchIt.value();
            }
        }
    }
    else if (m_linkedNotebookGuid.isEmpty())
    {
        FEWARNING(QStringLiteral("Saved search sync cache is expired"));
    }

    const NoteSyncCache::NoteGuidToLocalUidBimap & noteGuidToLocalUidBimap =
        m_noteSyncCache.noteGuidToLocalUidBimap();
    const QHash<QString,Note> & dirtyNotesByGuid = m_noteSyncCache.dirtyNotesByGuid();
    for(auto it = noteGuidToLocalUidBimap.left.begin(),
        end = noteGuidToLocalUidBimap.left.end(); it != end; ++it)
    {
        const QString & guid = it->first;
        if (m_syncedGuids.m_syncedNoteGuids.find(guid) !=
            m_syncedGuids.m_syncedNoteGuids.end())
        {
            FETRACE(QStringLiteral("Found note guid ") << guid
                    << QStringLiteral(" within the synced ones"));
            continue;
        }

        auto dirtyNoteIt = dirtyNotesByGuid.find(guid);
        if (dirtyNoteIt == dirtyNotesByGuid.end())
        {
            FETRACE(QStringLiteral("Note guid ") << guid
                    << QStringLiteral(" doesn't appear within the list of "
                                      "dirty notes"));
            Q_UNUSED(noteGuidsToExpunge.insert(guid))
        }
        else
        {
            const QHash<QString,QString> & notebookGuidByNoteGuid =
                m_noteSyncCache.notebookGuidByNoteGuid();
            auto notebookGuidIt = notebookGuidByNoteGuid.find(guid);
            if (Q_UNLIKELY(notebookGuidIt == notebookGuidByNoteGuid.end()))
            {
                FEWARNING(QStringLiteral("Failed to find cached notebook guid ")
                          << QStringLiteral("for note guid ") << guid
                          << QStringLiteral(", won't do anything with this note"));
                continue;
            }

            const QString & notebookGuid = notebookGuidIt.value();
            bool foundActualNotebook = false;

            if (!m_pNotebookSyncCache.isNull())
            {
                if (m_syncedGuids.m_syncedNotebookGuids.find(notebookGuid) !=
                    m_syncedGuids.m_syncedNotebookGuids.end())
                {
                    FEDEBUG(QStringLiteral("Found notebook for a dirty note: "
                                           "it is synced"));
                    foundActualNotebook = true;
                }
                else
                {
                    const QHash<QString,Notebook> & dirtyNotebooksByGuidHash =
                        m_pNotebookSyncCache->dirtyNotebooksByGuidHash();
                    auto dirtyNotebookIt = dirtyNotebooksByGuidHash.find(notebookGuid);
                    if (dirtyNotebookIt != dirtyNotebooksByGuidHash.end())
                    {
                        FEDEBUG(QStringLiteral("Found notebook for a dirty note: "
                                               "it is also marked dirty"));
                        foundActualNotebook = true;
                    }
                }
            }
            else
            {
                FEWARNING(QStringLiteral("Notebook sync cache is expired"));
            }

            if (foundActualNotebook)
            {
                // This means the notebook for the note won't be expunged and
                // hence we should include the note into the list of those that
                // need to be updated
                FETRACE(QStringLiteral("Note guid ") << guid
                        << QStringLiteral(" appears within the list of "
                                          "dirty notes"));
                dirtyNotesToUpdate << dirtyNoteIt.value();
                continue;
            }

            FEDEBUG(QStringLiteral("Found no notebook for the note which should "
                                   "survive the purge; that means the note would "
                                   "be expunged automatically so there's no need "
                                   "to do anything with it; note guid = ") << guid);
        }
    }

    if (notebookGuidsToExpunge.isEmpty() &&
        m_tagGuidsToExpunge.isEmpty() &&
        savedSearchGuidsToExpunge.isEmpty() &&
        noteGuidsToExpunge.isEmpty() &&
        dirtyNotebooksToUpdate.isEmpty() &&
        dirtyTagsToUpdate.isEmpty() &&
        dirtySavedSearchesToUpdate.isEmpty() &&
        dirtyNotesToUpdate.isEmpty())
    {
        FEDEBUG(QStringLiteral("Nothing is required to be updated or expunged"));

        m_inProgress = false;

        FEDEBUG(QStringLiteral("Emitting the finished signal"));
        Q_EMIT finished();

        return;
    }

    connectToLocalStorage();

    for(auto it = notebookGuidsToExpunge.constBegin(),
        end = notebookGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        Notebook dummyNotebook;
        dummyNotebook.unsetLocalUid();
        dummyNotebook.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNotebookRequestIds.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to expunge notebook: ")
                << QStringLiteral("request id = ") << requestId
                << QStringLiteral(", notebook guid = ") << guid);
        Q_EMIT expungeNotebook(dummyNotebook, requestId);
    }

    // NOTE: won't expunge tags until the dirty ones are updated in order
    // to prevent the automatic expunging of child tags along with their parents -
    // updating the dirty tags would remove parents which are going to be expunged

    for(auto it = savedSearchGuidsToExpunge.constBegin(),
        end = savedSearchGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        SavedSearch dummySearch;
        dummySearch.unsetLocalUid();
        dummySearch.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeSavedSearchRequestIds.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to expunge saved search: ")
                << QStringLiteral("request id = ") << requestId
                << QStringLiteral(", saved search guid = ") << guid);
        Q_EMIT expungeSavedSearch(dummySearch, requestId);
    }

    for(auto it = noteGuidsToExpunge.constBegin(),
        end = noteGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        Note dummyNote;
        dummyNote.unsetLocalUid();
        dummyNote.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNoteRequestIds.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to expunge note: request id = ")
                << requestId << QStringLiteral(", note guid = ") << guid);
        Q_EMIT expungeNote(dummyNote, requestId);
    }

    for(auto it = dirtyNotebooksToUpdate.begin(),
        end = dirtyNotebooksToUpdate.end(); it != end; ++it)
    {
        Notebook & notebook = *it;
        notebook.setGuid(QString());
        notebook.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateNotebookRequestId.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to update notebook: request id = ")
                << requestId << QStringLiteral(", notebook: ") << notebook);
        Q_EMIT updateNotebook(notebook, requestId);
    }

    for(auto it = dirtyTagsToUpdate.begin(),
        end = dirtyTagsToUpdate.end(); it != end; ++it)
    {
        Tag & tag = *it;
        tag.setGuid(QString());
        tag.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateTagRequestIds.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to update tag: request id = ")
                << requestId << QStringLiteral(", tag: ") << tag);
        Q_EMIT updateTag(tag, requestId);
    }

    checkTagUpdatesCompletionAndSendExpungeTagRequests();

    for(auto it = dirtySavedSearchesToUpdate.begin(),
        end = dirtySavedSearchesToUpdate.end(); it != end; ++it)
    {
        SavedSearch & search = *it;
        search.setGuid(QString());
        search.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateSavedSearchRequestIds.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to update saved search: ")
                << QStringLiteral("request id = ") << requestId
                << QStringLiteral(", saved search: ") << search);
        Q_EMIT updateSavedSearch(search, requestId);
    }

    for(auto it = dirtyNotesToUpdate.begin(),
        end = dirtyNotesToUpdate.end(); it != end; ++it)
    {
        Note & note = *it;
        note.setGuid(QString());

        // NOTE: it is just in case one of notebooks stripped off the guid was
        // this note's notebook; it shouldn't be a problem since the note should
        // have notebook local uid set as well
        note.setNotebookGuid(QString());

        note.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to update note: request id = ")
                << requestId << QStringLiteral(", note: ") << note);
        Q_EMIT updateNote(note, LocalStorageManager::UpdateNoteOptions(0), requestId);
    }
}

void FullSyncStaleDataItemsExpunger::checkRequestsCompletionAndSendResult()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::"
                           "checkRequestsCompletionAndSendResult"));

    if (!m_expungeNotebookRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_expungeNotebookRequestIds.size()
                << QStringLiteral(" expunge notebook requests"));
        return;
    }

    if (!m_expungeTagRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_expungeTagRequestIds.size()
                << QStringLiteral(" expunge tag requests"));
        return;
    }

    if (!m_expungeNoteRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_expungeNoteRequestIds.size()
                << QStringLiteral(" expunge note requests"));
        return;
    }

    if (!m_expungeSavedSearchRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_expungeSavedSearchRequestIds.size()
                << QStringLiteral(" expunge saved search requests"));
        return;
    }

    if (!m_updateNotebookRequestId.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_updateNotebookRequestId.size()
                << QStringLiteral(" update notebook requests"));
        return;
    }

    if (!m_updateTagRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_updateTagRequestIds.size()
                << QStringLiteral(" update tag requests"));
        return;
    }

    if (!m_updateNoteRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_updateNoteRequestIds.size()
                << QStringLiteral(" update note requests"));
        return;
    }

    if (!m_updateSavedSearchRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_updateSavedSearchRequestIds.size()
                << QStringLiteral(" update saved search requests"));
        return;
    }

    disconnectFromLocalStorage();
    m_inProgress = false;

    FEDEBUG(QStringLiteral("Emitting the finished signal"));
    Q_EMIT finished();
}

void FullSyncStaleDataItemsExpunger::checkTagUpdatesCompletionAndSendExpungeTagRequests()
{
    FEDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::"
                           "checkTagUpdatesCompletionAndSendExpungeTagRequests"));

    if (!m_updateTagRequestIds.isEmpty()) {
        FEDEBUG(QStringLiteral("Still pending ") << m_updateTagRequestIds.size()
                << QStringLiteral(" tag update requests"));
        return;
    }

    if (m_tagGuidsToExpunge.isEmpty()) {
        FEDEBUG(QStringLiteral("Detected no pending tag update requests but "
                               "there are no tags meant to be expunged - "
                               "either there are no such ones or because expunge "
                               "requests have already been sent"));
        return;
    }

    FEDEBUG(QStringLiteral("Detected no pending tag update requests, "
                           "expunging the tags meant to be expunged"));

    for(auto it = m_tagGuidsToExpunge.constBegin(),
        end = m_tagGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        Tag dummyTag;
        dummyTag.unsetLocalUid();
        dummyTag.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeTagRequestIds.insert(requestId))
        FETRACE(QStringLiteral("Emitting the request to expunge tag: request id = ")
                << requestId << QStringLiteral(", tag guid = ") << guid);
        Q_EMIT expungeTag(dummyTag, requestId);
    }

    m_tagGuidsToExpunge.clear();
}

} // namespace quentier
