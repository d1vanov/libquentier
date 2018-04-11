#include "FakeNoteStore.h"

namespace quentier {

FakeNoteStore::FakeNoteStore(QObject * parent) :
    INoteStore(QSharedPointer<qevercloud::NoteStore>(new qevercloud::NoteStore), parent)
{}

QHash<QString,qevercloud::SavedSearch> FakeNoteStore::savedSearches() const
{
    QHash<QString,qevercloud::SavedSearch> result;
    result.reserve(static_cast<int>(m_savedSearches.size()));

    const SavedSearchDataByGuid & savedSearchByGuid = m_savedSearches.get<SavedSearchByGuid>();
    for(auto it = savedSearchByGuid.begin(), end = savedSearchByGuid.end(); it != end; ++it) {
        result[it->guid()] = it->qevercloudSavedSearch();
    }

    return result;
}

bool FakeNoteStore::setSavedSearch(SavedSearch & search, ErrorString & errorDescription)
{
    if (!search.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set saved search without guid"));
        return false;
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(m_savedSearches.insert(search))
    return true;
}

const SavedSearch * FakeNoteStore::findSavedSearch(const QString & guid) const
{
    const SavedSearchDataByGuid & index = m_savedSearches.get<SavedSearchByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return Q_NULLPTR;
    }

    const SavedSearch & search = *it;
    return &search;
}

bool FakeNoteStore::removeSavedSearch(const QString & guid)
{
    SavedSearchDataByGuid & index = m_savedSearches.get<SavedSearchByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedSavedSearchGuid(const QString & guid)
{
    Q_UNUSED(m_expungedSavedSearchGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedSavedSearchGuid(const QString & guid) const
{
    return m_expungedSavedSearchGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedSavedSearchGuid(const QString & guid)
{
    auto it = m_expungedSavedSearchGuids.find(guid);
    if (it == m_expungedSavedSearchGuids.end()) {
        return false;
    }

    Q_UNUSED(m_expungedSavedSearchGuids.erase(it))
    return true;
}

QHash<QString,qevercloud::Tag> FakeNoteStore::tags() const
{
    QHash<QString,qevercloud::Tag> result;
    result.reserve(static_cast<int>(m_tags.size()));

    const TagDataByGuid & tagDataByGuid = m_tags.get<TagByGuid>();
    for(auto it = tagDataByGuid.begin(), end = tagDataByGuid.end(); it != end; ++it) {
        result[it->guid()] = it->qevercloudTag();
    }

    return result;
}

bool FakeNoteStore::setTag(Tag & tag, ErrorString & errorDescription)
{
    if (!tag.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set tag without guid"));
        return false;
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    tag.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(m_tags.insert(tag))
    return true;
}

const Tag * FakeNoteStore::findTag(const QString & guid) const
{
    const TagDataByGuid & index = m_tags.get<TagByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return Q_NULLPTR;
    }

    const Tag & tag = *it;
    return &tag;
}

bool FakeNoteStore::removeTag(const QString & guid)
{
    TagDataByGuid & index = m_tags.get<TagByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedTagGuid(const QString & guid)
{
    Q_UNUSED(m_expungedTagGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedTagGuid(const QString & guid) const
{
    return m_expungedTagGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedTagGuid(const QString & guid)
{
    auto it = m_expungedTagGuids.find(guid);
    if (it == m_expungedTagGuids.end()) {
        return false;
    }

    Q_UNUSED(m_expungedTagGuids.erase(it))
    return true;
}

QHash<QString,qevercloud::Notebook> FakeNoteStore::notebooks() const
{
    QHash<QString,qevercloud::Notebook> result;
    result.reserve(static_cast<int>(m_notebooks.size()));

    const NotebookDataByGuid & notebookDataByGuid = m_notebooks.get<NotebookByGuid>();
    for(auto it = notebookDataByGuid.begin(), end = notebookDataByGuid.end(); it != end; ++it) {
        result[it->guid()] = it->qevercloudNotebook();
    }

    return result;
}

bool FakeNoteStore::setNotebook(Notebook & notebook, ErrorString & errorDescription)
{
    if (!notebook.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set notebook without guid"));
        return false;
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    notebook.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(m_notebooks.insert(notebook))
    return true;
}

const Notebook * FakeNoteStore::findNotebook(const QString & guid) const
{
    const NotebookDataByGuid & index = m_notebooks.get<NotebookByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return Q_NULLPTR;
    }

    const Notebook & notebook = *it;
    return &notebook;
}

bool FakeNoteStore::removeNotebook(const QString & guid)
{
    NotebookDataByGuid & index = m_notebooks.get<NotebookByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedNotebookGuid(const QString & guid)
{
    Q_UNUSED(m_expungedNotebookGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedNotebookGuid(const QString & guid) const
{
    return m_expungedNotebookGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedNotebookGuid(const QString & guid)
{
    auto it = m_expungedNotebookGuids.find(guid);
    if (it == m_expungedNotebookGuids.end()) {
        return false;
    }

    Q_UNUSED(m_expungedNotebookGuids.erase(it))
    return true;
}

QHash<QString,qevercloud::Note> FakeNoteStore::notes() const
{
    QHash<QString,qevercloud::Note> result;
    result.reserve(static_cast<int>(m_notes.size()));

    const NoteDataByGuid & noteDataByGuid = m_notes.get<NoteByGuid>();
    for(auto it = noteDataByGuid.begin(), end = noteDataByGuid.end(); it != end; ++it) {
        result[it->guid()] = it->qevercloudNote();
    }

    return result;
}

bool FakeNoteStore::setNote(Note & note, ErrorString & errorDescription)
{
    if (!note.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set note without guid"));
        return false;
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    note.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(m_notes.insert(note))
    return true;
}

const Note * FakeNoteStore::findNote(const QString & guid) const
{
    const NoteDataByGuid & index = m_notes.get<NoteByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return Q_NULLPTR;
    }

    const Note & note = *it;
    return &note;
}

bool FakeNoteStore::removeNote(const QString & guid)
{
    NoteDataByGuid & index = m_notes.get<NoteByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedNoteGuid(const QString & guid)
{
    Q_UNUSED(m_expungedNoteGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedNoteGuid(const QString & guid) const
{
    return m_expungedNoteGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedNoteGuid(const QString & guid)
{
    auto it = m_expungedNoteGuids.find(guid);
    if (it == m_expungedNoteGuids.end()) {
        return false;
    }

    Q_UNUSED(m_expungedNoteGuids.erase(it))
    return true;
}

INoteStore * FakeNoteStore::create() const
{
    return new FakeNoteStore;
}

void FakeNoteStore::stop()
{
    // TODO: implement
}

qint32 FakeNoteStore::createNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                     const QString & linkedNotebookAuthToken)
{
    // TODO: implement
    Q_UNUSED(notebook)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    Q_UNUSED(linkedNotebookAuthToken)
    return 0;
}

qint32 FakeNoteStore::updateNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                     const QString & linkedNotebookAuthToken)
{
    // TODO: implement
    Q_UNUSED(notebook)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    Q_UNUSED(linkedNotebookAuthToken)
    return 0;
}

qint32 FakeNoteStore::createNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                 const QString & linkedNotebookAuthToken)
{
    // TODO: implement
    Q_UNUSED(note)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    Q_UNUSED(linkedNotebookAuthToken)
    return 0;
}

qint32 FakeNoteStore::updateNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                 const QString & linkedNotebookAuthToken)
{
    // TODO: implement
    Q_UNUSED(note)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    Q_UNUSED(linkedNotebookAuthToken)
    return 0;
}

qint32 FakeNoteStore::createTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                const QString & linkedNotebookAuthToken)
{
    // TODO: implement
    Q_UNUSED(tag)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    Q_UNUSED(linkedNotebookAuthToken)
    return 0;
}

qint32 FakeNoteStore::updateTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                const QString & linkedNotebookAuthToken)
{
    // TODO: implement
    Q_UNUSED(tag)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    Q_UNUSED(linkedNotebookAuthToken)
    return 0;
}

qint32 FakeNoteStore::createSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(savedSearch)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::updateSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(savedSearch)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::getSyncState(qevercloud::SyncState & syncState, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    Q_UNUSED(syncState)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::getSyncChunk(const qint32 afterUSN, const qint32 maxEntries, const qevercloud::SyncChunkFilter & filter,
                                   qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                   qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(afterUSN)
    Q_UNUSED(maxEntries)
    Q_UNUSED(filter)
    Q_UNUSED(syncChunk)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::getLinkedNotebookSyncState(const qevercloud::LinkedNotebook & linkedNotebook,
                                                 const QString & authToken, qevercloud::SyncState & syncState,
                                                 ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(linkedNotebook)
    Q_UNUSED(authToken)
    Q_UNUSED(syncState)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::getLinkedNotebookSyncChunk(const qevercloud::LinkedNotebook & linkedNotebook,
                                                 const qint32 afterUSN, const qint32 maxEntries,
                                                 const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
                                                 qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                                 qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(linkedNotebook)
    Q_UNUSED(afterUSN)
    Q_UNUSED(maxEntries)
    Q_UNUSED(linkedNotebookAuthToken)
    Q_UNUSED(fullSyncOnly)
    Q_UNUSED(syncChunk)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::getNote(const bool withContent, const bool withResourcesData,
                              const bool withResourcesRecognition, const bool withResourceAlternateData,
                              Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(withContent)
    Q_UNUSED(withResourcesData)
    Q_UNUSED(withResourcesRecognition)
    Q_UNUSED(withResourceAlternateData)
    Q_UNUSED(note)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

bool FakeNoteStore::getNoteAsync(const bool withContent, const bool withResourceData, const bool withResourcesRecognition,
                                 const bool withResourceAlternateData, const bool withSharedNotes,
                                 const bool withNoteAppDataValues, const bool withResourceAppDataValues,
                                 const bool withNoteLimits, const QString & noteGuid,
                                 const QString & authToken, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(withContent)
    Q_UNUSED(withResourceData)
    Q_UNUSED(withResourcesRecognition)
    Q_UNUSED(withResourceAlternateData)
    Q_UNUSED(withSharedNotes)
    Q_UNUSED(withNoteAppDataValues)
    Q_UNUSED(withResourceAppDataValues)
    Q_UNUSED(withNoteLimits)
    Q_UNUSED(noteGuid)
    Q_UNUSED(authToken)
    Q_UNUSED(errorDescription)
    return false;
}

qint32 FakeNoteStore::getResource(const bool withDataBody, const bool withRecognitionDataBody,
                                  const bool withAlternateDataBody, const bool withAttributes,
                                  const QString & authToken, Resource & resource, ErrorString & errorDescription,
                                  qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(withDataBody)
    Q_UNUSED(withRecognitionDataBody)
    Q_UNUSED(withAlternateDataBody)
    Q_UNUSED(withAttributes)
    Q_UNUSED(authToken)
    Q_UNUSED(resource)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

bool FakeNoteStore::getResourceAsync(const bool withDataBody, const bool withRecognitionDataBody,
                                     const bool withAlternateDataBody, const bool withAttributes, const QString & resourceGuid,
                                     const QString & authToken, ErrorString & errorDescription)
{
    // TODO: implement
    Q_UNUSED(withDataBody)
    Q_UNUSED(withRecognitionDataBody)
    Q_UNUSED(withAlternateDataBody)
    Q_UNUSED(withAttributes)
    Q_UNUSED(resourceGuid)
    Q_UNUSED(authToken)
    Q_UNUSED(errorDescription)
    return false;
}

qint32 FakeNoteStore::authenticateToSharedNotebook(const QString & shareKey, qevercloud::AuthenticationResult & authResult,
                                                   ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    // TODO: implement
    Q_UNUSED(shareKey)
    Q_UNUSED(authResult)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::currentMaxUsn() const
{
    qint32 maxUsn = 0;

    const SavedSearchDataByUSN & savedSearchUsnIndex = m_savedSearches.get<SavedSearchByUSN>();
    if (!savedSearchUsnIndex.empty())
    {
        auto lastSavedSearchIt = savedSearchUsnIndex.end();
        --lastSavedSearchIt;
        if (lastSavedSearchIt->updateSequenceNumber() > maxUsn) {
            maxUsn = lastSavedSearchIt->updateSequenceNumber();
        }
    }

    const TagDataByUSN & tagUsnIndex = m_tags.get<TagByUSN>();
    if (!tagUsnIndex.empty())
    {
        auto lastTagIt = tagUsnIndex.end();
        --lastTagIt;
        if (lastTagIt->updateSequenceNumber() > maxUsn) {
            maxUsn = lastTagIt->updateSequenceNumber();
        }
    }

    const NotebookDataByUSN & notebookUsnIndex = m_notebooks.get<NotebookByUSN>();
    if (!notebookUsnIndex.empty())
    {
        auto lastNotebookIt = notebookUsnIndex.end();
        --lastNotebookIt;
        if (lastNotebookIt->updateSequenceNumber() > maxUsn) {
            maxUsn = lastNotebookIt->updateSequenceNumber();
        }
    }

    const NoteDataByUSN & noteUsnIndex = m_notes.get<NoteByUSN>();
    if (!noteUsnIndex.empty())
    {
        auto lastNoteIt = noteUsnIndex.end();
        --lastNoteIt;
        if (lastNoteIt->updateSequenceNumber() > maxUsn) {
            maxUsn = lastNoteIt->updateSequenceNumber();
        }
    }

    return maxUsn;
}

} // namespace quentier
