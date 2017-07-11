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
    m_inProgress(false),
    m_caches(caches),
    m_noteSyncCache(localStorageManagerAsync),
    m_syncedGuids(syncedGuids),
    m_numPendingNotebookSyncCaches(0),
    m_numPendingTagSyncCaches(0),
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
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::start"));

    if (m_inProgress) {
        QNDEBUG(QStringLiteral("Already started"));
        return;
    }

    m_inProgress = true;

    checkAndRequestCachesFilling();
    if (pendingCachesFilling()) {
        QNDEBUG(QStringLiteral("Pending caches filling"));
        return;
    }

    analyzeDataAndSendRequestsOrResult();
}

void FullSyncStaleDataItemsExpunger::onNotebookCacheFilled()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onNotebookCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        QNDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    if (m_numPendingNotebookSyncCaches > 0) {
        --m_numPendingNotebookSyncCaches;
        QNTRACE(QStringLiteral("Decremented the number of pending notebook sync caches to ") << m_numPendingNotebookSyncCaches);
    }

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onTagCacheFilled()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onTagCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        QNDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    if (m_numPendingTagSyncCaches > 0) {
        --m_numPendingTagSyncCaches;
        QNTRACE(QStringLiteral("Decremented the number of pending tag sync caches to ") << m_numPendingTagSyncCaches);
    }

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onSavedSearchCacheFilled()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onSavedSearchCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        QNDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    m_pendingSavedSearchSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
}

void FullSyncStaleDataItemsExpunger::onNoteCacheFilled()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::onNoteCacheFilled"));

    if (Q_UNLIKELY(!m_inProgress)) {
        QNDEBUG(QStringLiteral("Not in progress at the moment"));
        return;
    }

    m_pendingNoteSyncCache = false;

    if (!pendingCachesFilling()) {
        analyzeDataAndSendRequestsOrResult();
    }
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

void FullSyncStaleDataItemsExpunger::checkAndRequestCachesFilling()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::checkAndRequestCachesFilling"));

    for(auto it = m_caches.m_notebookSyncCaches.constBegin(),
        end = m_caches.m_notebookSyncCaches.constEnd(); it != end; ++it)
    {
        const QPointer<NotebookSyncCache> & pNotebookSyncCache = *it;
        if (Q_UNLIKELY(pNotebookSyncCache.isNull())) {
            QNDEBUG(QStringLiteral("Skipping expired notebook sync cache"));
            continue;
        }

        if (pNotebookSyncCache->isFilled()) {
            continue;
        }

        QObject::connect(pNotebookSyncCache.data(), QNSIGNAL(NotebookSyncCache,filled),
                         this, QNSLOT(FullSyncStaleDataItemsExpunger,onNotebookCacheFilled));
        ++m_numPendingNotebookSyncCaches;
        pNotebookSyncCache->fill();
    }

    for(auto it = m_caches.m_tagSyncCaches.constBegin(),
        end = m_caches.m_tagSyncCaches.constEnd(); it != end; ++it)
    {
        const QPointer<TagSyncCache> & pTagSyncCache = *it;
        if (Q_UNLIKELY(pTagSyncCache.isNull())) {
            QNDEBUG(QStringLiteral("Skipping expired tag sync cache"));
            continue;
        }

        if (pTagSyncCache->isFilled()) {
            continue;
        }

        QObject::connect(pTagSyncCache.data(), QNSIGNAL(TagSyncCache,filled),
                         this, QNSLOT(FullSyncStaleDataItemsExpunger,onTagCacheFilled));
        ++m_numPendingTagSyncCaches;
        pTagSyncCache->fill();
    }

    if (Q_UNLIKELY(m_caches.m_savedSearchSyncCache.isNull())) {
        SavedSearchSyncCache * pSavedSearchSyncCache = new SavedSearchSyncCache(m_localStorageManagerAsync, this);
        m_caches.m_savedSearchSyncCache = QPointer<SavedSearchSyncCache>(pSavedSearchSyncCache);
    }

    if (!m_caches.m_savedSearchSyncCache->isFilled()) {
        QObject::connect(m_caches.m_savedSearchSyncCache.data(), QNSIGNAL(SavedSearchSyncCache,filled),
                         this, QNSLOT(FullSyncStaleDataItemsExpunger,onSavedSearchCacheFilled));
        m_pendingSavedSearchSyncCache = true;
        m_caches.m_savedSearchSyncCache->fill();
    }

    if (!m_noteSyncCache.isFilled()) {
        QObject::connect(&m_noteSyncCache, QNSIGNAL(NoteSyncCache,filled),
                         this, QNSLOT(FullSyncStaleDataItemsExpunger,onNoteCacheFilled));
        m_pendingNoteSyncCache = true;
        m_noteSyncCache.fill();
    }
}

