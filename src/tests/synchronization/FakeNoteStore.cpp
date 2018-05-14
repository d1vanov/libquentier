/*
 * Copyright 2018 Dmitry Ivanov
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

#include "FakeNoteStore.h"
#include <quentier/utility/UidGenerator.h>
#include <quentier/types/Resource.h>
#include <quentier/logging/QuentierLogger.h>
#include <QTimerEvent>
#include <QRegExp>
#include <QDateTime>
#include <algorithm>
#include <limits>

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
    m_resources(),
    m_linkedNotebooks(),
    m_expungedLinkedNotebookGuids(),
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
    m_maxResourceSize(static_cast<quint64>(qevercloud::EDAM_RESOURCE_SIZE_MAX_FREE)),
    m_syncState(),
    m_linkedNotebookSyncStates(),
    m_authenticationToken(),
    m_linkedNotebookAuthTokens(),
    m_getNoteAsyncRequests(),
    m_getResourceAsyncRequests()
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

    if (!search.hasName()) {
        errorDescription.setBase(QStringLiteral("Can't set saved search without name"));
        return false;
    }

    SavedSearchDataByNameUpper & nameIndex = m_savedSearches.get<SavedSearchByNameUpper>();
    auto nameIt = nameIndex.find(search.name().toUpper());
    while(nameIt != nameIndex.end())
    {
        if (nameIt->guid() == search.guid()) {
            break;
        }

        QString name = nextName(search.name());
        search.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(removeExpungedSavedSearchGuid(search.guid()))

    SavedSearchDataByGuid & savedSearchGuidIndex = m_savedSearches.get<SavedSearchByGuid>();
    auto it = savedSearchGuidIndex.find(search.guid());
    if (it == savedSearchGuidIndex.end()) {
        Q_UNUSED(m_savedSearches.insert(search))
    }
    else {
        Q_UNUSED(savedSearchGuidIndex.replace(it, search))
    }

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
    Q_UNUSED(removeSavedSearch(guid))
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
    QNDEBUG(QStringLiteral("FakeNoteStore::setTag: tag = ") << tag);

    if (!tag.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set tag without guid"));
        return false;
    }

    if (!tag.hasName()) {
        errorDescription.setBase(QStringLiteral("Can't set tag without name"));
        return false;
    }

    if (tag.hasLinkedNotebookGuid())
    {
        const QString & linkedNotebookGuid = tag.linkedNotebookGuid();

        const LinkedNotebookDataByGuid & index = m_linkedNotebooks.get<LinkedNotebookByGuid>();
        auto it = index.find(linkedNotebookGuid);
        if (it == index.end()) {
            errorDescription.setBase(QStringLiteral("Can't set tag with linked notebook guid corresponding to no existing linked notebook"));
            return false;
        }
    }

    TagDataByNameUpper & nameIndex = m_tags.get<TagByNameUpper>();
    auto nameIt = nameIndex.find(tag.name().toUpper());
    while(nameIt != nameIndex.end())
    {
        if (nameIt->guid() == tag.guid()) {
            break;
        }

        QString name = nextName(tag.name());
        tag.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    qint32 maxUsn = currentMaxUsn(tag.hasLinkedNotebookGuid() ? tag.linkedNotebookGuid() : QString());
    ++maxUsn;
    tag.setUpdateSequenceNumber(maxUsn);

    if (!tag.hasLinkedNotebookGuid()) {
        Q_UNUSED(removeExpungedTagGuid(tag.guid()))
    }

    TagDataByGuid & tagGuidIndex = m_tags.get<TagByGuid>();
    auto tagIt = tagGuidIndex.find(tag.guid());
    if (tagIt == tagGuidIndex.end()) {
        Q_UNUSED(m_tags.insert(tag))
    }
    else {
        Q_UNUSED(tagGuidIndex.replace(tagIt, tag))
    }

    QNDEBUG(QStringLiteral("Tag with complemented fields: ") << tag);
    return true;
}

const Tag * FakeNoteStore::findTag(const QString & guid) const
{
    const TagDataByGuid & index = m_tags.get<TagByGuid>();
    auto it = index.find(guid);
    if (it != index.end()) {
        return &(*it);
    }

    return Q_NULLPTR;
}

bool FakeNoteStore::removeTag(const QString & guid)
{
    TagDataByGuid & index = m_tags.get<TagByGuid>();
    auto tagIt = index.find(guid);
    if (tagIt == index.end()) {
        return false;
    }

    const TagDataByParentTagGuid & parentTagGuidIndex = m_tags.get<TagByParentTagGuid>();
    auto range = parentTagGuidIndex.equal_range(guid);
    QStringList childTagGuids;
    childTagGuids.reserve(static_cast<int>(std::distance(range.first, range.second)));
    for(auto it = range.first; it != range.second; ++it) {
        childTagGuids << it->guid();
    }

    bool removedChildTags = false;
    for(auto it = childTagGuids.constBegin(), end = childTagGuids.constEnd(); it != end; ++it) {
        removedChildTags |= removeTag(*it);
    }

    // NOTE: have to once again evaluate the iterator if we deleted any child tags
    // since the deletion of child tags could cause the invalidation of the previously
    // found iterator
    if (removedChildTags)
    {
        tagIt = index.find(guid);
        if (Q_UNLIKELY(tagIt == index.end())) {
            QNWARNING(QStringLiteral("Tag to be removed is not not found after the removal of its child tags: guid = ") << guid);
            return false;
        }
    }

    Q_UNUSED(index.erase(tagIt))
    return true;
}

void FakeNoteStore::setExpungedTagGuid(const QString & guid)
{
    Q_UNUSED(removeTag(guid))
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

    if (!notebook.hasName()) {
        errorDescription.setBase(QStringLiteral("Can't set notebook without name"));
        return false;
    }

    if (notebook.hasLinkedNotebookGuid())
    {
        const QString & linkedNotebookGuid = notebook.linkedNotebookGuid();

        const LinkedNotebookDataByGuid & index = m_linkedNotebooks.get<LinkedNotebookByGuid>();
        auto it = index.find(linkedNotebookGuid);
        if (it == index.end()) {
            errorDescription.setBase(QStringLiteral("Can't set notebook with linked notebook guid corresponding to no existing linked notebook"));
            return false;
        }
    }

    NotebookDataByNameUpper & nameIndex = m_notebooks.get<NotebookByNameUpper>();
    auto nameIt = nameIndex.find(notebook.name().toUpper());
    while(nameIt != nameIndex.end())
    {
        if (nameIt->guid() == notebook.guid()) {
            break;
        }

        QString name = nextName(notebook.name());
        notebook.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    qint32 maxUsn = currentMaxUsn(notebook.hasLinkedNotebookGuid() ? notebook.linkedNotebookGuid() : QString());
    ++maxUsn;
    notebook.setUpdateSequenceNumber(maxUsn);

    if (!notebook.hasLinkedNotebookGuid()) {
        Q_UNUSED(removeExpungedNotebookGuid(notebook.guid()))
    }

    NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(notebook.guid());
    if (notebookIt == notebookGuidIndex.end()) {
        Q_UNUSED(m_notebooks.insert(notebook))
    }
    else {
        Q_UNUSED(notebookGuidIndex.replace(notebookIt, notebook))
    }

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
    auto notebookIt = index.find(guid);
    if (notebookIt == index.end()) {
        return false;
    }

    const NoteDataByNotebookGuid & noteDataByNotebookGuid = m_notes.get<NoteByNotebookGuid>();
    auto range = noteDataByNotebookGuid.equal_range(guid);
    QStringList noteGuids;
    noteGuids.reserve(static_cast<int>(std::distance(range.first, range.second)));
    for(auto it = range.first; it != range.second; ++it) {
        noteGuids << it->guid();
    }

    for(auto it = noteGuids.constBegin(), end = noteGuids.constEnd(); it != end; ++it) {
        removeNote(*it);
    }

    Q_UNUSED(index.erase(notebookIt))
    return true;
}

QList<const Notebook*> FakeNoteStore::findNotebooksForLinkedNotebookGuid(const QString & linkedNotebookGuid) const
{
    const NotebookDataByLinkedNotebookGuid & index = m_notebooks.get<NotebookByLinkedNotebookGuid>();
    auto range = index.equal_range(linkedNotebookGuid);
    QList<const Notebook*> notebooks;
    notebooks.reserve(static_cast<int>(std::distance(range.first, range.second)));
    for(auto it = range.first; it != range.second; ++it) {
        notebooks << &(*it);
    }
    return notebooks;
}

void FakeNoteStore::setExpungedNotebookGuid(const QString & guid)
{
    Q_UNUSED(removeNotebook(guid))
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

    if (!note.hasNotebookGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set note without notebook guid"));
        return false;
    }

    const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(note.notebookGuid());
    if (notebookIt == notebookGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("Can't set note: no notebook was found for it by guid"));
        return false;
    }

    qint32 maxUsn = currentMaxUsn(notebookIt->hasLinkedNotebookGuid() ? notebookIt->linkedNotebookGuid() : QString());
    ++maxUsn;
    note.setUpdateSequenceNumber(maxUsn);

    if (!notebookIt->hasLinkedNotebookGuid()) {
        Q_UNUSED(removeExpungedNoteGuid(note.guid()))
    }

    NoteDataByGuid & noteGuidIndex = m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(note.guid());
    if (noteIt == noteGuidIndex.end()) {
        auto insertResult = noteGuidIndex.insert(note);
        noteIt = insertResult.first;
    }

    if (note.hasResources())
    {
        QList<Resource> resources = note.resources();
        for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
        {
            Resource & resource = *it;

            if (!resource.hasGuid()) {
                resource.setGuid(UidGenerator::Generate());
            }

            if (!resource.hasNoteGuid()) {
                resource.setNoteGuid(note.guid());
            }

            if (!setResource(resource, errorDescription)) {
                return false;
            }
        }

        QList<Resource> originalResources = resources;

        for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
        {
            Resource & resource = *it;

            // Won't store resource binary data along with notes
            resource.setDataBody(QByteArray());
            resource.setRecognitionDataBody(QByteArray());
            resource.setAlternateDataBody(QByteArray());
        }

        note.setResources(resources);
        Q_UNUSED(noteGuidIndex.replace(noteIt, note))
        note.setResources(originalResources);
    }
    else
    {
        Q_UNUSED(noteGuidIndex.replace(noteIt, note))
    }

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

    const Note & note = *it;
    if (note.hasResources())
    {
        QList<Resource> resources = note.resources();
        for(auto resIt = resources.constBegin(), resEnd = resources.constEnd(); resIt != resEnd; ++resIt) {
            removeResource(resIt->guid());
        }
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedNoteGuid(const QString & guid)
{
    Q_UNUSED(removeNote(guid))
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

QHash<QString,qevercloud::Resource> FakeNoteStore::resources() const
{
    QHash<QString,qevercloud::Resource> result;
    result.reserve(static_cast<int>(m_resources.size()));

    const ResourceDataByGuid & resourceDataByGuid = m_resources.get<ResourceByGuid>();
    for(auto it = resourceDataByGuid.begin(), end = resourceDataByGuid.end(); it != end; ++it) {
        result[it->guid()] = it->qevercloudResource();
    }

    return result;
}

bool FakeNoteStore::setResource(Resource & resource, ErrorString & errorDescription)
{
    if (!resource.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set resource without guid"));
        return false;
    }

    if (!resource.hasNoteGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set resource without note guid"));
        return false;
    }

    NoteDataByGuid & noteGuidIndex = m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(resource.noteGuid());
    if (noteIt == noteGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("Can't set resource: no note was found for it by guid"));
        return false;
    }

    const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(noteIt->notebookGuid());
    if (notebookIt == notebookGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("Can't set resource: no notebook was found for resource's note by notebook guid"));
        return false;
    }

    if (!resource.hasUpdateSequenceNumber()) {
        qint32 maxUsn = currentMaxUsn(notebookIt->hasLinkedNotebookGuid() ? notebookIt->linkedNotebookGuid() : QString());
        ++maxUsn;
        resource.setUpdateSequenceNumber(maxUsn);
    }

    ResourceDataByGuid & resourceGuidIndex = m_resources.get<ResourceByGuid>();
    auto it = resourceGuidIndex.find(resource.guid());
    if (it == resourceGuidIndex.end()) {
        Q_UNUSED(resourceGuidIndex.insert(resource))
    }
    else {
        Q_UNUSED(resourceGuidIndex.replace(it, resource))
    }

    if (!noteIt->isDirty()) {
        Note note(*noteIt);
        note.setDirty(true);
        noteGuidIndex.replace(noteIt, note);
    }

    return true;
}

const Resource * FakeNoteStore::findResource(const QString & guid) const
{
    const ResourceDataByGuid & index = m_resources.get<ResourceByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return Q_NULLPTR;
    }

    const Resource & resource = *it;
    return &resource;
}

bool FakeNoteStore::removeResource(const QString & guid)
{
    ResourceDataByGuid & index = m_resources.get<ResourceByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    const QString & noteGuid = it->noteGuid();
    NoteDataByGuid & noteGuidIndex = m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(noteGuid);
    if (noteIt != noteGuidIndex.end()) {
        Note note = *noteIt;
        note.removeResource(*it);
        Q_UNUSED(noteGuidIndex.replace(noteIt, note))
    }
    else {
        QNWARNING(QStringLiteral("Found no note corresponding to the removed resource: ") << *it);
    }

    Q_UNUSED(index.erase(it))
    return true;
}

QHash<QString,qevercloud::LinkedNotebook> FakeNoteStore::linkedNotebooks() const
{
    QHash<QString,qevercloud::LinkedNotebook> result;
    result.reserve(static_cast<int>(m_linkedNotebooks.size()));

    const LinkedNotebookDataByGuid & linkedNotebookDataByGuid = m_linkedNotebooks.get<LinkedNotebookByGuid>();
    for(auto it = linkedNotebookDataByGuid.begin(), end = linkedNotebookDataByGuid.end(); it != end; ++it) {
        result[it->guid()] = it->qevercloudLinkedNotebook();
    }

    return result;
}

bool FakeNoteStore::setLinkedNotebook(LinkedNotebook & linkedNotebook, ErrorString & errorDescription)
{
    if (!linkedNotebook.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Can't set linked notebook without guid"));
        return false;
    }

    if (!linkedNotebook.hasUsername()) {
        errorDescription.setBase(QStringLiteral("Can't set linked notebook without username"));
        return false;
    }

    if (!linkedNotebook.hasShardId() && !linkedNotebook.hasUri()) {
        errorDescription.setBase(QStringLiteral("Can't set linked notebook without either shard id or uri"));
        return false;
    }

    if (!linkedNotebook.hasSharedNotebookGlobalId()) {
        linkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    linkedNotebook.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(removeExpungedLinkedNotebookGuid(linkedNotebook.guid()))

    LinkedNotebookDataByGuid & index = m_linkedNotebooks.get<LinkedNotebookByGuid>();
    auto it = index.find(linkedNotebook.guid());
    if (it == index.end()) {
        Q_UNUSED(index.insert(linkedNotebook))
    }
    else {
        Q_UNUSED(index.replace(it, linkedNotebook))
    }

    return true;
}

const LinkedNotebook * FakeNoteStore::findLinkedNotebook(const QString & guid) const
{
    const LinkedNotebookDataByGuid & index = m_linkedNotebooks.get<LinkedNotebookByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return Q_NULLPTR;
    }

    const LinkedNotebook & linkedNotebook = *it;
    return &linkedNotebook;
}

bool FakeNoteStore::removeLinkedNotebook(const QString & guid)
{
    LinkedNotebookDataByGuid & index = m_linkedNotebooks.get<LinkedNotebookByGuid>();
    auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedLinkedNotebookGuid(const QString & guid)
{
    Q_UNUSED(removeLinkedNotebook(guid))
    Q_UNUSED(m_expungedLinkedNotebookGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedLinkedNotebookGuid(const QString & guid) const
{
    return m_expungedLinkedNotebookGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedLinkedNotebookGuid(const QString & guid)
{
    auto it = m_expungedLinkedNotebookGuids.find(guid);
    if (it == m_expungedLinkedNotebookGuids.end()) {
        return false;
    }

    Q_UNUSED(m_expungedLinkedNotebookGuids.erase(it))
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

void FakeNoteStore::setSyncState(const qevercloud::SyncState & syncState)
{
    m_syncState = syncState;
}

const qevercloud::SyncState * FakeNoteStore::findLinkedNotebookSyncState(const QString & linkedNotebookOwner) const
{
    auto it = m_linkedNotebookSyncStates.find(linkedNotebookOwner);
    if (it != m_linkedNotebookSyncStates.end()) {
        return &(it.value());
    }

    return Q_NULLPTR;
}

void FakeNoteStore::setLinkedNotebookSyncState(const QString & linkedNotebookOwner, const qevercloud::SyncState & syncState)
{
    QNDEBUG(QStringLiteral("FakeNoteStore::setLinkedNotebookSyncState: linked notebook owner: ")
            << linkedNotebookOwner << QStringLiteral(", sync state: ") << syncState);
    m_linkedNotebookSyncStates[linkedNotebookOwner] = syncState;
}

bool FakeNoteStore::removeLinkedNotebookSyncState(const QString & linkedNotebookOwner)
{
    auto it = m_linkedNotebookSyncStates.find(linkedNotebookOwner);
    if (it != m_linkedNotebookSyncStates.end()) {
        Q_UNUSED(m_linkedNotebookSyncStates.erase(it))
        return true;
    }

    return false;
}

const QString & FakeNoteStore::authToken() const
{
    return m_authenticationToken;
}

void FakeNoteStore::setAuthToken(const QString & authToken)
{
    m_authenticationToken = authToken;
}

QString FakeNoteStore::linkedNotebookAuthToken(const QString & linkedNotebookOwner) const
{
    auto it = m_linkedNotebookAuthTokens.find(linkedNotebookOwner);
    if (it != m_linkedNotebookAuthTokens.end()) {
        return it.value();
    }

    return QString();
}

void FakeNoteStore::setLinkedNotebookAuthToken(const QString & linkedNotebookOwner, const QString & linkedNotebookAuthToken)
{
    QNDEBUG(QStringLiteral("FakeNoteStore::setLinkedNotebookAuthToken: owner = ") << linkedNotebookOwner
            << QStringLiteral(", token = ") << linkedNotebookAuthToken);
    m_linkedNotebookAuthTokens[linkedNotebookOwner] = linkedNotebookAuthToken;
}

bool FakeNoteStore::removeLinkedNotebookAuthToken(const QString & linkedNotebookOwner)
{
    auto it = m_linkedNotebookAuthTokens.find(linkedNotebookOwner);
    if (it != m_linkedNotebookAuthTokens.end()) {
        Q_UNUSED(m_linkedNotebookAuthTokens.erase(it))
        return true;
    }

    return false;
}

qint32 FakeNoteStore::currentMaxUsn(const QString & linkedNotebookGuid) const
{
    QNDEBUG(QStringLiteral("FakeNoteStore::currentMaxUsn: linked notebook guid = ") << linkedNotebookGuid);

    qint32 maxUsn = 0;

    const SavedSearchDataByUSN & savedSearchUsnIndex = m_savedSearches.get<SavedSearchByUSN>();
    if (linkedNotebookGuid.isEmpty() && !savedSearchUsnIndex.empty())
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
        auto preBeginTagIt = tagUsnIndex.begin();
        --preBeginTagIt;
        auto tagIt = lastTagIt;
        while(tagIt != preBeginTagIt)
        {
            QNTRACE(QStringLiteral("Examing tag: ") << *tagIt);

            bool matchesByLinkedNotebook = ( ( !linkedNotebookGuid.isEmpty() &&
                                               tagIt->hasLinkedNotebookGuid() &&
                                               (tagIt->linkedNotebookGuid() == linkedNotebookGuid) )
                                             ||
                                             (linkedNotebookGuid.isEmpty() &&
                                              !tagIt->hasLinkedNotebookGuid()) );
            if (!matchesByLinkedNotebook) {
                QNTRACE(QStringLiteral("Skipping tag ") << *tagIt << QStringLiteral("\nAs it doesn't match by linked notebook"));
                --tagIt;
                continue;
            }

            if (tagIt->updateSequenceNumber() > maxUsn) {
                maxUsn = tagIt->updateSequenceNumber();
                QNTRACE(QStringLiteral("Updated max USN to ") << maxUsn << QStringLiteral(" from tag: ") << *tagIt);
            }
            else {
                QNTRACE(QStringLiteral("Skipping tag ") << *tagIt << QStringLiteral("\nAs its USN is no greater than max USN ") << maxUsn);
            }

            --tagIt;
            // break;
        }
    }

    const NotebookDataByUSN & notebookUsnIndex = m_notebooks.get<NotebookByUSN>();
    if (!notebookUsnIndex.empty())
    {
        auto lastNotebookIt = notebookUsnIndex.end();
        --lastNotebookIt;
        auto preBeginNotebookIt = notebookUsnIndex.begin();
        --preBeginNotebookIt;
        auto notebookIt = lastNotebookIt;
        while(notebookIt != preBeginNotebookIt)
        {
            bool matchesByLinkedNotebook = ( ( !linkedNotebookGuid.isEmpty() &&
                                               notebookIt->hasLinkedNotebookGuid() &&
                                               (notebookIt->linkedNotebookGuid() == linkedNotebookGuid) )
                                             ||
                                             (linkedNotebookGuid.isEmpty() &&
                                              !notebookIt->hasLinkedNotebookGuid()) );
            if (!matchesByLinkedNotebook) {
                QNTRACE(QStringLiteral("Skipping notebook ") << *notebookIt << QStringLiteral("\nAs it doesn't match by linked notebook"));
                --notebookIt;
                continue;
            }

            if (notebookIt->updateSequenceNumber() > maxUsn) {
                maxUsn = notebookIt->updateSequenceNumber();
                QNTRACE(QStringLiteral("Updated max USN to ") << maxUsn);
            }
            else {
                QNTRACE(QStringLiteral("Skipping notebook ") << *notebookIt << QStringLiteral("\nAs its USN is no greater than max USN ") << maxUsn);
            }

            --notebookIt;
            // break;
        }
    }

    const NoteDataByUSN & noteUsnIndex = m_notes.get<NoteByUSN>();
    if (!noteUsnIndex.empty())
    {
        const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();

        auto lastNoteIt = noteUsnIndex.end();
        --lastNoteIt;
        auto preBeginNoteIt = noteUsnIndex.begin();
        --preBeginNoteIt;
        auto noteIt = lastNoteIt;
        while(noteIt != preBeginNoteIt)
        {
            bool matchesByLinkedNotebook = false;

            auto notebookIt = notebookGuidIndex.find(noteIt->notebookGuid());
            if (notebookIt != notebookGuidIndex.end())
            {
                matchesByLinkedNotebook = ( ( !linkedNotebookGuid.isEmpty() &&
                                              notebookIt->hasLinkedNotebookGuid() &&
                                              (notebookIt->linkedNotebookGuid() == linkedNotebookGuid) )
                                            ||
                                            (linkedNotebookGuid.isEmpty() &&
                                             !notebookIt->hasLinkedNotebookGuid()) );
            }

            if (!matchesByLinkedNotebook) {
                QNTRACE(QStringLiteral("Skipping note ") << *noteIt << QStringLiteral("\nAs it doesn't match by linked notebook"));
                --noteIt;
                continue;
            }

            if (noteIt->updateSequenceNumber() > maxUsn) {
                maxUsn = noteIt->updateSequenceNumber();
                QNTRACE(QStringLiteral("Updated max USN to ") << maxUsn);
            }
            else {
                QNTRACE(QStringLiteral("Skipping note ") << *noteIt << QStringLiteral("\nAs its USN is no greater than max USN ") << maxUsn);
            }

            --noteIt;
            // break;
        }
    }

    const ResourceDataByUSN & resourceUsnIndex = m_resources.get<ResourceByUSN>();
    if (!resourceUsnIndex.empty())
    {
        const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();
        const NoteDataByGuid & noteGuidIndex = m_notes.get<NoteByGuid>();

        auto lastResourceIt = resourceUsnIndex.end();
        --lastResourceIt;
        auto preBeginResourceIt = resourceUsnIndex.begin();
        --preBeginResourceIt;
        auto resourceIt = lastResourceIt;

        while(resourceIt != preBeginResourceIt)
        {
            bool matchesByLinkedNotebook = false;
            auto noteIt = noteGuidIndex.find(resourceIt->noteGuid());
            if (noteIt != noteGuidIndex.end())
            {
                auto notebookIt = notebookGuidIndex.find(noteIt->notebookGuid());
                if (notebookIt != notebookGuidIndex.end())
                {
                    matchesByLinkedNotebook = ( ( !linkedNotebookGuid.isEmpty() &&
                                                  notebookIt->hasLinkedNotebookGuid() &&
                                                  (notebookIt->linkedNotebookGuid() == linkedNotebookGuid) )
                                                ||
                                                (linkedNotebookGuid.isEmpty() &&
                                                 !notebookIt->hasLinkedNotebookGuid()) );
                }
            }

            if (!matchesByLinkedNotebook) {
                QNTRACE(QStringLiteral("Skipping resource ") << *resourceIt << QStringLiteral("\nAs it doesn't match by linked notebook"));
                --resourceIt;
                continue;
            }

            if (resourceIt->updateSequenceNumber() > maxUsn) {
                maxUsn = resourceIt->updateSequenceNumber();
                QNTRACE(QStringLiteral("Updated max USN to ") << maxUsn);
            }
            else {
                QNTRACE(QStringLiteral("Skipping resource ") << *resourceIt << QStringLiteral("\nAs its USN is no greater than max USN ") << maxUsn);
            }

            --resourceIt;
            // break;
        }
    }

    const LinkedNotebookDataByUSN & linkedNotebookUsnIndex = m_linkedNotebooks.get<LinkedNotebookByUSN>();
    if (linkedNotebookGuid.isEmpty() && !linkedNotebookUsnIndex.empty())
    {
        auto lastLinkedNotebookIt = linkedNotebookUsnIndex.end();
        --lastLinkedNotebookIt;
        if (lastLinkedNotebookIt->updateSequenceNumber() > maxUsn) {
            maxUsn = lastLinkedNotebookIt->updateSequenceNumber();
            QNTRACE(QStringLiteral("Updated max USN from linked notebook to ") << maxUsn);
        }
    }

    QNDEBUG(QStringLiteral("Overall max USN = ") << maxUsn);
    return maxUsn;
}

INoteStore * FakeNoteStore::create() const
{
    FakeNoteStore * pNoteStore = new FakeNoteStore;
    pNoteStore->m_savedSearches = m_savedSearches;
    pNoteStore->m_expungedSavedSearchGuids = m_expungedSavedSearchGuids;
    pNoteStore->m_tags = m_tags;
    pNoteStore->m_expungedTagGuids = m_expungedTagGuids;
    pNoteStore->m_notebooks = m_notebooks;
    pNoteStore->m_expungedNoteGuids = m_expungedNotebookGuids;
    pNoteStore->m_notes = m_notes;
    pNoteStore->m_expungedNoteGuids = m_expungedNoteGuids;
    pNoteStore->m_resources = m_resources;
    pNoteStore->m_linkedNotebooks = m_linkedNotebooks;
    pNoteStore->m_expungedLinkedNotebookGuids = m_expungedLinkedNotebookGuids;
    pNoteStore->m_linkedNotebookSyncStates = m_linkedNotebookSyncStates;
    pNoteStore->m_linkedNotebookAuthTokens = m_linkedNotebookAuthTokens;
    pNoteStore->m_authenticationToken = m_authenticationToken;
    return pNoteStore;
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

    if (notebook.hasLinkedNotebookGuid())
    {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(notebook.guid(), linkedNotebookAuthToken, errorDescription);
        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (!linkedNotebookAuthToken.isEmpty() && (linkedNotebookAuthToken != m_authenticationToken))
    {
        errorDescription.setBase(QStringLiteral("Notebook doesn't belong to a linked notebook but linked notebook auth token is not empty"));
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
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

    qint32 maxUsn = currentMaxUsn(notebook.hasLinkedNotebookGuid() ? notebook.linkedNotebookGuid() : QString());
    ++maxUsn;
    notebook.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(m_notebooks.insert(notebook))
    return 0;
}

qint32 FakeNoteStore::updateNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                     const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

    if (!notebook.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Notebook guid is not set"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    qint32 checkRes = checkNotebookFields(notebook, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (notebook.hasLinkedNotebookGuid())
    {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(notebook.guid(), linkedNotebookAuthToken, errorDescription);
        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (!linkedNotebookAuthToken.isEmpty() && (linkedNotebookAuthToken != m_authenticationToken))
    {
        errorDescription.setBase(QStringLiteral("Notebook doesn't belong to a linked notebook but linked notebook auth token is not empty"));
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
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

    const Notebook & originalNotebook = *it;
    if (!originalNotebook.canUpdateNotebook()) {
        errorDescription.setBase(QStringLiteral("No permission to update the notebook"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    if (originalNotebook.name().toUpper() != notebook.name().toUpper())
    {
        NotebookDataByNameUpper & nameIndex = m_notebooks.get<NotebookByNameUpper>();
        auto nameIt = nameIndex.find(notebook.name().toUpper());
        if (nameIt != nameIndex.end()) {
            errorDescription.setBase(QStringLiteral("Notebook with the specified name already exists"));
            return qevercloud::EDAMErrorCode::DATA_CONFLICT;
        }
    }

    qint32 maxUsn = currentMaxUsn(notebook.hasLinkedNotebookGuid() ? notebook.linkedNotebookGuid() : QString());
    ++maxUsn;
    notebook.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(index.replace(it, notebook))
    return 0;
}

qint32 FakeNoteStore::createNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                 const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

    if (m_notes.size() + 1 > m_maxNumNotes) {
        errorDescription.setBase(QStringLiteral("Already at max number of notes"));
        return qevercloud::EDAMErrorCode::LIMIT_REACHED;
    }

    qint32 checkRes = checkNoteFields(note, CheckNoteFieldsPurpose::CreateNote, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    const Notebook * pNotebook = findNotebook(note.notebookGuid());
    if (Q_UNLIKELY(!pNotebook)) {
        errorDescription.setBase(QStringLiteral("No notebook was found for note"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    if (pNotebook->hasLinkedNotebookGuid())
    {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(note.notebookGuid(), linkedNotebookAuthToken, errorDescription);
        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (!linkedNotebookAuthToken.isEmpty() && (linkedNotebookAuthToken != m_authenticationToken))
    {
        errorDescription.setBase(QStringLiteral("Note's notebook doesn't belong to a linked notebook but linked notebook auth token is not empty"));
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
    }

    note.setGuid(UidGenerator::Generate());

    qint32 maxUsn = currentMaxUsn(pNotebook->hasLinkedNotebookGuid() ? pNotebook->linkedNotebookGuid() : QString());
    ++maxUsn;
    note.setUpdateSequenceNumber(maxUsn);

    auto insertResult = m_notes.insert(note);

    if (note.hasResources())
    {
        QList<Resource> resources = note.resources();
        for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
        {
            Resource & resource = *it;

            if (!resource.hasGuid()) {
                resource.setGuid(UidGenerator::Generate());
            }

            if (!resource.hasNoteGuid()) {
                resource.setNoteGuid(note.guid());
            }

            // NOTE: not really sure it should behave this way but let's try: setting each resource's USN to note's USN
            resource.setUpdateSequenceNumber(note.updateSequenceNumber());

            if (!setResource(resource, errorDescription)) {
                return qevercloud::EDAMErrorCode::DATA_CONFLICT;
            }
        }

        QList<Resource> originalResources = resources;

        for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
        {
            Resource & resource = *it;

            // Won't store resource binary data along with notes
            resource.setDataBody(QByteArray());
            resource.setRecognitionDataBody(QByteArray());
            resource.setAlternateDataBody(QByteArray());
        }

        note.setResources(resources);
        Q_UNUSED(m_notes.replace(insertResult.first, note))

        // Restore the original resources with guids and USNs back to the input-output note
        note.setResources(originalResources);

        QNTRACE(QStringLiteral("Note after FakeNoteStore::createNote: ") << note);
    }

    return 0;
}

qint32 FakeNoteStore::updateNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                 const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

    if (!note.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Note.guid"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    qint32 checkRes = checkNoteFields(note, CheckNoteFieldsPurpose::UpdateNote, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    NoteDataByGuid & index = m_notes.get<NoteByGuid>();
    auto it = index.find(note.guid());
    if (it == index.end()) {
        errorDescription.setBase(QStringLiteral("Note with the specified guid doesn't exist"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    const Notebook * pNotebook = findNotebook(note.notebookGuid());
    if (Q_UNLIKELY(!pNotebook)) {
        errorDescription.setBase(QStringLiteral("No notebook was found for note"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    if (pNotebook->hasLinkedNotebookGuid())
    {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(note.notebookGuid(), linkedNotebookAuthToken, errorDescription);
        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (!linkedNotebookAuthToken.isEmpty() && (linkedNotebookAuthToken != m_authenticationToken))
    {
        errorDescription.setBase(QStringLiteral("Note's notebook doesn't belong to a linked notebook but linked notebook auth token is not empty"));
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
    }

    qint32 maxUsn = currentMaxUsn(pNotebook->hasLinkedNotebookGuid() ? pNotebook->linkedNotebookGuid() : QString());
    ++maxUsn;
    note.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(index.replace(it, note))

    if (note.hasResources())
    {
        QList<Resource> resources = note.resources();
        for(auto resIt = resources.begin(), resEnd = resources.end(); resIt != resEnd; ++resIt)
        {
            Resource & resource = *resIt;

            if (!resource.hasGuid()) {
                resource.setGuid(UidGenerator::Generate());
            }

            if (!resource.hasNoteGuid()) {
                resource.setNoteGuid(note.guid());
            }

            // NOTE: not really sure it should behave this way but let's try: setting each resource's USN to note's USN
            resource.setUpdateSequenceNumber(note.updateSequenceNumber());

            if (!setResource(resource, errorDescription)) {
                return qevercloud::EDAMErrorCode::DATA_CONFLICT;
            }
        }

        QList<Resource> originalResources = resources;

        for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
        {
            Resource & resource = *it;

            // Won't store resource binary data along with notes
            resource.setDataBody(QByteArray());
            resource.setRecognitionDataBody(QByteArray());
            resource.setAlternateDataBody(QByteArray());
        }

        note.setResources(resources);
        Q_UNUSED(index.replace(it, note))

        // Restore the original resources with guids and USNs back to the input-output note
        note.setResources(originalResources);

        QNTRACE(QStringLiteral("Note after FakeNoteStore::updateNote: ") << note);
    }

    return 0;
}

qint32 FakeNoteStore::createTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

    if (m_tags.size() + 1 > m_maxNumTags) {
        errorDescription.setBase(QStringLiteral("Already at max number of tags"));
        return qevercloud::EDAMErrorCode::LIMIT_REACHED;
    }

    qint32 checkRes = checkTagFields(tag, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (tag.hasLinkedNotebookGuid())
    {
        checkRes = checkLinkedNotebookAuthTokenForTag(tag, linkedNotebookAuthToken, errorDescription);
        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (!linkedNotebookAuthToken.isEmpty() && (linkedNotebookAuthToken != m_authenticationToken))
    {
        errorDescription.setBase(QStringLiteral("Tag doesn't belong to a linked notebook but linked notebook auth token is not empty"));
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
    }

    TagDataByNameUpper & nameIndex = m_tags.get<TagByNameUpper>();
    auto it = nameIndex.find(tag.name().toUpper());
    if (it != nameIndex.end()) {
        errorDescription.setBase(QStringLiteral("Tag name is already in use"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    tag.setGuid(UidGenerator::Generate());

    qint32 maxUsn = currentMaxUsn(tag.hasLinkedNotebookGuid() ? tag.linkedNotebookGuid() : QString());
    ++maxUsn;
    tag.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(m_tags.insert(tag))

    return 0;
}

qint32 FakeNoteStore::updateTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
                                const QString & linkedNotebookAuthToken)
{
    CHECK_API_RATE_LIMIT()

    if (!tag.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Tag guid is not set"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    qint32 checkRes = checkTagFields(tag, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (tag.hasLinkedNotebookGuid())
    {
        checkRes = checkLinkedNotebookAuthTokenForTag(tag, linkedNotebookAuthToken, errorDescription);
        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (!linkedNotebookAuthToken.isEmpty() && (linkedNotebookAuthToken != m_authenticationToken))
    {
        errorDescription.setBase(QStringLiteral("Tag doesn't belong to a linked notebook but linked notebook auth token is not empty"));
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
    }

    TagDataByGuid & index = m_tags.get<TagByGuid>();
    auto it = index.find(tag.guid());
    if (it == index.end()) {
        errorDescription.setBase(QStringLiteral("Tag with the specified guid doesn't exist"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    const Tag & originalTag = *it;
    if (originalTag.name().toUpper() != tag.name().toUpper())
    {
        TagDataByNameUpper & nameIndex = m_tags.get<TagByNameUpper>();
        auto nameIt = nameIndex.find(tag.name().toUpper());
        if (nameIt != nameIndex.end()) {
            errorDescription.setBase(QStringLiteral("Tag with the specified name already exists"));
            return qevercloud::EDAMErrorCode::DATA_CONFLICT;
        }
    }

    qint32 maxUsn = currentMaxUsn(tag.hasLinkedNotebookGuid() ? tag.linkedNotebookGuid() : QString());
    ++maxUsn;
    tag.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(index.replace(it, tag))
    return 0;
}

qint32 FakeNoteStore::createSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    if (m_savedSearches.size() + 1 > m_maxNumSavedSearches) {
        errorDescription.setBase(QStringLiteral("Already at max number of saved searches"));
        return qevercloud::EDAMErrorCode::LIMIT_REACHED;
    }

    qint32 checkRes = checkSavedSearchFields(savedSearch, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    SavedSearchDataByNameUpper & nameIndex = m_savedSearches.get<SavedSearchByNameUpper>();
    auto it = nameIndex.find(savedSearch.name().toUpper());
    if (it != nameIndex.end()) {
        errorDescription.setBase(QStringLiteral("Saved search name is already in use"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    savedSearch.setGuid(UidGenerator::Generate());

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    savedSearch.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(m_savedSearches.insert(savedSearch))

    return 0;
}

qint32 FakeNoteStore::updateSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    if (!savedSearch.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Saved search guid is not set"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    qint32 checkRes = checkSavedSearchFields(savedSearch, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    SavedSearchDataByGuid & index = m_savedSearches.get<SavedSearchByGuid>();
    auto it = index.find(savedSearch.guid());
    if (it == index.end()) {
        errorDescription.setBase(QStringLiteral("Saved search with the specified guid doesn't exist"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    const SavedSearch & originalSavedSearch = *it;
    if (originalSavedSearch.name().toUpper() != savedSearch.name().toUpper())
    {
        const SavedSearchDataByNameUpper & nameIndex = m_savedSearches.get<SavedSearchByNameUpper>();
        auto nameIt = nameIndex.find(savedSearch.name().toUpper());
        if (nameIt != nameIndex.end()) {
            errorDescription.setBase(QStringLiteral("Saved search with the specified name already exists"));
            return qevercloud::EDAMErrorCode::DATA_CONFLICT;
        }
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    savedSearch.setUpdateSequenceNumber(maxUsn);

    Q_UNUSED(index.replace(it, savedSearch))
    return 0;
}

qint32 FakeNoteStore::getSyncState(qevercloud::SyncState & syncState, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()
    syncState = m_syncState;
    Q_UNUSED(errorDescription)
    return 0;
}

qint32 FakeNoteStore::getSyncChunk(const qint32 afterUSN, const qint32 maxEntries, const qevercloud::SyncChunkFilter & filter,
                                   qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                   qint32 & rateLimitSeconds)
{
    return getSyncChunkImpl(afterUSN, maxEntries, (afterUSN == 0), QString(), filter,
                            syncChunk, errorDescription, rateLimitSeconds);
}

qint32 FakeNoteStore::getLinkedNotebookSyncState(const qevercloud::LinkedNotebook & linkedNotebook,
                                                 const QString & authToken, qevercloud::SyncState & syncState,
                                                 ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    if (m_pQecNoteStore->authenticationToken() != authToken) {
        errorDescription.setBase(QStringLiteral("Wrong authentication token"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    qint32 checkRes = checkLinkedNotebookFields(linkedNotebook, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    auto it = m_linkedNotebookSyncStates.find(linkedNotebook.username.ref());
    if (it == m_linkedNotebookSyncStates.end()) {
        QNWARNING(QStringLiteral("Failed to find linked notebook sync state for linked notebook: ") << linkedNotebook
                  << QStringLiteral("\nLinked notebook sync states: ") << m_linkedNotebookSyncStates);
        errorDescription.setBase(QStringLiteral("Found no sync state for the given linked notebook owner"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    syncState = *it;
    return 0;
}

qint32 FakeNoteStore::getLinkedNotebookSyncChunk(const qevercloud::LinkedNotebook & linkedNotebook,
                                                 const qint32 afterUSN, const qint32 maxEntries,
                                                 const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
                                                 qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                                 qint32 & rateLimitSeconds)
{
    QNDEBUG(QStringLiteral("FakeNoteStore::getLinkedNotebookSyncChunk: linked notebook = ") << linkedNotebook
            << QStringLiteral("\nAfter USN = ") << afterUSN << QStringLiteral(", max entries = ") << maxEntries
            << QStringLiteral(", full sync only = ") << (fullSyncOnly ? QStringLiteral("true") : QStringLiteral("false")));

    CHECK_API_RATE_LIMIT()

    qint32 checkRes = checkLinkedNotebookFields(linkedNotebook, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    const LinkedNotebookDataByUsername & linkedNotebookUsernameIndex = m_linkedNotebooks.get<LinkedNotebookByUsername>();
    auto linkedNotebookIt = linkedNotebookUsernameIndex.find(linkedNotebook.username.ref());
    if (linkedNotebookIt == linkedNotebookUsernameIndex.end()) {
        errorDescription.setBase(QStringLiteral("Found no existing linked notebook by username"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    if (linkedNotebookAuthToken != m_authenticationToken) {
        errorDescription.setBase(QStringLiteral("Wrong authentication token"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    qevercloud::SyncChunkFilter filter;
    filter.includeTags = true;
    filter.includeNotebooks = true;
    filter.includeNotes = true;
    filter.includeNoteResources = true;
    filter.includeNoteAttributes = true;
    filter.includeNoteApplicationDataFullMap = true;
    filter.includeNoteResourceApplicationDataFullMap = true;

    if (!fullSyncOnly && (afterUSN != 0)) {
        filter.includeResources = true;
        filter.includeResourceApplicationDataFullMap = true;
    }

    return getSyncChunkImpl(afterUSN, maxEntries, fullSyncOnly, linkedNotebook.guid.ref(), filter,
                            syncChunk, errorDescription, rateLimitSeconds);
}

qint32 FakeNoteStore::getNote(const bool withContent, const bool withResourcesData,
                              const bool withResourcesRecognition, const bool withResourcesAlternateData,
                              Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    if (!note.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Note has no guid"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    const NoteDataByGuid & noteGuidIndex = m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(note.guid());
    if (noteIt == noteGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("Note was not found"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    note = *noteIt;

    if (!withContent) {
        note.setContent(QString());
    }

    if (note.hasResources())
    {
        const ResourceDataByGuid & resourceGuidIndex = m_resources.get<ResourceByGuid>();
        QList<Resource> resources = note.resources();
        for(auto it = resources.begin(); it != resources.end(); )
        {
            Resource & resource = *it;
            if (Q_UNLIKELY(!resource.hasGuid())) {
                it = resources.erase(it);
                continue;
            }

            auto resourceIt = resourceGuidIndex.find(resource.guid());
            if (Q_UNLIKELY(resourceIt == resourceGuidIndex.end())) {
                it = resources.erase(it);
                continue;
            }

            resource = *resourceIt;

            if (!withResourcesData) {
                resource.setDataBody(QByteArray());
            }

            if (!withResourcesRecognition) {
                resource.setRecognitionDataBody(QByteArray());
            }

            if (!withResourcesAlternateData) {
                resource.setAlternateDataBody(QByteArray());
            }

            ++it;
        }

        note.setResources(resources);
    }

    return 0;
}

bool FakeNoteStore::getNoteAsync(const bool withContent, const bool withResourcesData, const bool withResourcesRecognition,
                                 const bool withResourcesAlternateData, const bool withSharedNotes,
                                 const bool withNoteAppDataValues, const bool withResourceAppDataValues,
                                 const bool withNoteLimits, const QString & noteGuid,
                                 const QString & authToken, ErrorString & errorDescription)
{
    if (Q_UNLIKELY(noteGuid.isEmpty())) {
        errorDescription.setBase(QStringLiteral("Note guid is empty"));
        return false;
    }

    GetNoteAsyncRequest request;
    request.m_withContent = withContent;
    request.m_withResourcesData = withResourcesData;
    request.m_withResourcesRecognition = withResourcesRecognition;
    request.m_withResourcesAlternateData = withResourcesAlternateData;
    request.m_withSharedNotes = withSharedNotes;
    request.m_withNoteAppDataValues = withNoteAppDataValues;
    request.m_withResourceAppDataValues = withResourceAppDataValues;
    request.m_withNoteLimits = withNoteLimits;
    request.m_noteGuid = noteGuid;
    request.m_authToken = authToken;

    m_getNoteAsyncRequests.enqueue(request);

    int timerId = startTimer(0);
    QNDEBUG(QStringLiteral("Started timer to postpone the get note result, timer id = ") << timerId);

    Q_UNUSED(m_getNoteAsyncDelayTimerIds.insert(timerId))
    return true;
}

qint32 FakeNoteStore::getResource(const bool withDataBody, const bool withRecognitionDataBody,
                                  const bool withAlternateDataBody, const bool withAttributes,
                                  const QString & authToken, Resource & resource, ErrorString & errorDescription,
                                  qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    if (!resource.hasGuid()) {
        errorDescription.setBase(QStringLiteral("Resource has no guid"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    const ResourceDataByGuid & resourceGuidIndex = m_resources.get<ResourceByGuid>();
    auto resourceIt = resourceGuidIndex.find(resource.guid());
    if (resourceIt == resourceGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("Resource was not found"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    if (Q_UNLIKELY(!resourceIt->hasNoteGuid())) {
        errorDescription.setBase(QStringLiteral("Found resource has no note guid"));
        return qevercloud::EDAMErrorCode::INTERNAL_ERROR;
    }

    const QString & noteGuid = resourceIt->noteGuid();
    const NoteDataByGuid & noteGuidIndex = m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(noteGuid);
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        errorDescription.setBase(QStringLiteral("Found no note containing the resource"));
        return qevercloud::EDAMErrorCode::INTERNAL_ERROR;
    }

    if (Q_UNLIKELY(!noteIt->hasNotebookGuid())) {
        errorDescription.setBase(QStringLiteral("Found note has no notebook guid"));
        return qevercloud::EDAMErrorCode::INTERNAL_ERROR;
    }

    const QString & notebookGuid = noteIt->notebookGuid();
    const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(notebookGuid);
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        errorDescription.setBase(QStringLiteral("Found no notebook containing the note with the resource"));
        return qevercloud::EDAMErrorCode::INTERNAL_ERROR;
    }

    if (notebookIt->hasLinkedNotebookGuid())
    {
        qint32 checkRes = checkLinkedNotebookAuthTokenForNotebook(notebookGuid, authToken, errorDescription);
        if (checkRes != 0) {
            return checkRes;
        }
    }

    resource = *resourceIt;

    if (!withDataBody) {
        resource.setDataBody(QByteArray());
    }

    if (!withRecognitionDataBody) {
        resource.setRecognitionDataBody(QByteArray());
    }

    if (!withAlternateDataBody) {
        resource.setAlternateDataBody(QByteArray());
    }

    if (!withAttributes) {
        resource.setResourceAttributes(qevercloud::ResourceAttributes());
    }

    return 0;
}

bool FakeNoteStore::getResourceAsync(const bool withDataBody, const bool withRecognitionDataBody,
                                     const bool withAlternateDataBody, const bool withAttributes, const QString & resourceGuid,
                                     const QString & authToken, ErrorString & errorDescription)
{
    if (Q_UNLIKELY(resourceGuid.isEmpty())) {
        errorDescription.setBase(QStringLiteral("Resource guid is empty"));
        return false;
    }

    GetResourceAsyncRequest request;
    request.m_withDataBody = withDataBody;
    request.m_withRecognitionDataBody = withRecognitionDataBody;
    request.m_withAlternateDataBody = withAlternateDataBody;
    request.m_withAttributes = withAttributes;
    request.m_resourceGuid = resourceGuid;

    request.m_authToken = authToken;

    m_getResourceAsyncRequests.enqueue(request);
    int timerId = startTimer(0);
    QNDEBUG(QStringLiteral("Started timer to postpone the get resource result, timer id = ") << timerId);

    Q_UNUSED(m_getResourceAsyncDelayTimerIds.insert(timerId))
    return true;
}

qint32 FakeNoteStore::authenticateToSharedNotebook(const QString & shareKey, qevercloud::AuthenticationResult & authResult,
                                                   ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    const LinkedNotebookDataBySharedNotebookGlobalId & index = m_linkedNotebooks.get<LinkedNotebookBySharedNotebookGlobalId>();
    auto it = index.find(shareKey);
    if (it == index.end()) {
        errorDescription.setBase("Found no linked notebook corresponding to share key");
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
    }

    const LinkedNotebook & linkedNotebook = *it;
    auto authTokenIt = m_linkedNotebookAuthTokens.find(linkedNotebook.username());
    if (authTokenIt == m_linkedNotebookAuthTokens.end()) {
        errorDescription.setBase(QStringLiteral("No valid authentication token was provided"));
        return qevercloud::EDAMErrorCode::INVALID_AUTH;
    }

    authResult.authenticationToken = authTokenIt.value();
    authResult.currentTime = QDateTime::currentMSecsSinceEpoch();
    authResult.expiration = QDateTime::currentDateTime().addYears(1).toMSecsSinceEpoch();
    authResult.noteStoreUrl = QStringLiteral("Fake note store URL");
    authResult.webApiUrlPrefix = QStringLiteral("Fake web API url prefix");
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
        QNDEBUG(QStringLiteral("getNoteAsync delay timer event, timer id = ") << pEvent->timerId());

        Q_UNUSED(m_getNoteAsyncDelayTimerIds.erase(noteIt))
        killTimer(pEvent->timerId());

        if (!m_getNoteAsyncRequests.isEmpty())
        {
            GetNoteAsyncRequest request = m_getNoteAsyncRequests.dequeue();

            qint32 rateLimitSeconds = 0;
            ErrorString errorDescription;
            Note note;
            note.setGuid(request.m_noteGuid);
            qint32 res = getNote(request.m_withContent, request.m_withResourcesData,
                                 request.m_withResourcesRecognition, request.m_withResourcesAlternateData,
                                 note, errorDescription, rateLimitSeconds);
            if ((res == 0) && (note.hasNotebookGuid()))
            {
                const Notebook * pNotebook = findNotebook(note.notebookGuid());
                if (Q_UNLIKELY(!pNotebook)) {
                    errorDescription.setBase(QStringLiteral("No notebook was found for note"));
                    res = qevercloud::EDAMErrorCode::DATA_CONFLICT;
                }
                else if (pNotebook->hasLinkedNotebookGuid())
                {
                    res = checkLinkedNotebookAuthTokenForNotebook(note.notebookGuid(), request.m_authToken, errorDescription);
                }
                else if (!request.m_authToken.isEmpty() && (request.m_authToken != m_authenticationToken))
                {
                    errorDescription.setBase(QStringLiteral("Note's notebook doesn't belong to a linked notebook but linked notebook auth token is not empty"));
                    res = qevercloud::EDAMErrorCode::INVALID_AUTH;
                }
            }

            Q_EMIT getNoteAsyncFinished(res, note.qevercloudNote(), rateLimitSeconds, errorDescription);
        }
        else
        {
            QNWARNING("Get note async requests queue is empty");
        }

        return;
    }

    auto resourceIt = m_getResourceAsyncDelayTimerIds.find(pEvent->timerId());
    if (resourceIt != m_getResourceAsyncDelayTimerIds.end())
    {
        QNDEBUG(QStringLiteral("getResourceAsync delay timer event, timer id = ") << pEvent->timerId());

        Q_UNUSED(m_getResourceAsyncDelayTimerIds.erase(resourceIt))
        killTimer(pEvent->timerId());

        if (!m_getResourceAsyncRequests.isEmpty())
        {
            GetResourceAsyncRequest request = m_getResourceAsyncRequests.dequeue();

            qint32 rateLimitSeconds = 0;
            ErrorString errorDescription;
            Resource resource;
            resource.setGuid(request.m_resourceGuid);
            qint32 res = getResource(request.m_withDataBody, request.m_withRecognitionDataBody,
                                     request.m_withAlternateDataBody, request.m_withAttributes,
                                     request.m_authToken, resource, errorDescription, rateLimitSeconds);
            Q_EMIT getResourceAsyncFinished(res, resource.qevercloudResource(), rateLimitSeconds, errorDescription);
        }
        else
        {
            QNWARNING("Get resource async requests queue is empty");
        }

        return;
    }

    INoteStore::timerEvent(pEvent);
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

    // NOTE: tried to use qevercloud::EDAM_NOTEBOOK_NAME_REGEX to check the name correctness
    // but it appears that regex is too perlish for QRegExp to handle i.e. QRegExp doesn't recognize
    // the regexp in its full entirety and hence doesn't match the text which should actually match

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

        if (notebookStack != notebookStack.trimmed()) {
            errorDescription.setBase(QStringLiteral("Notebook stack should not begin or end with whitespace"));
            return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
        }

        // NOTE: tried to use qevercloud::EDAM_NOTEBOOK_STACK_REGEX to check the name correctness
        // but it appears that regex is too perlish for QRegExp to handle i.e. QRegExp doesn't recognize
        // the regexp in its full entirety and hence doesn't match the text which should actually match
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

qint32 FakeNoteStore::checkNoteFields(const Note & note, const CheckNoteFieldsPurpose::type purpose, ErrorString & errorDescription) const
{
    if (!note.hasNotebookGuid()) {
        errorDescription.setBase(QStringLiteral("Note has no notebook guid set"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    const NotebookDataByGuid & notebookIndex = m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookIndex.find(note.notebookGuid());
    if (notebookIt == notebookIndex.end()) {
        errorDescription.setBase(QStringLiteral("Note.notebookGuid"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    const Notebook & notebook = *notebookIt;
    if (purpose == CheckNoteFieldsPurpose::CreateNote)
    {
        if (!notebook.canCreateNotes()) {
            errorDescription.setBase(QStringLiteral("No permission to create notes within this notebook"));
            return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
        }
    }
    else if (purpose == CheckNoteFieldsPurpose::UpdateNote)
    {
        if (!notebook.canUpdateNotes()) {
            errorDescription.setBase(QStringLiteral("No permission to update notes within this notebook"));
            return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
        }
    }

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

        // NOTE: tried to use qevercloud::EDAM_NOTE_TITLE_REGEX to check the name correctness
        // but it appears that regex is too perlish for QRegExp to handle i.e. QRegExp doesn't recognize
        // the regexp in its full entirety and hence doesn't match the text which should actually match
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

    if (note.hasNoteAttributes())
    {
        const qevercloud::NoteAttributes & attributes = note.noteAttributes();

#define CHECK_STRING_ATTRIBUTE(attr) \
        if (attributes.attr.isSet()) \
        { \
            if (attributes.attr.ref().size() < qevercloud::EDAM_ATTRIBUTE_LEN_MIN) { \
                errorDescription.setBase(QStringLiteral("Attribute length is too small: ") + QString::fromUtf8( #attr )); \
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT; \
            } \
            \
            if (attributes.attr.ref().size() < qevercloud::EDAM_ATTRIBUTE_LEN_MAX) { \
                errorDescription.setBase(QStringLiteral("Attribute length is too large: ") + QString::fromUtf8( #attr )); \
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT; \
            } \
        }

        CHECK_STRING_ATTRIBUTE(author)
        CHECK_STRING_ATTRIBUTE(source)
        CHECK_STRING_ATTRIBUTE(sourceURL)
        CHECK_STRING_ATTRIBUTE(sourceApplication)
        CHECK_STRING_ATTRIBUTE(placeName)
        CHECK_STRING_ATTRIBUTE(contentClass)

        if (attributes.applicationData.isSet())
        {
            qint32 res = checkAppData(attributes.applicationData.ref(), errorDescription);
            if (res != 0) {
                return res;
            }
        }
    }

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

        CHECK_STRING_ATTRIBUTE(sourceURL)
        CHECK_STRING_ATTRIBUTE(cameraMake)
        CHECK_STRING_ATTRIBUTE(cameraModel)

        if (attributes.applicationData.isSet())
        {
            qint32 res = checkAppData(attributes.applicationData.ref(), errorDescription);
            if (res != 0) {
                return res;
            }
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkTagFields(const Tag & tag, ErrorString & errorDescription) const
{
    if (!tag.hasName()) {
        errorDescription.setBase(QStringLiteral("Tag name is not set"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    const QString & tagName = tag.name();
    if (tagName.size() < qevercloud::EDAM_TAG_NAME_LEN_MIN) {
        errorDescription.setBase(QStringLiteral("Tag name length is too small"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (tagName.size() > qevercloud::EDAM_TAG_NAME_LEN_MAX) {
        errorDescription.setBase(QStringLiteral("Tag name length is too large"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    // NOTE: tried to use qevercloud::EDAM_TAG_NAME_REGEX to check the name correctness
    // but it appears that regex is too perlish for QRegExp to handle i.e. QRegExp doesn't recognize
    // the regexp in its full entirety and hence doesn't match the text which should actually match

    if (tagName != tagName.trimmed()) {
        errorDescription.setBase(QStringLiteral("Tag name shouldn't start or end with whitespace"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (tag.hasParentGuid())
    {
        const TagDataByGuid & index = m_tags.get<TagByGuid>();
        auto it = index.find(tag.parentGuid());
        if (it == index.end()) {
            errorDescription.setBase(QStringLiteral("Parent tag doesn't exist"));
            return qevercloud::EDAMErrorCode::UNKNOWN;
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkSavedSearchFields(const SavedSearch & savedSearch, ErrorString & errorDescription) const
{
    if (!savedSearch.hasName()) {
        errorDescription.setBase(QStringLiteral("Saved search name is not set"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (!savedSearch.hasQuery()) {
        errorDescription.setBase(QStringLiteral("Saved search query is not set"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    const QString & savedSearchName = savedSearch.name();
    if (savedSearchName.size() < qevercloud::EDAM_SAVED_SEARCH_NAME_LEN_MIN) {
        errorDescription.setBase(QStringLiteral("Saved search name length is too small"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (savedSearchName.size() > qevercloud::EDAM_SAVED_SEARCH_NAME_LEN_MAX) {
        errorDescription.setBase(QStringLiteral("Saved search name length is too large"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    // NOTE: tried to use qevercloud::EDAM_SAVED_SEARCH_NAME_REGEX to check the name correctness
    // but it appears that regex is too perlish for QRegExp to handle i.e. QRegExp doesn't recognize
    // the regexp in its full entirety and hence doesn't match the text which should actually match

    if (savedSearchName != savedSearchName.trimmed()) {
        errorDescription.setBase(QStringLiteral("Saved search name shouldn't start or end with whitespace"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    const QString & savedSearchQuery = savedSearch.query();
    if (savedSearchQuery.size() < qevercloud::EDAM_SEARCH_QUERY_LEN_MIN) {
        errorDescription.setBase(QStringLiteral("Saved search query length is too small"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (savedSearchQuery.size() > qevercloud::EDAM_SEARCH_QUERY_LEN_MAX) {
        errorDescription.setBase(QStringLiteral("Saved search query length is too large"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    // NOTE: tried to use qevercloud::EDAM_SEARCH_QUERY_REGEX to check the name correctness
    // but it appears that regex is too perlish for QRegExp to handle i.e. QRegExp doesn't recognize
    // the regexp in its full entirety and hence doesn't match the text which should actually match

    return 0;
}

qint32 FakeNoteStore::checkLinkedNotebookFields(const qevercloud::LinkedNotebook & linkedNotebook, ErrorString & errorDescription) const
{
    if (!linkedNotebook.username.isSet()) {
        errorDescription.setBase(QStringLiteral("Linked notebook owner is not set"));
        return qevercloud::EDAMErrorCode::DATA_REQUIRED;
    }

    if (!linkedNotebook.shardId.isSet() && !linkedNotebook.uri.isSet()) {
        errorDescription.setBase(QStringLiteral("Neither linked notebook's shard id nor uri is set"));
        return qevercloud::EDAMErrorCode::DATA_REQUIRED;
    }

    const LinkedNotebookDataByUsername & usernameIndex = m_linkedNotebooks.get<LinkedNotebookByUsername>();
    auto linkedNotebookIt = usernameIndex.find(linkedNotebook.username.ref());
    if (linkedNotebookIt == usernameIndex.end()) {
        errorDescription.setBase(QStringLiteral("Found no linked notebook corresponding to the owner"));
        return qevercloud::EDAMErrorCode::UNKNOWN;
    }

    const LinkedNotebook & existingLinkedNotebook = *linkedNotebookIt;
    if (linkedNotebook.shardId.isSet())
    {
        if (!existingLinkedNotebook.hasShardId()) {
            errorDescription.setBase(QStringLiteral("Linked notebook belonging to this owner has no shard id"));
            return qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE;
        }

        if (existingLinkedNotebook.shardId() != linkedNotebook.shardId.ref()) {
            errorDescription.setBase(QStringLiteral("Linked notebook belonging to this owner has another shard id"));
            return qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE;
        }
    }
    else if (linkedNotebook.uri.isSet())
    {
        if (!existingLinkedNotebook.hasUri()) {
            errorDescription.setBase(QStringLiteral("Linked notebook belonging to this owner has no uri"));
            return qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE;
        }

        if (existingLinkedNotebook.uri() != linkedNotebook.uri.ref()) {
            errorDescription.setBase(QStringLiteral("Linked notebook belonging to this owner has another uri"));
            return qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE;
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkAppData(const qevercloud::LazyMap & appData, ErrorString & errorDescription) const
{
    QRegExp keyRegExp(qevercloud::EDAM_APPLICATIONDATA_NAME_REGEX);
    QRegExp valueRegExp(qevercloud::EDAM_APPLICATIONDATA_VALUE_REGEX);

    if (appData.keysOnly.isSet())
    {
        for(auto it = appData.keysOnly.ref().constBegin(),
            end = appData.keysOnly.ref().constEnd(); it != end; ++it)
        {
            const QString & key = *it;
            qint32 res = checkAppDataKey(key, keyRegExp, errorDescription);
            if (res != 0) {
                return res;
            }
        }
    }

    if (appData.fullMap.isSet())
    {
        for(auto it = appData.fullMap.ref().constBegin(),
            end = appData.fullMap.ref().constEnd(); it != end; ++it)
        {
            const QString & key = it.key();
            qint32 res = checkAppDataKey(key, keyRegExp, errorDescription);
            if (res != 0) {
                return res;
            }

            const QString & value = it.value();

            if (value.size() < qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MIN) {
                errorDescription.setBase(QStringLiteral("Resource app data value length is too small: ") + value);
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
            }

            if (value.size() > qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MAX) {
                errorDescription.setBase(QStringLiteral("Resource app data value length is too large: ") + value);
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
            }

            if (!valueRegExp.exactMatch(value)) {
                errorDescription.setBase(QStringLiteral("Resource app data value doesn't match the mandatory regex"));
                return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
            }
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkAppDataKey(const QString & key, const QRegExp & keyRegExp, ErrorString & errorDescription) const
{
    if (key.size() < qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) {
        errorDescription.setBase(QStringLiteral("Resource app data key length is too small: ") + key);
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (key.size() > qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX) {
        errorDescription.setBase(QStringLiteral("Resource app data key length is too large: ") + key);
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (!keyRegExp.exactMatch(key)) {
        errorDescription.setBase(QStringLiteral("Resource app data key doesn't match the mandatory regex"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    return 0;
}

qint32 FakeNoteStore::checkLinkedNotebookAuthToken(const LinkedNotebook & linkedNotebook, const QString & linkedNotebookAuthToken,
                                                   ErrorString & errorDescription) const
{
    const QString & username = linkedNotebook.username();
    auto authTokenIt = m_linkedNotebookAuthTokens.find(username);
    if (authTokenIt == m_linkedNotebookAuthTokens.end()) {
        errorDescription.setBase(QStringLiteral("Found no auth token for the given linked notebook"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    const QString & expectedLinkedNotebookAuthToken = authTokenIt.value();
    if (linkedNotebookAuthToken != expectedLinkedNotebookAuthToken) {
        errorDescription.setBase(QStringLiteral("Wrong linked notebook auth token"));
        QNWARNING(errorDescription << QStringLiteral(", expected: ") << expectedLinkedNotebookAuthToken
                  << QStringLiteral(", got: ") << linkedNotebookAuthToken
                  << QStringLiteral(", linked notebook: ") << linkedNotebook);
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    return 0;
}

qint32 FakeNoteStore::checkLinkedNotebookAuthTokenForNotebook(const QString & notebookGuid, const QString & linkedNotebookAuthToken,
                                                              ErrorString & errorDescription) const
{
    const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(notebookGuid);
    if (notebookIt == notebookGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("No notebook with specified guid was found"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    const Notebook & notebook = *notebookIt;
    if (!notebook.hasLinkedNotebookGuid()) {
        errorDescription.setBase(QStringLiteral("Notebook doesn't belong to a linked notebook"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    if (linkedNotebookAuthToken.isEmpty()) {
        errorDescription.setBase(QStringLiteral("Notebook belongs to a linked notebook but linked notebook auth token is empty"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    const QString & linkedNotebookGuid = notebook.linkedNotebookGuid();
    const LinkedNotebookDataByGuid & linkedNotebookGuidIndex = m_linkedNotebooks.get<LinkedNotebookByGuid>();
    auto linkedNotebookIt = linkedNotebookGuidIndex.find(linkedNotebookGuid);
    if (linkedNotebookIt == linkedNotebookGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("Found no linked notebook corresponding to the notebook"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    return checkLinkedNotebookAuthToken(*linkedNotebookIt, linkedNotebookAuthToken, errorDescription);
}

qint32 FakeNoteStore::checkLinkedNotebookAuthTokenForTag(const Tag & tag, const QString & linkedNotebookAuthToken,
                                                         ErrorString & errorDescription) const
{
    if (!linkedNotebookAuthToken.isEmpty() && !tag.hasLinkedNotebookGuid()) {
        errorDescription.setBase(QStringLiteral("Excess linked notebook auth token"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    if (!tag.hasLinkedNotebookGuid()) {
        errorDescription.setBase(QStringLiteral("Tag doesn't belong to a linked notebook"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    if (linkedNotebookAuthToken.isEmpty()) {
        errorDescription.setBase(QStringLiteral("Tag belongs to a linked notebook but linked notebook auth token is empty"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    const LinkedNotebookDataByGuid & linkedNotebookGuidIndex = m_linkedNotebooks.get<LinkedNotebookByGuid>();
    auto it = linkedNotebookGuidIndex.find(tag.linkedNotebookGuid());
    if (it == linkedNotebookGuidIndex.end()) {
        errorDescription.setBase(QStringLiteral("Tag belongs to a linked notebook but it was not found by guid"));
        return qevercloud::EDAMErrorCode::PERMISSION_DENIED;
    }

    return checkLinkedNotebookAuthToken(*it, linkedNotebookAuthToken, errorDescription);
}

QString FakeNoteStore::nextName(const QString & name) const
{
    int lastIndex = name.lastIndexOf(QStringLiteral("_"));
    if (lastIndex >= 0)
    {
        QString numStr = name.mid(lastIndex, name.size() - lastIndex - 1);
        bool conversionResult = false;
        int num = numStr.toInt(&conversionResult);
        if (conversionResult) {
            QString result = name.left(lastIndex);
            result.append(QStringLiteral("_") + QString::number(num + 1));
            return result;
        }
    }

    QString result = name;
    result.append(QStringLiteral("_") + QString::number(2));
    return result;
}

qint32 FakeNoteStore::getSyncChunkImpl(const qint32 afterUSN, const qint32 maxEntries,
                                       const bool fullSyncOnly, const QString & linkedNotebookGuid,
                                       const qevercloud::SyncChunkFilter & filter,
                                       qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                       qint32 & rateLimitSeconds)
{
    CHECK_API_RATE_LIMIT()

    if (afterUSN < 0) {
        errorDescription.setBase(QStringLiteral("After USN is negative"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    if (maxEntries < 1) {
        errorDescription.setBase(QStringLiteral("Max entries is less than 1"));
        return qevercloud::EDAMErrorCode::BAD_DATA_FORMAT;
    }

    syncChunk = qevercloud::SyncChunk();
    syncChunk.currentTime = QDateTime::currentMSecsSinceEpoch();

    if (filter.notebookGuids.isSet() && !filter.notebookGuids.ref().isEmpty() &&
        filter.includeExpunged.isSet() && filter.includeExpunged.ref())
    {
        errorDescription.setBase(QStringLiteral("Can't set notebook guids along with include expunged"));
        return qevercloud::EDAMErrorCode::DATA_CONFLICT;
    }

    const SavedSearchDataByUSN & savedSearchUsnIndex = m_savedSearches.get<SavedSearchByUSN>();
    const TagDataByUSN & tagUsnIndex = m_tags.get<TagByUSN>();
    const NotebookDataByUSN & notebookUsnIndex = m_notebooks.get<NotebookByUSN>();
    const NoteDataByUSN & noteUsnIndex = m_notes.get<NoteByUSN>();
    const ResourceDataByUSN & resourceUsnIndex = m_resources.get<ResourceByUSN>();
    const LinkedNotebookDataByUSN & linkedNotebookUsnIndex = m_linkedNotebooks.get<LinkedNotebookByUSN>();

    syncChunk.updateCount = currentMaxUsn(linkedNotebookGuid);
    QNDEBUG(QStringLiteral("Sync chunk update count = ") << syncChunk.updateCount);

    auto savedSearchIt = savedSearchUsnIndex.end();
    if (linkedNotebookGuid.isEmpty() && filter.includeSearches.isSet() && filter.includeSearches.ref()) {
        savedSearchIt = std::upper_bound(savedSearchUsnIndex.begin(), savedSearchUsnIndex.end(), afterUSN, CompareByUSN<SavedSearch>());
    }

    auto tagIt = tagUsnIndex.end();
    if (filter.includeTags.isSet() && filter.includeTags.ref()) {
        tagIt = std::upper_bound(tagUsnIndex.begin(), tagUsnIndex.end(), afterUSN, CompareByUSN<Tag>());
        tagIt = advanceIterator(tagIt, tagUsnIndex, linkedNotebookGuid);
    }

    auto notebookIt = notebookUsnIndex.end();
    if (filter.includeNotebooks.isSet() && filter.includeNotebooks.ref()) {
        notebookIt = std::upper_bound(notebookUsnIndex.begin(), notebookUsnIndex.end(), afterUSN, CompareByUSN<Notebook>());
        notebookIt = advanceIterator(notebookIt, notebookUsnIndex, linkedNotebookGuid);
    }

    auto noteIt = noteUsnIndex.end();
    if (filter.includeNotes.isSet() && filter.includeNotes.ref()) {
        noteIt = std::upper_bound(noteUsnIndex.begin(), noteUsnIndex.end(), afterUSN, CompareByUSN<Note>());
        noteIt = nextNoteByUsnIterator(noteIt, linkedNotebookGuid);
    }

    auto resourceIt = resourceUsnIndex.end();
    if (!fullSyncOnly && filter.includeResources.isSet() && filter.includeResources.ref()) {
        resourceIt = std::upper_bound(resourceUsnIndex.begin(), resourceUsnIndex.end(), afterUSN, CompareByUSN<Resource>());
        resourceIt = nextResourceByUsnIterator(resourceIt, linkedNotebookGuid);
    }

    auto linkedNotebookIt = linkedNotebookUsnIndex.end();
    if (linkedNotebookGuid.isEmpty() && filter.includeLinkedNotebooks.isSet() && filter.includeLinkedNotebooks.ref()) {
        linkedNotebookIt = std::upper_bound(linkedNotebookUsnIndex.begin(), linkedNotebookUsnIndex.end(), afterUSN, CompareByUSN<LinkedNotebook>());
    }

    while(true)
    {
        NextItemType::type nextItemType = NextItemType::None;
        qint32 lastItemUsn = std::numeric_limits<qint32>::max();

        if (savedSearchIt != savedSearchUsnIndex.end())
        {
            const SavedSearch & nextSearch = *savedSearchIt;
            QNDEBUG(QStringLiteral("Checking saved search for the possibility to include it into the sync chunk: ")
                    << nextSearch.qevercloudSavedSearch());

            qint32 usn = nextSearch.updateSequenceNumber();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::SavedSearch;
                QNDEBUG(QStringLiteral("Will include saved search into the sync chunk"));
            }
        }

        if ((nextItemType == NextItemType::None) && (tagIt != tagUsnIndex.end()))
        {
            const Tag & nextTag = *tagIt;
            QNDEBUG(QStringLiteral("Checking tag for the possibility to include it into the sync chunk: ")
                    << nextTag.qevercloudTag());

            qint32 usn = nextTag.updateSequenceNumber();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Tag;
                QNDEBUG(QStringLiteral("Will include tag into the sync chunk"));
            }
        }

        if ((nextItemType == NextItemType::None) && (notebookIt != notebookUsnIndex.end()))
        {
            const Notebook & nextNotebook = *notebookIt;
            QNDEBUG(QStringLiteral("Checking notebook for the possibility to include it into the sync chunk: ")
                    << nextNotebook.qevercloudNotebook());

            qint32 usn = nextNotebook.updateSequenceNumber();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Notebook;
                QNDEBUG(QStringLiteral("Will include notebook into the sync chunk"));
            }
        }

        if ((nextItemType == NextItemType::None) && (noteIt != noteUsnIndex.end()))
        {
            const Note & nextNote = *noteIt;
            QNDEBUG(QStringLiteral("Checking note for the possibility to include it into the sync chunk: ")
                    << nextNote.qevercloudNote());

            qint32 usn = nextNote.updateSequenceNumber();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Note;
                QNDEBUG(QStringLiteral("Will include note into the sync chunk"));
            }
        }

        if ((nextItemType == NextItemType::None) && (resourceIt != resourceUsnIndex.end()))
        {
            const Resource & nextResource = *resourceIt;
            QNDEBUG(QStringLiteral("Checking resource for the possibility to include it into the sync chunk: ")
                    << nextResource.qevercloudResource());

            qint32 usn = nextResource.updateSequenceNumber();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Resource;
                QNDEBUG(QStringLiteral("Will include resource into the sync chunk"));
            }
        }

        if ((nextItemType == NextItemType::None) && (linkedNotebookIt != linkedNotebookUsnIndex.end()))
        {
            const LinkedNotebook & nextLinkedNotebook = *linkedNotebookIt;
            QNDEBUG(QStringLiteral("Checking linked notebook for the possibility to include it into the sync chunk: ")
                    << nextLinkedNotebook.qevercloudLinkedNotebook());

            qint32 usn = nextLinkedNotebook.updateSequenceNumber();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::LinkedNotebook;
                QNDEBUG(QStringLiteral("Will include linked notebook into the sync chunk"));
            }
        }

        if (nextItemType == NextItemType::None) {
            break;
        }

        switch(nextItemType)
        {
        case NextItemType::SavedSearch:
            {
                if (!syncChunk.searches.isSet()) {
                    syncChunk.searches = QList<qevercloud::SavedSearch>();
                }

                syncChunk.searches->append(savedSearchIt->qevercloudSavedSearch());
                syncChunk.chunkHighUSN = savedSearchIt->updateSequenceNumber();
                QNDEBUG(QStringLiteral("Added saved search to sync chunk: ") << savedSearchIt->qevercloudSavedSearch()
                        << QStringLiteral("\nSync chunk high USN updated to ") << syncChunk.chunkHighUSN);
                ++savedSearchIt;
            }
            break;
        case NextItemType::Tag:
            {
                if (!syncChunk.tags.isSet()) {
                    syncChunk.tags = QList<qevercloud::Tag>();
                }

                syncChunk.tags->append(tagIt->qevercloudTag());
                syncChunk.chunkHighUSN = tagIt->updateSequenceNumber();
                QNDEBUG(QStringLiteral("Added tag to sync chunk: ") << tagIt->qevercloudTag()
                        << QStringLiteral("\nSync chunk high USN updated to ") << syncChunk.chunkHighUSN);

                ++tagIt;
                tagIt = advanceIterator(tagIt, tagUsnIndex, linkedNotebookGuid);
            }
            break;
        case NextItemType::Notebook:
            {
                if (!syncChunk.notebooks.isSet()) {
                    syncChunk.notebooks = QList<qevercloud::Notebook>();
                }

                syncChunk.notebooks->append(notebookIt->qevercloudNotebook());
                syncChunk.chunkHighUSN = notebookIt->updateSequenceNumber();
                QNDEBUG(QStringLiteral("Added notebook to sync chunk: ") << notebookIt->qevercloudNotebook()
                        << QStringLiteral("\nSync chunk high USN updated to ") << syncChunk.chunkHighUSN);
                ++notebookIt;
                notebookIt = advanceIterator(notebookIt, notebookUsnIndex, linkedNotebookGuid);
            }
            break;
        case NextItemType::Note:
            {
                if (!syncChunk.notes.isSet()) {
                    syncChunk.notes = QList<qevercloud::Note>();
                }

                qevercloud::Note qecNote = noteIt->qevercloudNote();

                if (!filter.includeNoteResources.isSet() || !filter.includeNoteResources.ref()) {
                    qecNote.resources.clear();
                }

                if (!filter.includeNoteAttributes.isSet() || !filter.includeNoteAttributes.ref())
                {
                    qecNote.attributes.clear();
                }
                else
                {
                    if ( (!filter.includeNoteApplicationDataFullMap.isSet() || !filter.includeNoteApplicationDataFullMap.ref()) &&
                         qecNote.attributes.isSet() && qecNote.attributes->applicationData.isSet() )
                    {
                        qecNote.attributes->applicationData->fullMap.clear();
                    }

                    if ( (!filter.includeNoteResourceApplicationDataFullMap.isSet() || !filter.includeNoteResourceApplicationDataFullMap.ref()) &&
                         qecNote.resources.isSet() )
                    {
                        for(auto it = qecNote.resources->begin(), end = qecNote.resources->end(); it != end; ++it)
                        {
                            qevercloud::Resource & resource = *it;
                            if (resource.attributes.isSet() && resource.attributes->applicationData.isSet()) {
                                resource.attributes->applicationData->fullMap.clear();
                            }
                        }
                    }
                }

                if (!filter.includeSharedNotes.isSet() || !filter.includeSharedNotes.ref()) {
                    qecNote.sharedNotes.clear();
                }

                // Notes within the sync chunks should include only note
                // metadata bt no content, resource content, resource
                // recognition data or resource alternate data
                qecNote.content.clear();
                if (qecNote.resources.isSet())
                {
                    for(auto it = qecNote.resources->begin(), end = qecNote.resources->end(); it != end; ++it)
                    {
                        qevercloud::Resource & resource = *it;
                        if (resource.data.isSet()) {
                            resource.data->body.clear();
                        }
                        if (resource.recognition.isSet()) {
                            resource.recognition->body.clear();
                        }
                        if (resource.alternateData.isSet()) {
                            resource.alternateData->body.clear();
                        }
                    }
                }

                syncChunk.notes->append(qecNote);
                syncChunk.chunkHighUSN = noteIt->updateSequenceNumber();
                QNDEBUG(QStringLiteral("Added note to sync chunk: ") << qecNote
                        << QStringLiteral("\nSync chunk high USN updated to ") << syncChunk.chunkHighUSN);
                ++noteIt;
                noteIt = nextNoteByUsnIterator(noteIt, linkedNotebookGuid);
            }
            break;
        case NextItemType::Resource:
            {
                if (!syncChunk.resources.isSet()) {
                    syncChunk.resources = QList<qevercloud::Resource>();
                }

                qevercloud::Resource qecResource = resourceIt->qevercloudResource();

                if ( (!filter.includeResourceApplicationDataFullMap.isSet() || !filter.includeResourceApplicationDataFullMap.ref()) &&
                     qecResource.attributes.isSet() && qecResource.attributes->applicationData.isSet() )
                {
                    qecResource.attributes->applicationData->fullMap.clear();
                }

                // Resources within the sync chunks should not include data,
                // recognition data or alternate data
                if (qecResource.data.isSet()) {
                    qecResource.data->body.clear();
                }

                if (qecResource.recognition.isSet()) {
                    qecResource.recognition->body.clear();
                }

                if (qecResource.alternateData.isSet()) {
                    qecResource.alternateData->body.clear();
                }

                syncChunk.resources->append(qecResource);
                syncChunk.chunkHighUSN = resourceIt->updateSequenceNumber();
                QNDEBUG(QStringLiteral("Added resource to sync chunk: ") << qecResource
                        << QStringLiteral("\nSync chunk high USN updated to ") << syncChunk.chunkHighUSN);
                ++resourceIt;
                resourceIt = nextResourceByUsnIterator(resourceIt, linkedNotebookGuid);
            }
            break;
        case NextItemType::LinkedNotebook:
            {
                if (!syncChunk.linkedNotebooks.isSet()) {
                    syncChunk.linkedNotebooks = QList<qevercloud::LinkedNotebook>();
                }

                syncChunk.linkedNotebooks->append(linkedNotebookIt->qevercloudLinkedNotebook());
                syncChunk.chunkHighUSN = linkedNotebookIt->updateSequenceNumber();
                QNDEBUG(QStringLiteral("Added linked notebook to sync chunk: ") << linkedNotebookIt->qevercloudLinkedNotebook()
                        << QStringLiteral("\nSync chunk high USN updated to ") << syncChunk.chunkHighUSN);
                ++linkedNotebookIt;
            }
            break;
        default:
            QNWARNING(QStringLiteral("Unexpected next item type: ") << nextItemType);
            break;
        }
    }

    if (!syncChunk.chunkHighUSN.isSet()) {
        syncChunk.chunkHighUSN = syncChunk.updateCount;
        QNDEBUG(QStringLiteral("Sync chunk's high USN was still not set, set it to the update count: ") << syncChunk.updateCount);
    }

    if (fullSyncOnly) {
        // No need to insert the information about expunged data items
        // when doing full sync
        return 0;
    }

    if (linkedNotebookGuid.isEmpty() && !m_expungedSavedSearchGuids.isEmpty())
    {
        if (!syncChunk.expungedSearches.isSet()) {
            syncChunk.expungedSearches = QList<qevercloud::Guid>();
        }

        syncChunk.expungedSearches->reserve(m_expungedSavedSearchGuids.size());

        for(auto it = m_expungedSavedSearchGuids.constBegin(),
            end = m_expungedSavedSearchGuids.constEnd(); it != end; ++it)
        {
            syncChunk.expungedSearches->append(*it);
        }
    }

    if (linkedNotebookGuid.isEmpty() && !m_expungedTagGuids.isEmpty())
    {
        if (!syncChunk.expungedTags.isSet()) {
            syncChunk.expungedTags = QList<qevercloud::Guid>();
        }

        syncChunk.expungedTags->reserve(m_expungedTagGuids.size());

        for(auto it = m_expungedTagGuids.constBegin(),
            end = m_expungedTagGuids.constEnd(); it != end; ++it)
        {
            syncChunk.expungedTags->append(*it);
        }
    }

    if (!m_expungedNotebookGuids.isEmpty())
    {
        if (!syncChunk.expungedNotebooks.isSet()) {
            syncChunk.expungedNotebooks = QList<qevercloud::Guid>();
        }

        syncChunk.expungedNotebooks->reserve(m_expungedNotebookGuids.size());
        for(auto it = m_expungedNotebookGuids.constBegin(),
            end = m_expungedNotebookGuids.constEnd(); it != end; ++it)
        {
            syncChunk.expungedNotebooks->append(*it);
        }
    }

    if (!m_expungedNoteGuids.isEmpty())
    {
        if (!syncChunk.expungedNotes.isSet()) {
            syncChunk.expungedNotes = QList<qevercloud::Guid>();
        }

        syncChunk.expungedNotes->reserve(m_expungedNoteGuids.size());
        for(auto it = m_expungedNoteGuids.constBegin(),
            end = m_expungedNoteGuids.constEnd(); it != end; ++it)
        {
            syncChunk.expungedNotes->append(*it);
        }
    }

    if (linkedNotebookGuid.isEmpty() && !m_expungedLinkedNotebookGuids.isEmpty())
    {
        if (!syncChunk.expungedLinkedNotebooks.isSet()) {
            syncChunk.expungedLinkedNotebooks = QList<qevercloud::Guid>();
        }

        syncChunk.expungedLinkedNotebooks->reserve(m_expungedLinkedNotebookGuids.size());
        for(auto it = m_expungedLinkedNotebookGuids.constBegin(),
            end = m_expungedLinkedNotebookGuids.constEnd(); it != end; ++it)
        {
            syncChunk.expungedLinkedNotebooks->append(*it);
        }
    }

    return 0;
}

template <class ConstIterator, class UsnIndex>
ConstIterator FakeNoteStore::advanceIterator(ConstIterator it, const UsnIndex & index, const QString & linkedNotebookGuid) const
{
    while(it != index.end())
    {
        if (linkedNotebookGuid.isEmpty() && it->hasLinkedNotebookGuid()) {
            ++it;
            continue;
        }

        if (!linkedNotebookGuid.isEmpty() && !it->hasLinkedNotebookGuid()) {
            ++it;
            continue;
        }

        if (!linkedNotebookGuid.isEmpty() && it->hasLinkedNotebookGuid() &&
            (linkedNotebookGuid != it->linkedNotebookGuid()))
        {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

FakeNoteStore::NoteDataByUSN::const_iterator FakeNoteStore::nextNoteByUsnIterator(NoteDataByUSN::const_iterator it,
                                                                                  const QString & targetLinkedNotebookGuid) const
{
    const NoteDataByUSN & noteUsnIndex = m_notes.get<NoteByUSN>();
    const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();

    while(it != noteUsnIndex.end())
    {
        const QString & notebookGuid = it->notebookGuid();
        auto noteNotebookIt = notebookGuidIndex.find(notebookGuid);
        if (Q_UNLIKELY(noteNotebookIt == notebookGuidIndex.end())) {
            QNWARNING(QStringLiteral("Found note which notebook guid doesn't correspond to any existing notebook: ") << *it);
            ++it;
            continue;
        }

        const Notebook & notebook = *noteNotebookIt;
        if (notebook.hasLinkedNotebookGuid() &&
            (targetLinkedNotebookGuid.isEmpty() || (notebook.linkedNotebookGuid() != targetLinkedNotebookGuid)))
        {
            ++it;
            continue;
        }
        else if (!notebook.hasLinkedNotebookGuid() && !targetLinkedNotebookGuid.isEmpty())
        {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

FakeNoteStore::ResourceDataByUSN::const_iterator FakeNoteStore::nextResourceByUsnIterator(ResourceDataByUSN::const_iterator it,
                                                                                          const QString & targetLinkedNotebookGuid) const
{
    const ResourceDataByUSN & resourceUsnIndex = m_resources.get<ResourceByUSN>();
    const NoteDataByGuid & noteGuidIndex = m_notes.get<NoteByGuid>();
    const NotebookDataByGuid & notebookGuidIndex = m_notebooks.get<NotebookByGuid>();
    while(it != resourceUsnIndex.end())
    {
        const QString & noteGuid = it->noteGuid();

        auto resourceNoteIt = noteGuidIndex.find(noteGuid);
        if (Q_UNLIKELY(resourceNoteIt == noteGuidIndex.end())) {
            QNWARNING(QStringLiteral("Found resource which note guid doesn't correspond to any existing note: ") << *it);
            ++it;
            continue;
        }

        const Note & note = *resourceNoteIt;
        const QString & notebookGuid = note.notebookGuid();
        auto noteNotebookIt = notebookGuidIndex.find(notebookGuid);
        if (Q_UNLIKELY(noteNotebookIt == notebookGuidIndex.end())) {
            QNWARNING(QStringLiteral("Found note which notebook guid doesn't correspond to any existing notebook: ") << note);
            ++it;
            continue;
        }

        const Notebook & notebook = *noteNotebookIt;
        if (notebook.hasLinkedNotebookGuid() &&
            (targetLinkedNotebookGuid.isEmpty() || (notebook.linkedNotebookGuid() != targetLinkedNotebookGuid)))
        {
            ++it;
            continue;
        }
        else if (!notebook.hasLinkedNotebookGuid() && !targetLinkedNotebookGuid.isEmpty())
        {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

} // namespace quentier
