#include "FullSyncStaleDataItemsExpunger.h"
#include "NotebookSyncCache.h"
#include "TagSyncCache.h"
#include "SavedSearchSyncCache.h"
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

FullSyncStaleDataItemsExpunger::Caches::Caches(const QList<NotebookSyncCache*> & notebookSyncCaches,
                                               const QList<TagSyncCache*> & tagSyncCaches,
                                               SavedSearchSyncCache & savedSearchSyncCache) :
    m_notebookSyncCaches(),
    m_tagSyncCaches(),
    m_savedSearchSyncCache(&savedSearchSyncCache)
{
    m_notebookSyncCaches.reserve(notebookSyncCaches.size());
    for(auto it = notebookSyncCaches.constBegin(), end = notebookSyncCaches.constEnd(); it != end; ++it) {
        m_notebookSyncCaches << QPointer<NotebookSyncCache>(*it);
    }

    m_tagSyncCaches.reserve(tagSyncCaches.size());
    for(auto it = tagSyncCaches.constBegin(), end = tagSyncCaches.constEnd(); it != end; ++it) {
        m_tagSyncCaches << QPointer<TagSyncCache>(*it);
    }
}

FullSyncStaleDataItemsExpunger::FullSyncStaleDataItemsExpunger(LocalStorageManagerAsync & localStorageManagerAsync,
                                                               const Caches & caches, const SyncedGuids & syncedGuids,
                                                               QObject * parent) :
    QObject(parent),
    m_localStorageManagerAsync(localStorageManagerAsync),
    m_connectedToLocalStorage(false),
    m_caches(caches),
    m_syncedGuids(syncedGuids),
    m_notebookGuidsByExpungeRequestId(),
    m_tagGuidsByExpungeRequestId(),
    m_noteGuidsByExpungeRequestId(),
    m_savedSearchGuidsByExpungeRequestId(),
    m_notebookGuidsByUpdateRequestId(),
    m_tagGuidsByUpdateRequestId(),
    m_noteGuidsByUpdateRequestId(),
    m_savedSearchGuidByUpdateRequestId()
{
    // TODO: 1) analyze synced guids and cached guids
    // 2) if there's something to expunge, send requests to do so
    // 3) when all expunge requests (if any) have been processed, send finished signal
}

void FullSyncStaleDataItemsExpunger::connectToLocalStorage()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::connectToLocalStorage"));

    if (m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Already connected to the local storage"));
        return;
    }

    // TODO: actually connect to the required signals/slots of the local storage

    m_connectedToLocalStorage = true;
}

void FullSyncStaleDataItemsExpunger::disconnectFromLocalStorage()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::disconnectFromLocalStorage"));

    if (!m_connectedToLocalStorage) {
        QNDEBUG(QStringLiteral("Not connected to local storage at the moment"));
        return;
    }

    // TODO: actually disconnect from the local storage

    m_connectedToLocalStorage = false;
}

} // namespace quentier
