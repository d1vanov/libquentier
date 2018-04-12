#include "FakeNoteStore.h"
#include <quentier/utility/UidGenerator.h>
#include <quentier/types/Resource.h>
#include <QTimerEvent>
#include <QRegExp>

namespace quentier {

FakeNoteStore::FakeNoteStore(QObject * parent) :
    INoteStore(QSharedPointer<qevercloud::NoteStore>(new qevercloud::NoteStore), parent),
    m_savedSearches(),
    m_expungedSavedSearchGuids(),
    m_tags(),
    m_expungedTagGuids(),
    m_notebooks(),
    m_expungedNotebookGuids(),
    m_notes(),
    m_expungedNoteGuids(),
    m_shouldTriggerRateLimitReachOnNextCall(false),
    m_getNoteAsyncDelayTimerIds(),
    m_getResourceAsyncDelayTimerIds(),
    m_maxNumSavedSearches(static_cast<quint32>(qevercloud::EDAM_USER_SAVED_SEARCHES_MAX)),
    m_maxNumTags(static_cast<quint32>(qevercloud::EDAM_USER_TAGS_MAX)),
    m_maxNumNotebooks(static_cast<quint32>(qevercloud::EDAM_USER_NOTEBOOKS_MAX)),
    m_maxNumNotes(static_cast<quint32>(qevercloud::EDAM_USER_NOTES_MAX)),
    m_maxNoteSize(static_cast<quint64>(qevercloud::EDAM_NOTE_SIZE_MAX_FREE)),
    m_maxNumResourcesPerNote(static_cast<quint32>(qevercloud::EDAM_NOTE_RESOURCES_MAX)),
    m_maxNumTagsPerNote(static_cast<quint32>(qevercloud::EDAM_NOTE_TAGS_MAX)),
    m_maxResourceSize(static_cast<quint64>(qevercloud::EDAM_RESOURCE_SIZE_MAX_FREE))
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

void FakeNoteStore::triggerRateLimitReachOnNextCall()
{
    m_shouldTriggerRateLimitReachOnNextCall = true;
}

quint32 FakeNoteStore::maxNumSavedSearches() const
{
    return m_maxNumSavedSearches;
}

void FakeNoteStore::setMaxNumSavedSearches(const quint32 maxNumSavedSearches)
{
    m_maxNumSavedSearches = maxNumSavedSearches;
}

quint32 FakeNoteStore::maxNumTags() const
{
    return m_maxNumTags;
}

void FakeNoteStore::setMaxNumTags(const quint32 maxNumTags)
{
    m_maxNumTags = maxNumTags;
}

quint32 FakeNoteStore::maxNumNotebooks() const
{
    return m_maxNumNotebooks;
}

void FakeNoteStore::setMaxNumNotebooks(const quint32 maxNumNotebooks)
{
    m_maxNumNotebooks = maxNumNotebooks;
}

quint32 FakeNoteStore::maxNumNotes() const
{
    return m_maxNumNotes;
}

void FakeNoteStore::setMaxNumNotes(const quint32 maxNumNotes)
{
    m_maxNumNotes = maxNumNotes;
}

quint64 FakeNoteStore::maxNoteSize() const
{
    return m_maxNoteSize;
}

void FakeNoteStore::setMaxNoteSize(const quint64 maxNoteSize)
{
    m_maxNoteSize = maxNoteSize;
}

quint32 FakeNoteStore::maxNumResourcesPerNote() const
{
    return m_maxNumResourcesPerNote;
}

void FakeNoteStore::setMaxNumResourcesPerNote(const quint32 maxNumResourcesPerNote)
{
    m_maxNumResourcesPerNote = maxNumResourcesPerNote;
}

quint32 FakeNoteStore::maxNumTagsPerNote() const
{
    return m_maxNumTagsPerNote;
}

void FakeNoteStore::setMaxNumTagsPerNote(const quint32 maxNumTagsPerNote)
{
    m_maxNumTagsPerNote = maxNumTagsPerNote;
}

quint64 FakeNoteStore::maxResourceSize() const
{
    return m_maxResourceSize;
}

void FakeNoteStore::setMaxResourceSize(const quint64 maxResourceSize)
{
    m_maxResourceSize = maxResourceSize;
}

INoteStore * FakeNoteStore::create() const
{
    return new FakeNoteStore;
}

void FakeNoteStore::stop()
{
    for(auto it = m_getNoteAsyncDelayTimerIds.begin(), end = m_getNoteAsyncDelayTimerIds.end(); it != end; ++it) {
        killTimer(*it);
    }
    m_getNoteAsyncDelayTimerIds.clear();

    for(auto it = m_getResourceAsyncDelayTimerIds.begin(), end = m_getResourceAsyncDelayTimerIds.end(); it != end; ++it) {
        killTimer(*it);
    }
    m_getResourceAsyncDelayTimerIds.clear();
}

#define CHECK_API_RATE_LIMIT() \
    if (m_shouldTriggerRateLimitReachOnNextCall) { \
        rateLimitSeconds = 0; \
        m_shouldTriggerRateLimitReachOnNextCall = false; \
        return qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED; \
    }

qint32 FakeNoteStore::createNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                     const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