bool FullSyncStaleDataItemsExpunger::pendingCachesFilling() const
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::pendingCachesFilling"));

    if (m_numPendingNotebookSyncCaches > 0) {
        QNDEBUG(QStringLiteral("Still pending ") << m_numPendingNotebookSyncCaches
                << QStringLiteral(" notebook sync caches"));
        return true;
    }

    if (m_numPendingTagSyncCaches > 0) {
        QNDEBUG(QStringLiteral("Still pending ") << m_numPendingTagSyncCaches
                << QStringLiteral(" tag sync caches"));
        return true;
    }

    if (m_pendingSavedSearchSyncCache) {
        QNDEBUG(QStringLiteral("Still pending saved search sync cache"));
        return true;
    }

    if (m_pendingNoteSyncCache) {
        QNDEBUG(QStringLiteral("Still pending note sync cache"));
        return true;
    }

    QNDEBUG(QStringLiteral("Found no pending sync caches"));
    return false;
}

void FullSyncStaleDataItemsExpunger::analyzeDataAndSendRequestsOrResult()
{
    QNDEBUG(QStringLiteral("FullSyncStaleDataItemsExpunger::analyzeDataAndSendRequestsOrResult"));

    QSet<QString>   notebookGuidsToExpunge;
    QSet<QString>   tagGuidsToExpunge;
    QSet<QString>   savedSearchGuidsToExpunge;
    QSet<QString>   noteGuidsToExpunge;

    QList<Notebook>     dirtyNotebooksToUpdate;
    QList<Tag>          dirtyTagsToUpdate;
    QList<SavedSearch>  dirtySavedSearchesToUpdate;
    QList<Note>         dirtyNotesToUpdate;

    for(auto it = m_caches.m_notebookSyncCaches.constBegin(),
        end = m_caches.m_notebookSyncCaches.constEnd(); it != end; ++it)
    {
        const QPointer<NotebookSyncCache> & pNotebookSyncCache = *it;
        if (Q_UNLIKELY(pNotebookSyncCache.isNull())) {
            QNWARNING(QStringLiteral("Skipping the already expired notebook sync cache"));
            continue;
        }

        const QHash<QString,QString> & nameByGuidHash = pNotebookSyncCache->nameByGuidHash();
        const QHash<QString,Notebook> & dirtyNotebooksByGuidHash = pNotebookSyncCache->dirtyNotebooksByGuidHash();

        for(auto git = nameByGuidHash.constBegin(), gend = nameByGuidHash.constEnd(); git != gend; ++git)
        {
            const QString & guid = git.key();
            if (m_syncedGuids.m_syncedNotebookGuids.find(guid) != m_syncedGuids.m_syncedNotebookGuids.end()) {
                QNTRACE(QStringLiteral("Found notebook guid ") << guid << QStringLiteral(" within the synced ones"));
                continue;
            }

            auto dirtyNotebookIt = dirtyNotebooksByGuidHash.find(guid);
            if (dirtyNotebookIt == dirtyNotebooksByGuidHash.end()) {
                QNTRACE(QStringLiteral("Notebook guid ") << guid << QStringLiteral(" doesn't appear within the list of dirty notebooks"));
                Q_UNUSED(notebookGuidsToExpunge.insert(guid))
            }
            else {
                QNTRACE(QStringLiteral("Notebook guid ") << guid << QStringLiteral(" appears within the list of dirty notebooks"));
                dirtyNotebooksToUpdate << dirtyNotebookIt.value();
            }
        }
    }

    for(auto it = m_caches.m_tagSyncCaches.constBegin(), end = m_caches.m_tagSyncCaches.constEnd(); it != end; ++it)
    {
        const QPointer<TagSyncCache> & pTagSyncCache = *it;
        if (Q_UNLIKELY(pTagSyncCache.isNull())) {
            QNWARNING(QStringLiteral("Skipping the already expired tag sync cache"));
            continue;
        }

        const QHash<QString,QString> & nameByGuidHash = pTagSyncCache->nameByGuidHash();
        const QHash<QString,Tag> & dirtyTagsByGuidHash = pTagSyncCache->dirtyTagsByGuidHash();

        for(auto git = nameByGuidHash.constBegin(), gend = nameByGuidHash.constEnd(); git != gend; ++git)
        {
            const QString & guid = git.key();
            if (m_syncedGuids.m_syncedTagGuids.find(guid) != m_syncedGuids.m_syncedTagGuids.end()) {
                QNTRACE(QStringLiteral("Found tag guid ") << guid << QStringLiteral(" within the synced ones"));
                continue;
            }

            auto dirtyTagIt = dirtyTagsByGuidHash.find(guid);
            if (dirtyTagIt == dirtyTagsByGuidHash.end()) {
                QNTRACE(QStringLiteral("Tag guid ") << guid << QStringLiteral(" doesn't appear within the list of dirty tags"));
                Q_UNUSED(tagGuidsToExpunge.insert(guid))
            }
            else {
                QNTRACE(QStringLiteral("Tag guid ") << guid << QStringLiteral(" appears within the list of dirty tags"));
                dirtyTagsToUpdate << dirtyTagIt.value();
            }
        }
    }

    if (!m_caches.m_savedSearchSyncCache.isNull())
    {
        const QHash<QString,QString> & savedSearchNameByGuidHash = m_caches.m_savedSearchSyncCache->nameByGuidHash();
        const QHash<QString,SavedSearch> & dirtySavedSearchesByGuid = m_caches.m_savedSearchSyncCache->dirtySavedSearchesByGuid();

        for(auto it = savedSearchNameByGuidHash.constBegin(), end = savedSearchNameByGuidHash.constEnd(); it != end; ++it)
        {
            const QString & guid = it.key();
            if (m_syncedGuids.m_syncedSavedSearchGuids.find(guid) != m_syncedGuids.m_syncedSavedSearchGuids.end()) {
                QNTRACE(QStringLiteral("Found saved search guid ") << guid << QStringLiteral(" within the synced ones"));
                continue;
            }

            auto dirtySavedSearchIt = dirtySavedSearchesByGuid.find(guid);
            if (dirtySavedSearchIt == dirtySavedSearchesByGuid.end()) {
                QNTRACE(QStringLiteral("Saved search guid ") << guid << QStringLiteral(" doesn't appear within the list of dirty searches"));
                Q_UNUSED(savedSearchGuidsToExpunge.insert(guid))
            }
            else {
                QNTRACE(QStringLiteral("Saved search guid ") << guid << QStringLiteral(" appears within the list of dirty saved searches"));
                dirtySavedSearchesToUpdate << dirtySavedSearchIt.value();
            }
        }
    }
    else
    {
        QNWARNING(QStringLiteral("Skipping already expired saved search sync cache"));
    }

    const NoteSyncCache::NoteGuidToLocalUidBimap & noteGuidToLocalUidBimap = m_noteSyncCache.noteGuidToLocalUidBimap();
    const QHash<QString,Note> & dirtyNotesByGuid = m_noteSyncCache.dirtyNotesByGuid();
    for(auto it = noteGuidToLocalUidBimap.left.begin(), end = noteGuidToLocalUidBimap.left.end(); it != end; ++it)
    {
        const QString & guid = it->first;
        if (m_syncedGuids.m_syncedNoteGuids.find(guid) != m_syncedGuids.m_syncedNoteGuids.end()) {
            QNTRACE(QStringLiteral("Found note guid ") << guid << QStringLiteral(" within the synced ones"));
            continue;
        }

        auto dirtyNoteIt = dirtyNotesByGuid.find(guid);
        if (dirtyNoteIt == dirtyNotesByGuid.end()) {
            QNTRACE(QStringLiteral("Note guid ") << guid << QStringLiteral(" doesn't appear within the list of dirty notes"));
            Q_UNUSED(noteGuidsToExpunge.insert(guid))
        }
        else {
            QNTRACE(QStringLiteral("Note guid ") << guid << QStringLiteral(" appears within the list of dirty notes"));
            dirtyNotesToUpdate << dirtyNoteIt.value();
        }
    }

    if (notebookGuidsToExpunge.isEmpty() &&
        tagGuidsToExpunge.isEmpty() &&
        savedSearchGuidsToExpunge.isEmpty() &&
        noteGuidsToExpunge.isEmpty() &&
        dirtyNotebooksToUpdate.isEmpty() &&
        dirtyTagsToUpdate.isEmpty() &&
        dirtySavedSearchesToUpdate.isEmpty() &&
        dirtyNotesToUpdate.isEmpty())
    {
        QNDEBUG(QStringLiteral("Nothing is required to be updated or expunged"));

        m_inProgress = false;

        QNDEBUG(QStringLiteral("Emitting the finished signal"));
        emit finished();

        return;
    }

    for(auto it = notebookGuidsToExpunge.constBegin(), end = notebookGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        Notebook dummyNotebook;
        dummyNotebook.unsetLocalUid();
        dummyNotebook.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNotebookRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to expunge notebook: request id = ") << requestId
                << QStringLiteral(", notebook guid = ") << guid);
        emit expungeNotebook(dummyNotebook, requestId);
    }

    for(auto it = tagGuidsToExpunge.constBegin(), end = tagGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        Tag dummyTag;
        dummyTag.unsetLocalUid();
        dummyTag.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeTagRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to expunge tag: request id = ") << requestId
                << QStringLiteral(", tag guid = ") << guid);
        emit expungeTag(dummyTag, requestId);
    }

    for(auto it = savedSearchGuidsToExpunge.constBegin(), end = savedSearchGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        SavedSearch dummySearch;
        dummySearch.unsetLocalUid();
        dummySearch.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeSavedSearchRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to expunge saved search: request id = ") << requestId
                << QStringLiteral(", saved search guid = ") << guid);
        emit expungeSavedSearch(dummySearch, requestId);
    }

    for(auto it = noteGuidsToExpunge.constBegin(), end = noteGuidsToExpunge.constEnd(); it != end; ++it)
    {
        const QString & guid = *it;
        Note dummyNote;
        dummyNote.unsetLocalUid();
        dummyNote.setGuid(guid);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_expungeNoteRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to expunge note: request id = ") << requestId
                << QStringLiteral(", note guid = ") << guid);
        emit expungeNote(dummyNote, requestId);
    }

    for(auto it = dirtyNotebooksToUpdate.begin(), end = dirtyNotebooksToUpdate.end(); it != end; ++it)
    {
        Notebook & notebook = *it;
        notebook.setGuid(QString());
        notebook.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateNotebookRequestId.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to update notebook: request id = ") << requestId
                << QStringLiteral(", notebook: ") << notebook);
        emit updateNotebook(notebook, requestId);
    }

    for(auto it = dirtyTagsToUpdate.begin(), end = dirtyTagsToUpdate.end(); it != end; ++it)
    {
        Tag & tag = *it;
        tag.setGuid(QString());
        tag.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateTagRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to update tag: request id = ") << requestId
                << QStringLiteral(", tag: ") << tag);
        emit updateTag(tag, requestId);
    }

    for(auto it = dirtySavedSearchesToUpdate.begin(), end = dirtySavedSearchesToUpdate.end(); it != end; ++it)
    {
        SavedSearch & search = *it;
        search.setGuid(QString());
        search.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateSavedSearchRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to update saved search: request id = ") << requestId
                << QStringLiteral(", saved search: ") << search);
        emit updateSavedSearch(search, requestId);
    }

    for(auto it = dirtyNotesToUpdate.begin(), end = dirtyNotesToUpdate.end(); it != end; ++it)
    {
        Note & note = *it;
        note.setGuid(QString());
        note.setNotebookGuid(QString());    // NOTE: it is just in case one of notebooks stripped off the guid was this note's notebook
        note.setUpdateSequenceNumber(-1);

        QUuid requestId = QUuid::createUuid();
        Q_UNUSED(m_updateNoteRequestIds.insert(requestId))
        QNTRACE(QStringLiteral("Emitting the request to update note: request id = ") << requestId
                << QStringLiteral(", note: ") << note);
        emit updateNote(note, /* update resources = */ false, /* update tags = */ false, requestId);
    }
}

} // namespace quentier