    if (m_notebooks.size() + 1 > m_maxNumNotebooks) {
        errorDescription.setBase(QStringLiteral("Already at max number of notebooks"));
        return qevercloud::EDAMErrorCode::LIMIT_REACHED;
    }

    qint32 checkRes = checkNotebookFields(notebook, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (!linkedNotebookAuthToken.isEmpty() && notebook.isDefaultNotebook()) {
        errorDescription.setBase(QStringLiteral("Linked notebook cannot be set as default notebook"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    NotebookDataByNameUpper & nameIndex = m_notebooks.get<NotebookByNameUpper>();
    auto nameIt = nameIndex.find(notebook.name().toUpper());
    if (nameIt != nameIndex.end()) {
        errorDescription.setBase(QStringLiteral("Notebook with the specified name already exists"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    notebook.setGuid(UidGenerator::Generate());
    Q_UNUSED(m_notebooks.insert(notebook))
    return 0;
}

qint32 FakeNoteStore::updateNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                     const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

    if (!notebook.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Notebook.guid"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    qint32 checkRes = checkNotebookFields(notebook, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (!linkedNotebookAuthToken.isEmpty() && notebook.isDefaultNotebook()) {
        errorDescription.setBase(QStringLiteral("Linked notebook cannot be set as default notebook"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    NotebookDataByGuid & index = m_notebooks.get<NotebookByGuid>();
    auto it = index.find(notebook.guid());
    if (it == index.end()) {
        errorDescription.setBase(QStringLiteral("Notebook with the specified guid doesn't exist"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    Q_UNUSED(index.replace(it, notebook))
    return 0;
}

qint32 FakeNoteStore::createNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                 const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

    // TODO: implement
    Q_UNUSED(tag)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    Q_UNUSED(linkedNotebookAuthToken)
    return 0;
}

qint32 FakeNoteStore::createSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    // TODO: implement
    Q_UNUSED(savedSearch)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::updateSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    // TODO: implement
    Q_UNUSED(savedSearch)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::getSyncState(qevercloud::SyncState & syncState, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    Q_UNUSED(syncState)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

qint32 FakeNoteStore::getSyncChunk(const qint32 afterUSN, const qint32 maxEntries, const qevercloud::SyncChunkFilter & filter,
                                   qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                   qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

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
    CHECK_API_RATE_LIMIT()

    // TODO: implement
    Q_UNUSED(shareKey)
    Q_UNUSED(authResult)
    Q_UNUSED(errorDescription)
    Q_UNUSED(rateLimitSeconds)
    return 0;
}

void FakeNoteStore::timerEvent(QTimerEvent * pEvent)
{
    if (Q_UNLIKELY(!pEvent)) {
        return;
    }

    auto noteIt = m_getNoteAsyncDelayTimerIds.find(pEvent->timerId());
    if (noteIt != m_getNoteAsyncDelayTimerIds.end())
    {
        // TODO: get note and emit the corresponding signal
        return;
    }

    auto resourceIt = m_getResourceAsyncDelayTimerIds.find(pEvent->timerId());
    if (resourceIt != m_getResourceAsyncDelayTimerIds.end())
    {
        // TODO: get resource and emit the corresponding signal
        return;
    }

    INoteStore::timerEvent(pEvent);
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

qint32 FakeNoteStore::checkNotebookFields(const Notebook & notebook, ErrorString & errorDescription) const
{
    if (!notebook.hasName()) {
        errorDescription.setBase(QStringLiteral("Notebook name is not set"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    const QString & notebookName = notebook.name();
    if (notebookName.size() < qevercloud::EDAM_NOTEBOOK_NAME_LEN_MIN) {
        errorDescription.setBase(QStringLiteral("Notebook name length is too small"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (notebookName.size() > qevercloud::EDAM_NOTEBOOK_NAME_LEN_MAX) {
        errorDescription.setBase(QStringLiteral("Notebook name length is too large"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (notebookName != notebookName.trimmed()) {
        errorDescription.setBase(QStringLiteral("Notebook name cannot begin or end with whitespace"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    QRegExp notebookRegExp(qevercloud::EDAM_NOTEBOOK_NAME_REGEX);
    if (!notebookRegExp.exactMatch(notebookName)) {
        errorDescription.setBase(QStringLiteral("Notebook name doesn't match the mandatory regex"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (notebook.hasStack())
    {
        const QString & notebookStack = notebook.stack();

        if (notebookStack.size() < qevercloud::EDAM_NOTEBOOK_STACK_LEN_MIN) {
            errorDescription.setBase(QStringLiteral("Notebook stack's length is too small"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        if (notebookStack.size() > qevercloud::EDAM_NOTEBOOK_STACK_LEN_MAX) {
            errorDescription.setBase(QStringLiteral("Notebook stack's length is too large"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        QRegExp notebookStackRegExp(qevercloud::EDAM_NOTEBOOK_STACK_REGEX);
        if (!notebookStackRegExp.exactMatch(notebookStack)) {
            errorDescription.setBase(QStringLiteral("Notebook's stack doesn't match the mandatory regex"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }
    }

    if (notebook.hasPublishingUri())
    {
        const QString & publishingUri = notebook.publishingUri();

        if (publishingUri.size() < qevercloud::EDAM_PUBLISHING_URI_LEN_MIN) {
            errorDescription.setBase(QStringLiteral("Notebook publishing uri length is too small"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        if (publishingUri.size() > qevercloud::EDAM_PUBLISHING_URI_LEN_MAX) {
            errorDescription.setBase(QStringLiteral("Notebook publising uri length is too large"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        for(auto it = qevercloud::EDAM_PUBLISHING_URI_PROHIBITED.begin(),
            end = qevercloud::EDAM_PUBLISHING_URI_PROHIBITED.end(); it != end; ++it)
        {
            if (publishingUri == *it) {
                errorDescription.setBase(QStringLiteral("Prohibited publishing URI value is set"));
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
            }
        }

        QRegExp publishingUriRegExp(qevercloud::EDAM_PUBLISHING_URI_REGEX);
        if (!publishingUriRegExp.exactMatch(publishingUri)) {
            errorDescription.setBase(QStringLiteral("Publishing URI doesn't match the mandatory regex"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }
    }

    if (notebook.hasPublishingPublicDescription())
    {
        const QString & description = notebook.publishingPublicDescription();

        if (description.size() < qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MIN) {
            errorDescription.setBase(QStringLiteral("Publishing description length is too small"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        if (description.size() > qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MAX) {
            errorDescription.setBase(QStringLiteral("Publishing description length is too large"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        QRegExp publishingDescriptionRegExp(qevercloud::EDAM_PUBLISHING_DESCRIPTION_REGEX);
        if (!publishingDescriptionRegExp.exactMatch(description)) {
            errorDescription.setBase(QStringLiteral("Notebook description doesn't match the mandatory regex"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkNoteFields(const Note & note, ErrorString & errorDescription) const
{
    if (note.hasTitle())
    {
        const QString & title = note.title();

        if (title.size() < qevercloud::EDAM_NOTE_TITLE_LEN_MIN) {
            errorDescription.setBase(QStringLiteral("Note title length is too small"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        if (title.size() > qevercloud::EDAM_NOTE_TITLE_LEN_MAX) {
            errorDescription.setBase(QStringLiteral("Note title length is too large"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        QRegExp noteTitleRegExp(qevercloud::EDAM_NOTE_TITLE_REGEX);
        if (!noteTitleRegExp.exactMatch(title)) {
            errorDescription.setBase(QStringLiteral("Note title doesn't match the mandatory regex"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }
    }

    if (note.hasContent())
    {
        const QString & content = note.content();

        if (content.size() < qevercloud::EDAM_NOTE_CONTENT_LEN_MIN) {
            errorDescription.setBase(QStringLiteral("Note content length is too small"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        if (content.size() > qevercloud::EDAM_NOTE_CONTENT_LEN_MAX) {
            errorDescription.setBase(QStringLiteral("Note content length is too large"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }
    }

    if (note.hasResources())
    {
        QList<Resource> resources = note.resources();
        for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it)
        {
            const Resource & resource = *it;
            qint32 checkRes = checkResourceFields(resource, errorDescription);
            if (checkRes != 0) {
                return checkRes;
            }
        }
    }

    // TODO: continue here

    return 0;
}

qint32 FakeNoteStore::checkResourceFields(const Resource & resource, ErrorString & errorDescription) const
{
    if (resource.hasMime())
    {
        const QString & mime = resource.mime();

        if (mime.size() < qevercloud::EDAM_MIME_LEN_MIN) {
            errorDescription.setBase(QStringLiteral("Note's resource mime type length is too small"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        if (mime.size() > qevercloud::EDAM_MIME_LEN_MAX) {
            errorDescription.setBase(QStringLiteral("Note's resource mime type length is too large"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        QRegExp mimeRegExp(qevercloud::EDAM_MIME_REGEX);
        if (!mimeRegExp.exactMatch(mime)) {
            errorDescription.setBase(QStringLiteral("Note's resource mime type doesn't match the mandatory regex"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }
    }

    if (resource.hasResourceAttributes())
    {
        const qevercloud::ResourceAttributes & attributes = resource.resourceAttributes();

#define CHECK_RESOURCE_STRING_ATTRIBUTE(attr) \
        if (attributes.attr.isSet()) \
        { \
            if (attributes.attr.ref().size() < qevercloud::EDAM_ATTRIBUTE_LEN_MIN) { \
                errorDescription.setBase(QStringLiteral("Note's resource attribute length is too small: ") + QString::fromUtf8( #attr )); \
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT; \
            } \
            \
            if (attributes.attr.ref().size() < qevercloud::EDAM_ATTRIBUTE_LEN_MAX) { \
                errorDescription.setBase(QStringLiteral("Note's resource attribute length is too large: ") + QString::fromUtf8( #attr )); \
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT; \
            } \
        }

        CHECK_RESOURCE_STRING_ATTRIBUTE(sourceURL)
        CHECK_RESOURCE_STRING_ATTRIBUTE(cameraMake)
        CHECK_RESOURCE_STRING_ATTRIBUTE(cameraModel)

#undef CHECK_RESOURCE_STRING_ATTRIBUTE

        if (attributes.applicationData.isSet())
        {
            // const qevercloud::LazyMap & appData = attributes.applicationData.ref();

            // TODO: continue here
        }
    }

    return 0;
}

} // namespace quentier
