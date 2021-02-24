/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/generated/types/AuthenticationResult.h>
#include <qevercloud/generated/types/SyncChunk.h>
#include <qevercloud/generated/types/SyncChunkFilter.h>

#include <QDateTime>
#include <QDebug>
#include <QRegExp>
#include <QTimerEvent>

#include <algorithm>
#include <limits>

namespace quentier {

FakeNoteStore::FakeNoteStore(QObject * parent) :
    INoteStore(parent), m_data(new Data)
{}

FakeNoteStore::FakeNoteStore(std::shared_ptr<Data> data) :
    INoteStore(), m_data(std::move(data))
{}

FakeNoteStore::~FakeNoteStore() = default;

QHash<QString, qevercloud::SavedSearch> FakeNoteStore::savedSearches() const
{
    QHash<QString, qevercloud::SavedSearch> result;
    result.reserve(static_cast<int>(m_data->m_savedSearches.size()));

    const auto & savedSearchByGuid =
        m_data->m_savedSearches.get<SavedSearchByGuid>();

    for (const auto & search: savedSearchByGuid) {
        result[search.guid().value()] = search;
    }

    return result;
}

bool FakeNoteStore::setSavedSearch(
    qevercloud::SavedSearch & search, ErrorString & errorDescription)
{
    if (!search.guid()) {
        errorDescription.setBase("Can't set saved search without guid");
        return false;
    }

    if (!search.name()) {
        errorDescription.setBase("Can't set saved search without name");
        return false;
    }

    auto & nameIndex = m_data->m_savedSearches.get<SavedSearchByNameUpper>();
    auto nameIt = nameIndex.find(search.name()->toUpper());
    while (nameIt != nameIndex.end()) {
        if (nameIt->guid() == search.guid()) {
            break;
        }

        QString name = nextName(*search.name());
        search.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    if (!search.updateSequenceNum()) {
        qint32 maxUsn = currentMaxUsn();
        ++maxUsn;
        search.setUpdateSequenceNum(maxUsn);

        if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
            auto & savedSearchGuids =
                m_data->m_guidsOfUserOwnCompleteSentItems.m_savedSearchGuids;

            const auto it = savedSearchGuids.find(*search.guid());
            if (it != savedSearchGuids.end()) {
                Q_UNUSED(savedSearchGuids.erase(it))
            }
        }
    }

    Q_UNUSED(removeExpungedSavedSearchGuid(*search.guid()))

    auto & savedSearchGuidIndex =
        m_data->m_savedSearches.get<SavedSearchByGuid>();

    const auto it = savedSearchGuidIndex.find(*search.guid());
    if (it == savedSearchGuidIndex.end()) {
        Q_UNUSED(m_data->m_savedSearches.insert(search))
    }
    else {
        Q_UNUSED(savedSearchGuidIndex.replace(it, search))
    }

    return true;
}

const qevercloud::SavedSearch * FakeNoteStore::findSavedSearch(
    const QString & guid) const
{
    const auto & index = m_data->m_savedSearches.get<SavedSearchByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return nullptr;
    }

    const auto & search = *it;
    return &search;
}

bool FakeNoteStore::removeSavedSearch(const QString & guid)
{
    auto & index = m_data->m_savedSearches.get<SavedSearchByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedSavedSearchGuid(const QString & guid)
{
    Q_UNUSED(removeSavedSearch(guid))
    Q_UNUSED(m_data->m_expungedSavedSearchGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedSavedSearchGuid(const QString & guid) const
{
    return m_data->m_expungedSavedSearchGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedSavedSearchGuid(const QString & guid)
{
    const auto it = m_data->m_expungedSavedSearchGuids.find(guid);
    if (it == m_data->m_expungedSavedSearchGuids.end()) {
        return false;
    }

    Q_UNUSED(m_data->m_expungedSavedSearchGuids.erase(it))
    return true;
}

QHash<QString, qevercloud::Tag> FakeNoteStore::tags() const
{
    QHash<QString, qevercloud::Tag> result;
    result.reserve(static_cast<int>(m_data->m_tags.size()));

    const auto & tagDataByGuid = m_data->m_tags.get<TagByGuid>();
    for (const auto & tag: tagDataByGuid) {
        result[tag.guid().value()] = tag;
    }

    return result;
}

bool FakeNoteStore::setTag(
    qevercloud::Tag & tag, ErrorString & errorDescription)
{
    QNDEBUG("tests:synchronization", "FakeNoteStore::setTag: tag = " << tag);

    if (!tag.guid()) {
        errorDescription.setBase("Can't set tag without guid");
        return false;
    }

    if (!tag.name()) {
        errorDescription.setBase("Can't set tag without name");
        return false;
    }

    if (tag.linkedNotebookGuid()) {
        const QString & linkedNotebookGuid = *tag.linkedNotebookGuid();

        const auto & index =
            m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

        const auto it = index.find(linkedNotebookGuid);
        if (it == index.end()) {
            errorDescription.setBase(
                "Can't set tag with linked notebook guid corresponding "
                "to no existing linked notebook");
            return false;
        }
    }

    auto & nameIndex = m_data->m_tags.get<TagByNameUpper>();
    auto nameIt = nameIndex.find(tag.name()->toUpper());
    while (nameIt != nameIndex.end()) {
        if (nameIt->guid() == tag.guid()) {
            break;
        }

        QString name = nextName(*tag.name());
        tag.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    if (!tag.updateSequenceNum()) {
        qint32 maxUsn = currentMaxUsn(
            tag.linkedNotebookGuid() ? *tag.linkedNotebookGuid() : QString{});

        ++maxUsn;
        tag.setUpdateSequenceNum(maxUsn);

        if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
            auto & guidsOfCompleteSentItems =
                (tag.linkedNotebookGuid()
                     ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                           [*tag.linkedNotebookGuid()]
                     : m_data->m_guidsOfUserOwnCompleteSentItems);

            auto it = guidsOfCompleteSentItems.m_tagGuids.find(*tag.guid());
            if (it != guidsOfCompleteSentItems.m_tagGuids.end()) {
                Q_UNUSED(guidsOfCompleteSentItems.m_tagGuids.erase(it))
            }
        }
    }

    if (!tag.linkedNotebookGuid()) {
        Q_UNUSED(removeExpungedTagGuid(*tag.guid()))
    }

    auto & tagGuidIndex = m_data->m_tags.get<TagByGuid>();
    const auto tagIt = tagGuidIndex.find(*tag.guid());
    if (tagIt == tagGuidIndex.end()) {
        Q_UNUSED(m_data->m_tags.insert(tag))
    }
    else {
        Q_UNUSED(tagGuidIndex.replace(tagIt, tag))
    }

    QNDEBUG("tests:synchronization", "Tag with complemented fields: " << tag);
    return true;
}

const qevercloud::Tag * FakeNoteStore::findTag(const QString & guid) const
{
    const auto & index = m_data->m_tags.get<TagByGuid>();
    const auto it = index.find(guid);
    if (it != index.end()) {
        return &(*it);
    }

    return nullptr;
}

bool FakeNoteStore::removeTag(const QString & guid)
{
    auto & index = m_data->m_tags.get<TagByGuid>();
    auto tagIt = index.find(guid);
    if (tagIt == index.end()) {
        return false;
    }

    const auto & parentTagGuidIndex = m_data->m_tags.get<TagByParentTagGuid>();

    auto range = parentTagGuidIndex.equal_range(guid);

    QStringList childTagGuids;

    childTagGuids.reserve(
        static_cast<int>(std::distance(range.first, range.second)));

    for (auto it = range.first; it != range.second; ++it) {
        childTagGuids << it->guid().value();
    }

    bool removedChildTags = false;
    for (const auto & childTagGuid: qAsConst(childTagGuids)) {
        removedChildTags |= removeTag(childTagGuid);
    }

    // NOTE: have to once again evaluate the iterator if we deleted any child
    // tags since the deletion of child tags could cause the invalidation of
    // the previously found iterator
    if (removedChildTags) {
        tagIt = index.find(guid);
        if (Q_UNLIKELY(tagIt == index.end())) {
            QNWARNING(
                "tests:synchronization",
                "Tag to be removed is not not "
                    << "found after the removal of its child tags: guid = "
                    << guid);
            return false;
        }
    }

    auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    for (auto noteIt = noteGuidIndex.begin(); noteIt != noteGuidIndex.end();
         ++noteIt)
    {
        const auto & note = *noteIt;
        if (!note.tagGuids() || note.tagGuids()->isEmpty()) {
            continue;
        }

        QStringList tagGuids = *note.tagGuids();
        Q_ASSERT(note.tagLocalIds().size() == tagGuids.size());
        int tagGuidIndex = tagGuids.indexOf(guid);
        if (tagGuidIndex >= 0) {
            tagGuids.removeAt(tagGuidIndex);
            auto noteCopy = note;
            noteCopy.setTagGuids(tagGuids);
            noteCopy.mutableTagLocalIds().removeAt(tagGuidIndex);
            noteGuidIndex.replace(noteIt, noteCopy);
        }
    }

    Q_UNUSED(index.erase(tagIt))
    return true;
}

void FakeNoteStore::setExpungedTagGuid(const QString & guid)
{
    Q_UNUSED(removeTag(guid))
    Q_UNUSED(m_data->m_expungedTagGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedTagGuid(const QString & guid) const
{
    return m_data->m_expungedTagGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedTagGuid(const QString & guid)
{
    const auto it = m_data->m_expungedTagGuids.find(guid);
    if (it == m_data->m_expungedTagGuids.end()) {
        return false;
    }

    Q_UNUSED(m_data->m_expungedTagGuids.erase(it))
    return true;
}

QHash<QString, qevercloud::Notebook> FakeNoteStore::notebooks() const
{
    QHash<QString, qevercloud::Notebook> result;
    result.reserve(static_cast<int>(m_data->m_notebooks.size()));

    const auto & notebookDataByGuid = m_data->m_notebooks.get<NotebookByGuid>();
    for (const auto & notebook: notebookDataByGuid) {
        result[notebook.guid().value()] = notebook;
    }

    return result;
}

bool FakeNoteStore::setNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription)
{
    if (!notebook.guid()) {
        errorDescription.setBase("Can't set notebook without guid");
        return false;
    }

    if (!notebook.name()) {
        errorDescription.setBase("Can't set notebook without name");
        return false;
    }

    if (notebook.linkedNotebookGuid()) {
        const QString & linkedNotebookGuid = *notebook.linkedNotebookGuid();

        const auto & index =
            m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

        auto it = index.find(linkedNotebookGuid);
        if (it == index.end()) {
            errorDescription.setBase(
                "Can't set notebook with linked notebook guid "
                "corresponding to no existing linked notebook");
            return false;
        }
    }

    auto & nameIndex = m_data->m_notebooks.get<NotebookByNameUpper>();
    auto nameIt = nameIndex.find(notebook.name()->toUpper());
    while (nameIt != nameIndex.end()) {
        if (nameIt->guid() == notebook.guid()) {
            break;
        }

        QString name = nextName(*notebook.name());
        notebook.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    if (!notebook.updateSequenceNum()) {
        qint32 maxUsn = currentMaxUsn(
            notebook.linkedNotebookGuid() ? *notebook.linkedNotebookGuid()
                                          : QString{});

        ++maxUsn;
        notebook.setUpdateSequenceNum(maxUsn);

        if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
            auto & guidsOfCompleteSentItems =
                (notebook.linkedNotebookGuid()
                     ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                           [*notebook.linkedNotebookGuid()]
                     : m_data->m_guidsOfUserOwnCompleteSentItems);

            auto it =
                guidsOfCompleteSentItems.m_notebookGuids.find(*notebook.guid());

            if (it != guidsOfCompleteSentItems.m_notebookGuids.end()) {
                Q_UNUSED(guidsOfCompleteSentItems.m_notebookGuids.erase(it))
            }
        }
    }

    if (!notebook.linkedNotebookGuid()) {
        Q_UNUSED(removeExpungedNotebookGuid(*notebook.guid()))
    }

    auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    const auto notebookIt = notebookGuidIndex.find(*notebook.guid());
    if (notebookIt == notebookGuidIndex.end()) {
        Q_UNUSED(m_data->m_notebooks.insert(notebook))
    }
    else {
        Q_UNUSED(notebookGuidIndex.replace(notebookIt, notebook))
    }

    Q_UNUSED(m_data->m_notebooks.insert(notebook))
    return true;
}

const qevercloud::Notebook * FakeNoteStore::findNotebook(
    const QString & guid) const
{
    const auto & index = m_data->m_notebooks.get<NotebookByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return nullptr;
    }

    const auto & notebook = *it;
    return &notebook;
}

bool FakeNoteStore::removeNotebook(const QString & guid)
{
    auto & index = m_data->m_notebooks.get<NotebookByGuid>();
    auto notebookIt = index.find(guid);
    if (notebookIt == index.end()) {
        return false;
    }

    const auto & noteDataByNotebookGuid =
        m_data->m_notes.get<NoteByNotebookGuid>();

    auto range = noteDataByNotebookGuid.equal_range(guid);

    QStringList noteGuids;

    noteGuids.reserve(
        static_cast<int>(std::distance(range.first, range.second)));

    for (auto it = range.first; it != range.second; ++it) {
        noteGuids << it->guid().value();
    }

    for (const auto & noteGuid: qAsConst(noteGuids)) {
        Q_UNUSED(removeNote(noteGuid))
    }

    Q_UNUSED(index.erase(notebookIt))
    return true;
}

QList<const qevercloud::Notebook *>
FakeNoteStore::findNotebooksForLinkedNotebookGuid(
    const QString & linkedNotebookGuid) const
{
    const auto & index =
        m_data->m_notebooks.get<NotebookByLinkedNotebookGuid>();

    auto range = index.equal_range(linkedNotebookGuid);
    QList<const qevercloud::Notebook *> notebooks;

    notebooks.reserve(
        static_cast<int>(std::distance(range.first, range.second)));

    for (auto it = range.first; it != range.second; ++it) {
        notebooks << &(*it);
    }

    return notebooks;
}

void FakeNoteStore::setExpungedNotebookGuid(const QString & guid)
{
    Q_UNUSED(removeNotebook(guid))
    Q_UNUSED(m_data->m_expungedNotebookGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedNotebookGuid(const QString & guid) const
{
    return m_data->m_expungedNotebookGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedNotebookGuid(const QString & guid)
{
    const auto it = m_data->m_expungedNotebookGuids.find(guid);
    if (it == m_data->m_expungedNotebookGuids.end()) {
        return false;
    }

    Q_UNUSED(m_data->m_expungedNotebookGuids.erase(it))
    return true;
}

QHash<QString, qevercloud::Note> FakeNoteStore::notes() const
{
    QHash<QString, qevercloud::Note> result;
    result.reserve(static_cast<int>(m_data->m_notes.size()));

    const auto & noteDataByGuid = m_data->m_notes.get<NoteByGuid>();
    for (auto it = noteDataByGuid.begin(), end = noteDataByGuid.end();
         it != end; ++it)
    {
        result[it->guid().value()] = *it;
    }

    return result;
}

bool FakeNoteStore::setNote(
    qevercloud::Note & note, ErrorString & errorDescription)
{
    if (!note.guid()) {
        errorDescription.setBase("Can't set note without guid");
        return false;
    }

    if (!note.notebookGuid()) {
        errorDescription.setBase("Can't set note without notebook guid");
        return false;
    }

    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(*note.notebookGuid());
    if (notebookIt == notebookGuidIndex.end()) {
        errorDescription.setBase(
            "Can't set note: no notebook was found for it by guid");
        return false;
    }

    if (!note.updateSequenceNum()) {
        qint32 maxUsn = currentMaxUsn(
            notebookIt->linkedNotebookGuid()
                ? *notebookIt->linkedNotebookGuid()
                : QString{});

        ++maxUsn;
        note.setUpdateSequenceNum(maxUsn);

        if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
            auto & guidsOfCompleteSentItems =
                (notebookIt->linkedNotebookGuid()
                     ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                           [*notebookIt->linkedNotebookGuid()]
                     : m_data->m_guidsOfUserOwnCompleteSentItems);

            const auto it = guidsOfCompleteSentItems.m_noteGuids.find(
                *note.guid());

            if (it != guidsOfCompleteSentItems.m_noteGuids.end()) {
                Q_UNUSED(guidsOfCompleteSentItems.m_noteGuids.erase(it))
            }
        }
    }

    if (!notebookIt->linkedNotebookGuid()) {
        Q_UNUSED(removeExpungedNoteGuid(*note.guid()))
    }

    auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(*note.guid());
    if (noteIt == noteGuidIndex.end()) {
        auto insertResult = noteGuidIndex.insert(note);
        noteIt = insertResult.first;
    }

    if (note.resources() && !note.resources()->isEmpty()) {
        auto resources = *note.resources();
        for (auto & resource: resources) {
            if (!resource.guid()) {
                resource.setGuid(UidGenerator::Generate());
            }

            if (!resource.noteGuid()) {
                resource.setNoteGuid(note.guid());
            }

            if (resource.noteLocalId().isEmpty()) {
                resource.setNoteLocalId(note.localId());
            }

            if (!setResource(resource, errorDescription)) {
                return false;
            }
        }

        auto originalResources = resources;

        for (auto & resource: resources) {
            // Won't store resource binary data along with notes
            if (resource.data()) {
                resource.mutableData()->setBody(std::nullopt);
            }

            if (resource.recognition()) {
                resource.mutableRecognition()->setBody(std::nullopt);
            }

            if (resource.alternateData()) {
                resource.mutableAlternateData()->setBody(std::nullopt);
            }
        }

        note.setResources(resources);
        Q_UNUSED(noteGuidIndex.replace(noteIt, note))
        note.setResources(originalResources);
    }
    else {
        Q_UNUSED(noteGuidIndex.replace(noteIt, note))
    }

    return true;
}

const qevercloud::Note * FakeNoteStore::findNote(const QString & guid) const
{
    const auto & index = m_data->m_notes.get<NoteByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return nullptr;
    }

    const auto & note = *it;
    return &note;
}

bool FakeNoteStore::removeNote(const QString & guid)
{
    auto & index = m_data->m_notes.get<NoteByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    const auto & note = *it;
    if (note.resources() && !note.resources()->isEmpty()) {
        auto resources = *note.resources();
        for (const auto & resource: qAsConst(resources)) {
            Q_UNUSED(removeResource(resource.guid().value()))
        }
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedNoteGuid(const QString & guid)
{
    Q_UNUSED(removeNote(guid))
    Q_UNUSED(m_data->m_expungedNoteGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedNoteGuid(const QString & guid) const
{
    return m_data->m_expungedNoteGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedNoteGuid(const QString & guid)
{
    auto it = m_data->m_expungedNoteGuids.find(guid);
    if (it == m_data->m_expungedNoteGuids.end()) {
        return false;
    }

    Q_UNUSED(m_data->m_expungedNoteGuids.erase(it))
    return true;
}

QList<qevercloud::Note> FakeNoteStore::getNotesByConflictSourceNoteGuid(
    const QString & conflictSourceNoteGuid) const
{
    const auto & index = m_data->m_notes.get<NoteByConflictSourceNoteGuid>();
    auto range = index.equal_range(conflictSourceNoteGuid);
    QList<qevercloud::Note> result;
    result.reserve(static_cast<int>(std::distance(range.first, range.second)));
    for (auto it = range.first; it != range.second; ++it) {
        result << *it;
    }
    return result;
}

QHash<QString, qevercloud::Resource> FakeNoteStore::resources() const
{
    QHash<QString, qevercloud::Resource> result;
    result.reserve(static_cast<int>(m_data->m_resources.size()));

    const auto & resourceDataByGuid = m_data->m_resources.get<ResourceByGuid>();
    for (auto it = resourceDataByGuid.begin(), end = resourceDataByGuid.end();
         it != end; ++it)
    {
        result[it->guid().value()] = *it;
    }

    return result;
}

bool FakeNoteStore::setResource(
    qevercloud::Resource & resource, ErrorString & errorDescription)
{
    if (!resource.guid()) {
        errorDescription.setBase("Can't set resource without guid");
        return false;
    }

    if (!resource.noteGuid()) {
        errorDescription.setBase("Can't set resource without note guid");
        return false;
    }

    auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(*resource.noteGuid());
    if (noteIt == noteGuidIndex.end()) {
        errorDescription.setBase(
            "Can't set resource: no note was found for it by guid");
        return false;
    }

    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(*noteIt->notebookGuid());
    if (notebookIt == notebookGuidIndex.end()) {
        errorDescription.setBase(
            "Can't set resource: no notebook was found for resource's note "
            "by notebook guid");
        return false;
    }

    if (!resource.updateSequenceNum()) {
        qint32 maxUsn = currentMaxUsn(
            notebookIt->linkedNotebookGuid()
                ? *notebookIt->linkedNotebookGuid()
                : QString{});

        ++maxUsn;
        resource.setUpdateSequenceNum(maxUsn);

        if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
            auto & guidsOfCompleteSentItems =
                (notebookIt->linkedNotebookGuid()
                     ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                           [*notebookIt->linkedNotebookGuid()]
                     : m_data->m_guidsOfUserOwnCompleteSentItems);

            const auto it =
                guidsOfCompleteSentItems.m_resourceGuids.find(*resource.guid());

            if (it != guidsOfCompleteSentItems.m_resourceGuids.end()) {
                Q_UNUSED(guidsOfCompleteSentItems.m_resourceGuids.erase(it))
            }
        }
    }

    auto & resourceGuidIndex = m_data->m_resources.get<ResourceByGuid>();
    const auto it = resourceGuidIndex.find(*resource.guid());
    if (it == resourceGuidIndex.end()) {
        Q_UNUSED(resourceGuidIndex.insert(resource))
    }
    else {
        Q_UNUSED(resourceGuidIndex.replace(it, resource))
    }

    if (!noteIt->isLocallyModified()) {
        qevercloud::Note note{*noteIt};
        note.setLocallyModified(true);
        noteGuidIndex.replace(noteIt, note);
    }

    return true;
}

const qevercloud::Resource * FakeNoteStore::findResource(
    const QString & guid) const
{
    const auto & index = m_data->m_resources.get<ResourceByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return nullptr;
    }

    const auto & resource = *it;
    return &resource;
}

bool FakeNoteStore::removeResource(const QString & guid)
{
    auto & index = m_data->m_resources.get<ResourceByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    const QString & noteGuid = it->noteGuid().value();
    auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    const auto noteIt = noteGuidIndex.find(noteGuid);
    if (noteIt != noteGuidIndex.end()) {
        qevercloud::Note note{*noteIt};
        if (note.resources() && !note.resources()->isEmpty()) {
            auto resourceIt = std::find_if(
                note.mutableResources()->begin(),
                note.mutableResources()->end(),
                [resourceGuid = it->guid()]
                (const qevercloud::Resource & resource)
                {
                    return resource.guid() == resourceGuid;
                });
            if (resourceIt != note.mutableResources()->end()) {
                note.mutableResources()->erase(resourceIt);
            }
        }
        Q_UNUSED(noteGuidIndex.replace(noteIt, note))
    }
    else {
        QNWARNING(
            "tests:synchronization",
            "Found no note corresponding to the removed resource: " << *it);
    }

    Q_UNUSED(index.erase(it))
    return true;
}

QHash<QString, qevercloud::LinkedNotebook> FakeNoteStore::linkedNotebooks()
    const
{
    QHash<QString, qevercloud::LinkedNotebook> result;
    result.reserve(static_cast<int>(m_data->m_linkedNotebooks.size()));

    const auto & linkedNotebookDataByGuid =
        m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

    for (const auto & linkedNotebook: linkedNotebookDataByGuid) {
        result[linkedNotebook.guid().value()] = linkedNotebook;
    }

    return result;
}

bool FakeNoteStore::setLinkedNotebook(
    qevercloud::LinkedNotebook & linkedNotebook, ErrorString & errorDescription)
{
    if (!linkedNotebook.guid()) {
        errorDescription.setBase("Can't set linked notebook without guid");
        return false;
    }

    if (!linkedNotebook.username()) {
        errorDescription.setBase("Can't set linked notebook without username");
        return false;
    }

    if (!linkedNotebook.shardId() && !linkedNotebook.uri()) {
        errorDescription.setBase(
            "Can't set linked notebook without either shard id or uri");
        return false;
    }

    if (!linkedNotebook.sharedNotebookGlobalId()) {
        linkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    linkedNotebook.setUpdateSequenceNum(maxUsn);

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        auto & linkedNotebookGuids =
            m_data->m_guidsOfUserOwnCompleteSentItems.m_linkedNotebookGuids;

        auto it = linkedNotebookGuids.find(*linkedNotebook.guid());
        if (it != linkedNotebookGuids.end()) {
            Q_UNUSED(linkedNotebookGuids.erase(it))
        }
    }

    Q_UNUSED(removeExpungedLinkedNotebookGuid(*linkedNotebook.guid()))

    auto & index = m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();
    const auto it = index.find(*linkedNotebook.guid());
    if (it == index.end()) {
        Q_UNUSED(index.insert(linkedNotebook))
    }
    else {
        Q_UNUSED(index.replace(it, linkedNotebook))
    }

    return true;
}

const qevercloud::LinkedNotebook * FakeNoteStore::findLinkedNotebook(
    const QString & guid) const
{
    const auto & index = m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return nullptr;
    }

    const auto & linkedNotebook = *it;
    return &linkedNotebook;
}

bool FakeNoteStore::removeLinkedNotebook(const QString & guid)
{
    auto & index = m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return false;
    }

    Q_UNUSED(index.erase(it))
    return true;
}

void FakeNoteStore::setExpungedLinkedNotebookGuid(const QString & guid)
{
    Q_UNUSED(removeLinkedNotebook(guid))
    Q_UNUSED(m_data->m_expungedLinkedNotebookGuids.insert(guid))
}

bool FakeNoteStore::containsExpungedLinkedNotebookGuid(
    const QString & guid) const
{
    return m_data->m_expungedLinkedNotebookGuids.contains(guid);
}

bool FakeNoteStore::removeExpungedLinkedNotebookGuid(const QString & guid)
{
    const auto it = m_data->m_expungedLinkedNotebookGuids.find(guid);
    if (it == m_data->m_expungedLinkedNotebookGuids.end()) {
        return false;
    }

    Q_UNUSED(m_data->m_expungedLinkedNotebookGuids.erase(it))
    return true;
}

quint32 FakeNoteStore::maxNumSavedSearches() const
{
    return m_data->m_maxNumSavedSearches;
}

void FakeNoteStore::setMaxNumSavedSearches(const quint32 maxNumSavedSearches)
{
    m_data->m_maxNumSavedSearches = maxNumSavedSearches;
}

quint32 FakeNoteStore::maxNumTags() const
{
    return m_data->m_maxNumTags;
}

void FakeNoteStore::setMaxNumTags(const quint32 maxNumTags)
{
    m_data->m_maxNumTags = maxNumTags;
}

quint32 FakeNoteStore::maxNumNotebooks() const
{
    return m_data->m_maxNumNotebooks;
}

void FakeNoteStore::setMaxNumNotebooks(const quint32 maxNumNotebooks)
{
    m_data->m_maxNumNotebooks = maxNumNotebooks;
}

quint32 FakeNoteStore::maxNumNotes() const
{
    return m_data->m_maxNumNotes;
}

void FakeNoteStore::setMaxNumNotes(const quint32 maxNumNotes)
{
    m_data->m_maxNumNotes = maxNumNotes;
}

quint64 FakeNoteStore::maxNoteSize() const
{
    return m_data->m_maxNoteSize;
}

void FakeNoteStore::setMaxNoteSize(const quint64 maxNoteSize)
{
    m_data->m_maxNoteSize = maxNoteSize;
}

quint32 FakeNoteStore::maxNumResourcesPerNote() const
{
    return m_data->m_maxNumResourcesPerNote;
}

void FakeNoteStore::setMaxNumResourcesPerNote(
    const quint32 maxNumResourcesPerNote)
{
    m_data->m_maxNumResourcesPerNote = maxNumResourcesPerNote;
}

quint32 FakeNoteStore::maxNumTagsPerNote() const
{
    return m_data->m_maxNumTagsPerNote;
}

void FakeNoteStore::setMaxNumTagsPerNote(const quint32 maxNumTagsPerNote)
{
    m_data->m_maxNumTagsPerNote = maxNumTagsPerNote;
}

quint64 FakeNoteStore::maxResourceSize() const
{
    return m_data->m_maxResourceSize;
}

void FakeNoteStore::setMaxResourceSize(const quint64 maxResourceSize)
{
    m_data->m_maxResourceSize = maxResourceSize;
}

void FakeNoteStore::setSyncState(const qevercloud::SyncState & syncState)
{
    m_data->m_syncState = syncState;
}

const qevercloud::SyncState * FakeNoteStore::findLinkedNotebookSyncState(
    const QString & linkedNotebookOwner) const
{
    auto it = m_data->m_linkedNotebookSyncStates.find(linkedNotebookOwner);
    if (it != m_data->m_linkedNotebookSyncStates.end()) {
        return &(it.value());
    }

    return nullptr;
}

void FakeNoteStore::setLinkedNotebookSyncState(
    const QString & linkedNotebookOwner,
    const qevercloud::SyncState & syncState)
{
    QNDEBUG(
        "tests:synchronization",
        "FakeNoteStore::setLinkedNotebookSyncState: linked notebook owner: "
            << linkedNotebookOwner << ", sync state: " << syncState);

    m_data->m_linkedNotebookSyncStates[linkedNotebookOwner] = syncState;
}

bool FakeNoteStore::removeLinkedNotebookSyncState(
    const QString & linkedNotebookOwner)
{
    auto it = m_data->m_linkedNotebookSyncStates.find(linkedNotebookOwner);
    if (it != m_data->m_linkedNotebookSyncStates.end()) {
        Q_UNUSED(m_data->m_linkedNotebookSyncStates.erase(it))
        return true;
    }

    return false;
}

const QString & FakeNoteStore::authToken() const
{
    return m_data->m_authenticationToken;
}

void FakeNoteStore::setAuthToken(const QString & authToken)
{
    QNDEBUG(
        "tests:synchronization", "FakeNoteStore::setAuthToken: " << authToken);

    m_data->m_authenticationToken = authToken;
}

QString FakeNoteStore::linkedNotebookAuthToken(
    const QString & linkedNotebookOwner) const
{
    auto it = m_data->m_linkedNotebookAuthTokens.find(linkedNotebookOwner);
    if (it != m_data->m_linkedNotebookAuthTokens.end()) {
        return it.value();
    }

    return QString();
}

void FakeNoteStore::setLinkedNotebookAuthToken(
    const QString & linkedNotebookOwner,
    const QString & linkedNotebookAuthToken)
{
    QNDEBUG(
        "tests:synchronization",
        "FakeNoteStore::setLinkedNotebookAuthToken: owner = "
            << linkedNotebookOwner << ", token = " << linkedNotebookAuthToken);

    m_data->m_linkedNotebookAuthTokens[linkedNotebookOwner] =
        linkedNotebookAuthToken;
}

bool FakeNoteStore::removeLinkedNotebookAuthToken(
    const QString & linkedNotebookOwner)
{
    auto it = m_data->m_linkedNotebookAuthTokens.find(linkedNotebookOwner);
    if (it != m_data->m_linkedNotebookAuthTokens.end()) {
        Q_UNUSED(m_data->m_linkedNotebookAuthTokens.erase(it))
        return true;
    }

    return false;
}

qint32 FakeNoteStore::currentMaxUsn(const QString & linkedNotebookGuid) const
{
    QNDEBUG(
        "tests:synchronization",
        "FakeNoteStore::currentMaxUsn: linked "
            << "notebook guid = " << linkedNotebookGuid);

    qint32 maxUsn = 0;

    const auto & savedSearchUsnIndex =
        m_data->m_savedSearches.get<SavedSearchByUSN>();

    if (linkedNotebookGuid.isEmpty() && !savedSearchUsnIndex.empty()) {
        auto lastSavedSearchIt = savedSearchUsnIndex.end();
        --lastSavedSearchIt;

        QNTRACE(
            "tests:synchronization",
            "Examing saved search " << *lastSavedSearchIt);

        if (lastSavedSearchIt->updateSequenceNum().value() > maxUsn) {
            maxUsn = *lastSavedSearchIt->updateSequenceNum();
            QNTRACE(
                "tests:synchronization",
                "Updated max USN to "
                    << maxUsn << " from saved search: " << *lastSavedSearchIt);
        }
    }

    const auto & tagUsnIndex = m_data->m_tags.get<TagByUSN>();
    if (!tagUsnIndex.empty()) {
        auto tagIt = tagUsnIndex.end();
        --tagIt;

        for (size_t tagCounter = 0, numTags = tagUsnIndex.size();
             tagCounter < numTags; ++tagCounter, --tagIt)
        {
            QNTRACE("tests:synchronization", "Examing tag: " << *tagIt);

            bool matchesByLinkedNotebook =
                ((!linkedNotebookGuid.isEmpty() &&
                  tagIt->linkedNotebookGuid() &&
                  (*tagIt->linkedNotebookGuid() == linkedNotebookGuid)) ||
                 (linkedNotebookGuid.isEmpty() &&
                  !tagIt->linkedNotebookGuid()));

            if (!matchesByLinkedNotebook) {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping tag "
                        << *tagIt
                        << "\nAs it doesn't match by linked notebook");
                continue;
            }

            if (tagIt->updateSequenceNum().value() > maxUsn) {
                maxUsn = *tagIt->updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated max USN to " << maxUsn << " from tag: " << *tagIt);
            }
            else {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping tag " << *tagIt
                                    << "\nAs its USN is no greater than "
                                    << "max USN " << maxUsn);
            }
        }
    }

    const auto & notebookUsnIndex = m_data->m_notebooks.get<NotebookByUSN>();
    if (!notebookUsnIndex.empty()) {
        auto notebookIt = notebookUsnIndex.end();
        --notebookIt;

        for (size_t notebookCounter = 0, numNotebooks = notebookUsnIndex.size();
             notebookCounter < numNotebooks; ++notebookCounter, --notebookIt)
        {
            QNTRACE(
                "tests:synchronization", "Examing notebook: " << *notebookIt);

            bool matchesByLinkedNotebook =
                ((!linkedNotebookGuid.isEmpty() &&
                  notebookIt->linkedNotebookGuid() &&
                  (*notebookIt->linkedNotebookGuid() == linkedNotebookGuid)) ||
                 (linkedNotebookGuid.isEmpty() &&
                  !notebookIt->linkedNotebookGuid()));

            if (!matchesByLinkedNotebook) {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping notebook " << *notebookIt
                                         << "\nAs it doesn't match by linked "
                                         << "notebook");
                continue;
            }

            if (notebookIt->updateSequenceNum().value() > maxUsn) {
                maxUsn = *notebookIt->updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated max USN to " << maxUsn
                                          << " from notebook: " << *notebookIt);
            }
            else {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping notebook "
                        << *notebookIt
                        << "\nAs its USN is no greater than max USN "
                        << maxUsn);
            }
        }
    }

    const auto & noteUsnIndex = m_data->m_notes.get<NoteByUSN>();
    if (!noteUsnIndex.empty()) {
        const auto & notebookGuidIndex =
            m_data->m_notebooks.get<NotebookByGuid>();

        auto noteIt = noteUsnIndex.end();
        --noteIt;

        for (size_t noteCounter = 0, numNotes = noteUsnIndex.size();
             noteCounter < numNotes; ++noteCounter, --noteIt)
        {
            QNTRACE("tests:synchronization", "Examing note: " << *noteIt);

            bool matchesByLinkedNotebook = false;

            auto notebookIt = notebookGuidIndex.find(
                noteIt->notebookGuid().value());

            if (notebookIt != notebookGuidIndex.end()) {
                matchesByLinkedNotebook =
                    ((!linkedNotebookGuid.isEmpty() &&
                      notebookIt->linkedNotebookGuid() &&
                      (*notebookIt->linkedNotebookGuid() ==
                       linkedNotebookGuid)) ||
                     (linkedNotebookGuid.isEmpty() &&
                      !notebookIt->linkedNotebookGuid()));
            }

            if (!matchesByLinkedNotebook) {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping note "
                        << *noteIt
                        << "\nAs it doesn't match by linked notebook");
                continue;
            }

            if (noteIt->updateSequenceNum().value() > maxUsn) {
                maxUsn = *noteIt->updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated max USN to " << maxUsn
                                          << " from note: " << *noteIt);
            }
            else {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping note "
                        << *noteIt << "\nAs its USN is no greater than max USN "
                        << maxUsn);
            }
        }
    }

    const auto & resourceUsnIndex = m_data->m_resources.get<ResourceByUSN>();

    if (!resourceUsnIndex.empty()) {
        const auto & notebookGuidIndex =
            m_data->m_notebooks.get<NotebookByGuid>();

        const auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();

        auto resourceIt = resourceUsnIndex.end();
        --resourceIt;

        for (size_t resourceCounter = 0, numResources = resourceUsnIndex.size();
             resourceCounter < numResources; ++resourceCounter, --resourceIt)
        {
            QNTRACE(
                "tests:synchronization", "Examing resource: " << *resourceIt);

            bool matchesByLinkedNotebook = false;
            auto noteIt = noteGuidIndex.find(resourceIt->noteGuid().value());
            if (noteIt != noteGuidIndex.end()) {
                auto notebookIt =
                    notebookGuidIndex.find(noteIt->notebookGuid().value());

                if (notebookIt != notebookGuidIndex.end()) {
                    matchesByLinkedNotebook =
                        ((!linkedNotebookGuid.isEmpty() &&
                          notebookIt->linkedNotebookGuid() &&
                          (*notebookIt->linkedNotebookGuid() ==
                           linkedNotebookGuid)) ||
                         (linkedNotebookGuid.isEmpty() &&
                          !notebookIt->linkedNotebookGuid()));
                }
            }

            if (!matchesByLinkedNotebook) {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping resource " << *resourceIt
                                         << "\nAs it doesn't match by linked "
                                         << "notebook");
                continue;
            }

            if (resourceIt->updateSequenceNum().value() > maxUsn) {
                maxUsn = *resourceIt->updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated max USN to " << maxUsn
                                          << " from resource: " << *resourceIt);
            }
            else {
                QNTRACE(
                    "tests:synchronization",
                    "Skipping resource "
                        << *resourceIt
                        << "\nAs its USN is no greater than max USN "
                        << maxUsn);
            }
        }
    }

    const auto & linkedNotebookUsnIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByUSN>();

    if (linkedNotebookGuid.isEmpty() && !linkedNotebookUsnIndex.empty()) {
        auto lastLinkedNotebookIt = linkedNotebookUsnIndex.end();
        --lastLinkedNotebookIt;

        QNTRACE(
            "tests:synchronization",
            "Examing linked notebook " << *lastLinkedNotebookIt);

        if (lastLinkedNotebookIt->updateSequenceNum().value() > maxUsn) {
            maxUsn = *lastLinkedNotebookIt->updateSequenceNum();
            QNTRACE(
                "tests:synchronization",
                "Updated max USN from linked notebook to " << maxUsn);
        }
    }

    QNDEBUG("tests:synchronization", "Overall max USN = " << maxUsn);
    return maxUsn;
}

FakeNoteStore::APIRateLimitsTrigger FakeNoteStore::apiRateLimitsTrigger() const
{
    return m_data->m_APIRateLimitsTrigger;
}

void FakeNoteStore::setAPIRateLimitsExceedingTrigger(
    const APIRateLimitsTrigger trigger)
{
    m_data->m_APIRateLimitsTrigger = trigger;
}

void FakeNoteStore::considerAllExistingDataItemsSentBeforeRateLimitBreach()
{
    considerAllExistingDataItemsSentBeforeRateLimitBreachImpl();

    const auto & linkedNotebookGuidIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

    for (const auto & linkedNotebook: linkedNotebookGuidIndex) {
        considerAllExistingDataItemsSentBeforeRateLimitBreachImpl(
            linkedNotebook.guid().value());
    }
}

qint32
FakeNoteStore::smallestUsnOfNotCompletelySentDataItemBeforeRateLimitBreach(
    const QString & linkedNotebookGuid) const
{
    QNDEBUG(
        "tests:synchronization",
        "FakeNoteStore"
            << "::smallestUsnOfNotCompletelySentDataItemBeforeRateLimitBreach: "
            << "linked notebook guid = " << linkedNotebookGuid);

    auto git = m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid.end();
    if (!linkedNotebookGuid.isEmpty()) {
        git = m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid.find(
            linkedNotebookGuid);

        if (git == m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid.end())
        {
            return -1;
        }
    }

    const auto & guidsOfCompleteSentItems =
        (linkedNotebookGuid.isEmpty()
             ? m_data->m_guidsOfUserOwnCompleteSentItems
             : git.value());

    qint32 smallestUsn = -1;

    if (linkedNotebookGuid.isEmpty()) {
        const auto & savedSearchUsnIndex =
            m_data->m_savedSearches.get<SavedSearchByUSN>();

        for (const auto & savedSearch: savedSearchUsnIndex) {
            QNTRACE(
                "tests:synchronization",
                "Processing saved search: " << savedSearch);

            auto guidIt = guidsOfCompleteSentItems.m_savedSearchGuids.find(
                savedSearch.guid().value());

            if (guidIt == guidsOfCompleteSentItems.m_savedSearchGuids.end()) {
                QNTRACE(
                    "tests:synchronization",
                    "Encountered first notebook "
                        << "not within the list of sent items");

                if ((smallestUsn < 0) ||
                    (smallestUsn > savedSearch.updateSequenceNum().value())) {
                    smallestUsn = *savedSearch.updateSequenceNum();
                    QNTRACE(
                        "tests:synchronization",
                        "Updated smallest USN to " << smallestUsn);
                }

                break;
            }
        }
    }

    const auto & notebookUsnIndex = m_data->m_notebooks.get<NotebookByUSN>();
    for (const auto & notebook: notebookUsnIndex) {
        if (linkedNotebookGuid.isEmpty() ==
            notebook.linkedNotebookGuid().has_value()) {
            QNTRACE(
                "tests:synchronization",
                "Skipping notebook not matching "
                    << "by linked notebook criteria: " << notebook);
            continue;
        }
        if (notebook.linkedNotebookGuid() &&
            (*notebook.linkedNotebookGuid() != linkedNotebookGuid))
        {
            QNTRACE(
                "tests:synchronization",
                "Skipping notebook not matching "
                    << "by linked notebook criteria: " << notebook);
            continue;
        }

        QNTRACE("tests:synchronization", "Processing notebook: " << notebook);

        auto guidIt = guidsOfCompleteSentItems.m_notebookGuids.find(
            notebook.guid().value());

        if (guidIt == guidsOfCompleteSentItems.m_notebookGuids.end()) {
            QNTRACE(
                "tests:synchronization",
                "Encountered first notebook not "
                    << "within the list of sent items");

            if ((smallestUsn < 0) ||
                (smallestUsn > notebook.updateSequenceNum().value())) {
                smallestUsn = *notebook.updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated smallest USN to " << smallestUsn);
            }

            break;
        }
    }

    const auto & tagUsnIndex = m_data->m_tags.get<TagByUSN>();
    for (const auto & tag: tagUsnIndex) {
        if (linkedNotebookGuid.isEmpty() ==
            tag.linkedNotebookGuid().has_value()) {
            QNTRACE(
                "tests:synchronization",
                "Skipping tag not matching by linked notebook criteria: "
                    << tag);
            continue;
        }
        if (tag.linkedNotebookGuid() &&
            (*tag.linkedNotebookGuid() != linkedNotebookGuid))
        {
            QNTRACE(
                "tests:synchronization",
                "Skipping tag not matching by linked notebook criteria: "
                    << tag);
            continue;
        }

        QNTRACE("tests:synchronization", "Processing tag: " << tag);

        auto guidIt = guidsOfCompleteSentItems.m_tagGuids.find(
            tag.guid().value());

        if (guidIt == guidsOfCompleteSentItems.m_tagGuids.end()) {
            QNTRACE(
                "tests:synchronization",
                "Encountered first tag not within the list of sent items");

            if ((smallestUsn < 0) ||
                (smallestUsn > tag.updateSequenceNum().value()))
            {
                smallestUsn = *tag.updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated smallest USN to " << smallestUsn);
            }

            break;
        }
    }

    if (linkedNotebookGuid.isEmpty()) {
        const auto & linkedNotebookUsnIndex =
            m_data->m_linkedNotebooks.get<LinkedNotebookByUSN>();

        for (const auto & linkedNotebook: linkedNotebookUsnIndex) {
            QNTRACE(
                "tests:synchronization",
                "Processing linked notebook: " << linkedNotebook);

            auto guidIt = guidsOfCompleteSentItems.m_linkedNotebookGuids.find(
                linkedNotebook.guid().value());

            if (guidIt == guidsOfCompleteSentItems.m_linkedNotebookGuids.end())
            {
                QNTRACE(
                    "tests:synchronization",
                    "Encountered first linked notebook not within the list of "
                        << "sent items");

                if ((smallestUsn < 0) ||
                    (smallestUsn > linkedNotebook.updateSequenceNum().value()))
                {
                    smallestUsn = *linkedNotebook.updateSequenceNum();
                    QNTRACE(
                        "tests:synchronization",
                        "Updated smallest USN to " << smallestUsn);
                }

                break;
            }
        }
    }

    const auto & noteUsnIndex = m_data->m_notes.get<NoteByUSN>();
    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    for (const auto & note: noteUsnIndex) {
        auto notebookIt = notebookGuidIndex.find(note.notebookGuid().value());
        if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Skipping note for which no "
                    << "notebook was found: " << note);
            continue;
        }

        const auto & notebook = *notebookIt;
        if (linkedNotebookGuid.isEmpty() ==
            notebook.linkedNotebookGuid().has_value()) {
            QNTRACE(
                "tests:synchronization",
                "Skipping note as its notebook "
                    << "doesn't match by linked notebook criteria: " << note
                    << "\nNotebook: " << notebook);
            continue;
        }

        if (notebook.linkedNotebookGuid() &&
            (*notebook.linkedNotebookGuid() != linkedNotebookGuid))
        {
            QNTRACE(
                "tests:synchronization",
                "Skipping note as its notebook "
                    << "doesn't match by linked notebook criteria: " << note
                    << "\nNotebook: " << notebook);
            continue;
        }

        QNTRACE("tests:synchronization", "Processing note: " << note);

        auto guidIt = guidsOfCompleteSentItems.m_noteGuids.find(
            note.guid().value());

        if (guidIt == guidsOfCompleteSentItems.m_noteGuids.end()) {
            QNTRACE(
                "tests:synchronization",
                "Encountered first note not "
                    << "within the list of sent items");

            if ((smallestUsn < 0) ||
                (smallestUsn > note.updateSequenceNum().value())) {
                smallestUsn = *note.updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated smallest USN to " << smallestUsn);
            }

            break;
        }
    }

    const auto & resourceUsnIndex = m_data->m_resources.get<ResourceByUSN>();
    const auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    for (const auto & resource: resourceUsnIndex) {
        auto noteIt = noteGuidIndex.find(resource.noteGuid().value());
        if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Skipping resource for which no note was found: " << resource);
            continue;
        }

        const auto & note = *noteIt;
        auto notebookIt = notebookGuidIndex.find(note.notebookGuid().value());
        if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Skipping resource for which "
                    << "note no notebook was found: " << note);
            continue;
        }

        const auto & notebook = *notebookIt;
        if (linkedNotebookGuid.isEmpty() ==
            notebook.linkedNotebookGuid().has_value()) {
            QNTRACE(
                "tests:synchronization",
                "Skipping resource as its note's "
                    << "notebook doesn't match by linked notebook criteria: "
                    << resource << "\nNote: " << note
                    << "\nNotebook: " << notebook);
            continue;
        }

        if (notebook.linkedNotebookGuid() &&
            (*notebook.linkedNotebookGuid() != linkedNotebookGuid))
        {
            QNTRACE(
                "tests:synchronization",
                "Skipping resource as its note's notebook doesn't match by "
                    << "linked notebook criteria: "
                    << resource << "\nNote: " << note
                    << "\nNotebook: " << notebook);
            continue;
        }

        QNTRACE("tests:synchronization", "Processing resource: " << resource);

        auto guidIt = guidsOfCompleteSentItems.m_resourceGuids.find(
            resource.guid().value());

        if (guidIt == guidsOfCompleteSentItems.m_resourceGuids.end()) {
            QNTRACE(
                "tests:synchronization",
                "Encountered first resource not within the list of sent items");

            if ((smallestUsn < 0) ||
                (smallestUsn > resource.updateSequenceNum().value())) {
                smallestUsn = *resource.updateSequenceNum();
                QNTRACE(
                    "tests:synchronization",
                    "Updated smallest USN to " << smallestUsn);
            }

            break;
        }
    }

    QNDEBUG(
        "tests:synchronization",
        "Smallest USN of not completely sent data item is " << smallestUsn
            << " (linked notebook guid = " << linkedNotebookGuid << ")");
    return smallestUsn;
}

qint32 FakeNoteStore::maxUsnBeforeAPIRateLimitsExceeding(
    const QString & linkedNotebookGuid) const
{
    if (linkedNotebookGuid.isEmpty()) {
        return m_data->m_maxUsnForUserOwnDataBeforeRateLimitBreach;
    }

    auto it = m_data->m_maxUsnsForLinkedNotebooksDataBeforeRateLimitBreach.find(
        linkedNotebookGuid);

    if (it ==
        m_data->m_maxUsnsForLinkedNotebooksDataBeforeRateLimitBreach.end()) {
        return -1;
    }

    return it.value();
}

INoteStore * FakeNoteStore::create() const
{
    return new FakeNoteStore(m_data);
}

QString FakeNoteStore::noteStoreUrl() const
{
    return m_data->m_noteStoreUrl;
}

void FakeNoteStore::setNoteStoreUrl(QString noteStoreUrl)
{
    m_data->m_noteStoreUrl = std::move(noteStoreUrl);
}

void FakeNoteStore::setAuthData(
    QString authenticationToken, QList<QNetworkCookie> cookies)
{
    Q_UNUSED(cookies)
    m_data->m_authenticationToken = std::move(authenticationToken);
}

void FakeNoteStore::stop()
{
    for (auto timerId: qAsConst(m_data->m_getNoteAsyncDelayTimerIds)) {
        killTimer(timerId);
    }
    m_data->m_getNoteAsyncDelayTimerIds.clear();

    for (auto timerId: qAsConst(m_data->m_getResourceAsyncDelayTimerIds)) {
        killTimer(timerId);
    }
    m_data->m_getResourceAsyncDelayTimerIds.clear();
}

qint32 FakeNoteStore::createNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnCreateNotebookAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (m_data->m_notebooks.size() + 1 > m_data->m_maxNumNotebooks) {
        errorDescription.setBase("Already at max number of notebooks");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::LIMIT_REACHED);
    }

    qint32 checkRes = checkNotebookFields(notebook, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (notebook.linkedNotebookGuid()) {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(
            notebook.guid().value(), linkedNotebookAuthToken, errorDescription);

        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (
        !linkedNotebookAuthToken.isEmpty() &&
        (linkedNotebookAuthToken != m_data->m_authenticationToken))
    {
        errorDescription.setBase(
            "Notebook doesn't belong to a linked notebook but linked "
            "notebook auth token is not empty");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    if (!linkedNotebookAuthToken.isEmpty() && notebook.defaultNotebook() &&
        *notebook.defaultNotebook())
    {
        errorDescription.setBase(
            "Linked notebook cannot be set as default notebook");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    auto & nameIndex = m_data->m_notebooks.get<NotebookByNameUpper>();
    auto nameIt = nameIndex.find(notebook.name()->toUpper());
    if (nameIt != nameIndex.end()) {
        errorDescription.setBase(
            "Notebook with the specified name already exists");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    notebook.setGuid(UidGenerator::Generate());

    qint32 maxUsn = currentMaxUsn(
        notebook.linkedNotebookGuid() ? *notebook.linkedNotebookGuid()
                                      : QString{});

    ++maxUsn;
    notebook.setUpdateSequenceNum(maxUsn);

    notebook.setLocallyModified(false);

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        auto & guidsOfCompleteSentItems =
            (notebook.linkedNotebookGuid()
                 ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                       [*notebook.linkedNotebookGuid()]
                 : m_data->m_guidsOfUserOwnCompleteSentItems);

        Q_UNUSED(guidsOfCompleteSentItems.m_notebookGuids.insert(
            notebook.guid().value()))
    }

    Q_UNUSED(m_data->m_notebooks.insert(notebook))
    return 0;
}

qint32 FakeNoteStore::updateNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnUpdateNotebookAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (!notebook.guid()) {
        errorDescription.setBase("Notebook guid is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    qint32 checkRes = checkNotebookFields(notebook, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (notebook.linkedNotebookGuid()) {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(
            notebook.guid().value(), linkedNotebookAuthToken, errorDescription);

        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (
        !linkedNotebookAuthToken.isEmpty() &&
        (linkedNotebookAuthToken != m_data->m_authenticationToken))
    {
        errorDescription.setBase(
            "Notebook doesn't belong to a linked notebook but linked "
            "notebook auth token is not empty");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    if (!linkedNotebookAuthToken.isEmpty() && notebook.defaultNotebook() &&
        *notebook.defaultNotebook())
    {
        errorDescription.setBase(
            "Linked notebook cannot be set as default notebook");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    auto & index = m_data->m_notebooks.get<NotebookByGuid>();
    auto it = index.find(*notebook.guid());
    if (it == index.end()) {
        errorDescription.setBase(
            "Notebook with the specified guid doesn't exist");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    const auto & originalNotebook = *it;
    if (originalNotebook.restrictions() &&
        originalNotebook.restrictions()->noUpdateNotebook() &&
        *originalNotebook.restrictions()->noUpdateNotebook())
    {
        errorDescription.setBase("No permission to update the notebook");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    if (originalNotebook.name().value().toUpper() !=
        notebook.name()->toUpper())
    {
        auto & nameIndex = m_data->m_notebooks.get<NotebookByNameUpper>();
        auto nameIt = nameIndex.find(notebook.name()->toUpper());
        if (nameIt != nameIndex.end()) {
            errorDescription.setBase(
                "Notebook with the specified name already exists");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::DATA_CONFLICT);
        }
    }

    qint32 maxUsn = currentMaxUsn(
        notebook.linkedNotebookGuid() ? *notebook.linkedNotebookGuid()
                                      : QString{});

    ++maxUsn;
    notebook.setUpdateSequenceNum(maxUsn);

    notebook.setLocallyModified(false);

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        auto & guidsOfCompleteSentItems =
            (notebook.linkedNotebookGuid()
                 ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                       [*notebook.linkedNotebookGuid()]
                 : m_data->m_guidsOfUserOwnCompleteSentItems);

        Q_UNUSED(guidsOfCompleteSentItems.m_notebookGuids.insert(
            *notebook.guid()))
    }

    Q_UNUSED(index.replace(it, notebook))
    return 0;
}

qint32 FakeNoteStore::createNote(
    qevercloud::Note & note, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnCreateNoteAttempt) {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (m_data->m_notes.size() + 1 > m_data->m_maxNumNotes) {
        errorDescription.setBase("Already at max number of notes");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::LIMIT_REACHED);
    }

    qint32 checkRes = checkNoteFields(
        note, CheckNoteFieldsPurpose::CreateNote, errorDescription);

    if (checkRes != 0) {
        return checkRes;
    }

    const auto * pNotebook = findNotebook(*note.notebookGuid());
    if (Q_UNLIKELY(!pNotebook)) {
        errorDescription.setBase("No notebook was found for note");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    if (pNotebook->linkedNotebookGuid()) {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(
            *note.notebookGuid(), linkedNotebookAuthToken, errorDescription);

        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (
        !linkedNotebookAuthToken.isEmpty() &&
        (linkedNotebookAuthToken != m_data->m_authenticationToken))
    {
        errorDescription.setBase(
            "Note's notebook doesn't belong to a linked notebook but linked "
            "notebook auth token is not empty");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    note.setGuid(UidGenerator::Generate());

    qint32 maxUsn = currentMaxUsn(
        pNotebook->linkedNotebookGuid() ? *pNotebook->linkedNotebookGuid()
                                        : QString{});

    ++maxUsn;
    note.setUpdateSequenceNum(maxUsn);

    note.setLocallyModified(false);

    auto insertResult = m_data->m_notes.insert(note);

    if (note.resources() && !note.resources()->isEmpty()) {
        auto resources = *note.resources();
        for (auto & resource: resources) {
            if (!resource.guid()) {
                resource.setGuid(UidGenerator::Generate());
            }

            if (!resource.noteGuid()) {
                resource.setNoteGuid(note.guid());
            }

            if (resource.noteLocalId().isEmpty()) {
                resource.setNoteLocalId(note.localId());
            }

            // NOTE: not really sure it should behave this way but let's try:
            // setting each resource's USN to note's USN
            resource.setUpdateSequenceNum(note.updateSequenceNum().value());

            if (!setResource(resource, errorDescription)) {
                return static_cast<qint32>(
                    qevercloud::EDAMErrorCode::DATA_CONFLICT);
            }
        }

        auto originalResources = resources;
        for (auto & resource: resources) {
            // Won't store resource binary data along with notes
            if (resource.data()) {
                resource.mutableData()->setBody(std::nullopt);
            }

            if (resource.recognition()) {
                resource.mutableRecognition()->setBody(std::nullopt);
            }

            if (resource.alternateData()) {
                resource.mutableAlternateData()->setBody(std::nullopt);
            }
        }

        note.setResources(resources);
        Q_UNUSED(m_data->m_notes.replace(insertResult.first, note))

        // Restore the original resources with guids and USNs back to
        // the input-output note
        note.setResources(originalResources);

        QNTRACE(
            "tests:synchronization",
            "Note after FakeNoteStore::createNote: " << note);
    }

    return 0;
}

qint32 FakeNoteStore::updateNote(
    qevercloud::Note & note, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnUpdateNoteAttempt) {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (!note.guid()) {
        errorDescription.setBase("Note.guid");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    qint32 checkRes = checkNoteFields(
        note, CheckNoteFieldsPurpose::UpdateNote, errorDescription);

    if (checkRes != 0) {
        return checkRes;
    }

    auto & index = m_data->m_notes.get<NoteByGuid>();
    auto it = index.find(*note.guid());
    if (it == index.end()) {
        errorDescription.setBase("Note with the specified guid doesn't exist");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    const auto * pNotebook = findNotebook(*note.notebookGuid());
    if (Q_UNLIKELY(!pNotebook)) {
        errorDescription.setBase("No notebook was found for note");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    if (pNotebook->linkedNotebookGuid()) {
        checkRes = checkLinkedNotebookAuthTokenForNotebook(
            *note.notebookGuid(), linkedNotebookAuthToken, errorDescription);

        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (
        !linkedNotebookAuthToken.isEmpty() &&
        (linkedNotebookAuthToken != m_data->m_authenticationToken))
    {
        errorDescription.setBase(
            "Note's notebook doesn't belong to a linked notebook but linked "
            "notebook auth token is not empty");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    qint32 maxUsn = currentMaxUsn(
        pNotebook->linkedNotebookGuid() ? *pNotebook->linkedNotebookGuid()
                                        : QString{});

    ++maxUsn;
    note.setUpdateSequenceNum(maxUsn);

    note.setLocallyModified(false);

    Q_UNUSED(index.replace(it, note))

    if (note.resources() && !note.resources()->isEmpty()) {
        auto resources = *note.resources();
        for (auto & resource: resources) {
            if (!resource.guid()) {
                resource.setGuid(UidGenerator::Generate());
            }

            if (!resource.noteGuid()) {
                resource.setNoteGuid(note.guid());
            }

            if (resource.noteLocalId().isEmpty()) {
                resource.setNoteLocalId(note.localId());
            }

            if (!resource.updateSequenceNum()) {
                // NOTE: not really sure it should behave this way but let's
                // try: setting each resource's USN to note's USN
                resource.setUpdateSequenceNum(note.updateSequenceNum().value());
            }

            if (!setResource(resource, errorDescription)) {
                return static_cast<qint32>(
                    qevercloud::EDAMErrorCode::DATA_CONFLICT);
            }
        }

        auto originalResources = resources;
        for (auto & resource: resources) {
            // Won't store resource binary data along with notes
            if (resource.data()) {
                resource.mutableData()->setBody(std::nullopt);
            }

            if (resource.recognition()) {
                resource.mutableRecognition()->setBody(std::nullopt);
            }

            if (resource.alternateData()) {
                resource.mutableAlternateData()->setBody(std::nullopt);
            }
        }

        note.setResources(resources);
        Q_UNUSED(index.replace(it, note))

        // Restore the original resources with guids and USNs back to
        // the input-output note
        note.setResources(originalResources);

        QNTRACE(
            "tests:synchronization",
            "Note after FakeNoteStore::updateNote: " << note);
    }

    return 0;
}

qint32 FakeNoteStore::createTag(
    qevercloud::Tag & tag, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnCreateTagAttempt) {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (m_data->m_tags.size() + 1 > m_data->m_maxNumTags) {
        errorDescription.setBase("Already at max number of tags");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::LIMIT_REACHED);
    }

    qint32 checkRes = checkTagFields(tag, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (tag.linkedNotebookGuid()) {
        checkRes = checkLinkedNotebookAuthTokenForTag(
            tag, linkedNotebookAuthToken, errorDescription);

        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (
        !linkedNotebookAuthToken.isEmpty() &&
        (linkedNotebookAuthToken != m_data->m_authenticationToken))
    {
        errorDescription.setBase(
            "Tag doesn't belong to a linked notebook but linked notebook "
            "auth token is not empty");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    auto & nameIndex = m_data->m_tags.get<TagByNameUpper>();
    auto it = nameIndex.find(tag.name()->toUpper());
    if (it != nameIndex.end()) {
        errorDescription.setBase("Tag name is already in use");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    tag.setGuid(UidGenerator::Generate());

    qint32 maxUsn = currentMaxUsn(
        tag.linkedNotebookGuid() ? *tag.linkedNotebookGuid() : QString{});

    ++maxUsn;
    tag.setUpdateSequenceNum(maxUsn);

    tag.setLocallyModified(false);

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        auto & guidsOfCompleteSentItems =
            (tag.linkedNotebookGuid()
                 ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                       [*tag.linkedNotebookGuid()]
                 : m_data->m_guidsOfUserOwnCompleteSentItems);
        Q_UNUSED(guidsOfCompleteSentItems.m_tagGuids.insert(*tag.guid()))
    }

    Q_UNUSED(m_data->m_tags.insert(tag))

    return 0;
}

qint32 FakeNoteStore::updateTag(
    qevercloud::Tag & tag, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnUpdateTagAttempt) {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (!tag.guid()) {
        errorDescription.setBase("Tag guid is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    qint32 checkRes = checkTagFields(tag, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    if (tag.linkedNotebookGuid()) {
        checkRes = checkLinkedNotebookAuthTokenForTag(
            tag, linkedNotebookAuthToken, errorDescription);

        if (checkRes != 0) {
            return checkRes;
        }
    }
    else if (
        !linkedNotebookAuthToken.isEmpty() &&
        (linkedNotebookAuthToken != m_data->m_authenticationToken))
    {
        errorDescription.setBase(
            "Tag doesn't belong to a linked notebook but linked notebook "
            "auth token is not empty");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    auto & index = m_data->m_tags.get<TagByGuid>();
    auto it = index.find(*tag.guid());
    if (it == index.end()) {
        errorDescription.setBase("Tag with the specified guid doesn't exist");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    const auto & originalTag = *it;
    if (originalTag.name().value().toUpper() != tag.name()->toUpper()) {
        auto & nameIndex = m_data->m_tags.get<TagByNameUpper>();
        auto nameIt = nameIndex.find(tag.name()->toUpper());
        if (nameIt != nameIndex.end()) {
            errorDescription.setBase(
                "Tag with the specified name already exists");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::DATA_CONFLICT);
        }
    }

    qint32 maxUsn = currentMaxUsn(
        tag.linkedNotebookGuid() ? *tag.linkedNotebookGuid() : QString{});

    ++maxUsn;
    tag.setUpdateSequenceNum(maxUsn);

    tag.setLocallyModified(false);

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        auto & guidsOfCompleteSentItems =
            (tag.linkedNotebookGuid()
                 ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                       [*tag.linkedNotebookGuid()]
                 : m_data->m_guidsOfUserOwnCompleteSentItems);

        Q_UNUSED(guidsOfCompleteSentItems.m_tagGuids.insert(*tag.guid()))
    }

    Q_UNUSED(index.replace(it, tag))
    return 0;
}

qint32 FakeNoteStore::createSavedSearch(
    qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnCreateSavedSearchAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (m_data->m_savedSearches.size() + 1 > m_data->m_maxNumSavedSearches) {
        errorDescription.setBase("Already at max number of saved searches");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::LIMIT_REACHED);
    }

    qint32 checkRes = checkSavedSearchFields(savedSearch, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    auto & nameIndex = m_data->m_savedSearches.get<SavedSearchByNameUpper>();
    auto it = nameIndex.find(savedSearch.name()->toUpper());
    if (it != nameIndex.end()) {
        errorDescription.setBase("Saved search name is already in use");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    savedSearch.setGuid(UidGenerator::Generate());

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    savedSearch.setUpdateSequenceNum(maxUsn);

    savedSearch.setLocallyModified(false);

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        auto & savedSearchGuids =
            m_data->m_guidsOfUserOwnCompleteSentItems.m_savedSearchGuids;

        Q_UNUSED(savedSearchGuids.insert(*savedSearch.guid()))
    }

    Q_UNUSED(m_data->m_savedSearches.insert(savedSearch))
    return 0;
}

qint32 FakeNoteStore::updateSavedSearch(
    qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnUpdateSavedSearchAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (!savedSearch.guid()) {
        errorDescription.setBase("Saved search guid is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    auto checkRes = checkSavedSearchFields(savedSearch, errorDescription);
    if (checkRes != 0) {
        return checkRes;
    }

    auto & index = m_data->m_savedSearches.get<SavedSearchByGuid>();
    auto it = index.find(*savedSearch.guid());
    if (it == index.end()) {
        errorDescription.setBase(
            "Saved search with the specified guid doesn't exist");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    const auto & originalSavedSearch = *it;
    if (originalSavedSearch.name().value().toUpper() !=
        savedSearch.name()->toUpper())
    {
        const auto & nameIndex =
            m_data->m_savedSearches.get<SavedSearchByNameUpper>();

        auto nameIt = nameIndex.find(savedSearch.name()->toUpper());
        if (nameIt != nameIndex.end()) {
            errorDescription.setBase(
                "Saved search with the specified name already exists");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::DATA_CONFLICT);
        }
    }

    qint32 maxUsn = currentMaxUsn();
    ++maxUsn;
    savedSearch.setUpdateSequenceNum(maxUsn);

    savedSearch.setLocallyModified(false);

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        auto & savedSearchGuids =
            m_data->m_guidsOfUserOwnCompleteSentItems.m_savedSearchGuids;

        Q_UNUSED(savedSearchGuids.insert(*savedSearch.guid()))
    }

    Q_UNUSED(index.replace(it, savedSearch))
    return 0;
}

qint32 FakeNoteStore::getSyncState(
    qevercloud::SyncState & syncState, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnGetUserOwnSyncStateAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    syncState = m_data->m_syncState;
    Q_UNUSED(errorDescription)
    return 0;
}

qint32 FakeNoteStore::getSyncChunk(
    const qint32 afterUSN, const qint32 maxEntries,
    const qevercloud::SyncChunkFilter & filter,
    qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    QNDEBUG(
        "tests:synchronization",
        "FakeNoteStore::getSyncChunk: after USN = "
            << afterUSN << ", max entries = " << maxEntries
            << ", filter = " << filter);

    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnGetUserOwnSyncChunkAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    return getSyncChunkImpl(
        afterUSN, maxEntries, (afterUSN == 0), QString(), filter, syncChunk,
        errorDescription);
}

qint32 FakeNoteStore::getLinkedNotebookSyncState(
    const qevercloud::LinkedNotebook & linkedNotebook,
    const QString & authToken, qevercloud::SyncState & syncState,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnGetLinkedNotebookSyncStateAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    if (m_data->m_authenticationToken != authToken) {
        errorDescription.setBase("Wrong authentication token");
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    qint32 checkRes =
        checkLinkedNotebookFields(linkedNotebook, errorDescription);

    if (checkRes != 0) {
        return checkRes;
    }

    auto it =
        m_data->m_linkedNotebookSyncStates.find(*linkedNotebook.username());

    if (it == m_data->m_linkedNotebookSyncStates.end()) {
        QNWARNING(
            "tests:synchronization",
            "Failed to find linked notebook sync state for linked notebook: "
                << linkedNotebook << "\nLinked notebook sync states: "
                << m_data->m_linkedNotebookSyncStates);

        errorDescription.setBase(
            "Found no sync state for the given linked notebook owner");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    syncState = *it;
    return 0;
}

qint32 FakeNoteStore::getLinkedNotebookSyncChunk(
    const qevercloud::LinkedNotebook & linkedNotebook, const qint32 afterUSN,
    const qint32 maxEntries, const QString & linkedNotebookAuthToken,
    const bool fullSyncOnly, qevercloud::SyncChunk & syncChunk,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    QNDEBUG(
        "tests:synchronization",
        "FakeNoteStore::getLinkedNotebookSyncChunk: linked notebook = "
            << linkedNotebook << "\nAfter USN = " << afterUSN
            << ", max entries = " << maxEntries
            << ", linked notebook auth token = " << linkedNotebookAuthToken
            << ", full sync only = " << (fullSyncOnly ? "true" : "false"));

    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnGetLinkedNotebookSyncChunkAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    m_data->m_onceGetLinkedNotebookSyncChunkCalled = true;

    qint32 checkRes =
        checkLinkedNotebookFields(linkedNotebook, errorDescription);

    if (checkRes != 0) {
        return checkRes;
    }

    const auto & linkedNotebookUsernameIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByUsername>();

    auto linkedNotebookIt =
        linkedNotebookUsernameIndex.find(*linkedNotebook.username());

    if (linkedNotebookIt == linkedNotebookUsernameIndex.end()) {
        errorDescription.setBase(
            "Found no existing linked notebook by username");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    if (linkedNotebookAuthToken != m_data->m_authenticationToken) {
        errorDescription.setBase("Wrong authentication token");
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    qevercloud::SyncChunkFilter filter;
    filter.setIncludeTags(true);
    filter.setIncludeNotebooks(true);
    filter.setIncludeNotes(true);
    filter.setIncludeNoteResources(true);
    filter.setIncludeNoteAttributes(true);
    filter.setIncludeNoteApplicationDataFullMap(true);
    filter.setIncludeNoteResourceApplicationDataFullMap(true);

    if (!fullSyncOnly && (afterUSN != 0)) {
        filter.setIncludeResources(true);
        filter.setIncludeResourceApplicationDataFullMap(true);
    }

    return getSyncChunkImpl(
        afterUSN, maxEntries, fullSyncOnly, *linkedNotebook.guid(), filter,
        syncChunk, errorDescription);
}

qint32 FakeNoteStore::getNote(
    const bool withContent, const bool withResourcesData,
    const bool withResourcesRecognition, const bool withResourcesAlternateData,
    qevercloud::Note & note, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    if (m_data->m_onceGetLinkedNotebookSyncChunkCalled) {
        if (m_data->m_APIRateLimitsTrigger ==
            APIRateLimitsTrigger::
                OnGetNoteAttemptAfterDownloadingLinkedNotebookSyncChunks)
        {
            m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
            rateLimitSeconds = 0;
            storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
            m_data->m_onceAPIRateLimitExceedingTriggered = true;

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
        }
    }
    else {
        if (m_data->m_APIRateLimitsTrigger ==
            APIRateLimitsTrigger::
                OnGetNoteAttemptAfterDownloadingUserOwnSyncChunks)
        {
            m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
            rateLimitSeconds = 0;
            storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
            m_data->m_onceAPIRateLimitExceedingTriggered = true;

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
        }
    }

    if (!note.guid()) {
        errorDescription.setBase("Note has no guid");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    const auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(note.guid().value());
    if (noteIt == noteGuidIndex.end()) {
        errorDescription.setBase("Note was not found");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    note = *noteIt;

    const auto * pNotebook = findNotebook(*note.notebookGuid());
    if (Q_UNLIKELY(!pNotebook)) {
        errorDescription.setBase("No notebook was found for note");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    if (!withContent) {
        note.setContent(QString());
    }

    if (note.resources() && !note.resources()->isEmpty()) {
        const auto & resourceGuidIndex =
            m_data->m_resources.get<ResourceByGuid>();

        auto resources = *note.resources();
        for (auto it = resources.begin(); it != resources.end();) {
            auto & resource = *it;
            if (Q_UNLIKELY(!resource.guid())) {
                it = resources.erase(it);
                continue;
            }

            auto resourceIt = resourceGuidIndex.find(*resource.guid());
            if (Q_UNLIKELY(resourceIt == resourceGuidIndex.end())) {
                it = resources.erase(it);
                continue;
            }

            resource = *resourceIt;

            if (!withResourcesData && resource.data()) {
                resource.mutableData()->setBody(std::nullopt);
            }

            if (!withResourcesRecognition && resource.recognition()) {
                resource.mutableRecognition()->setBody(std::nullopt);
            }

            if (!withResourcesAlternateData && resource.alternateData()) {
                resource.mutableAlternateData()->setBody(std::nullopt);
            }

            ++it;
        }

        note.setResources(resources);
    }

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        GuidsOfCompleteSentItems & guidsOfCompleteSentItems =
            (pNotebook->linkedNotebookGuid()
                 ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                       [*pNotebook->linkedNotebookGuid()]
                 : m_data->m_guidsOfUserOwnCompleteSentItems);

        Q_UNUSED(guidsOfCompleteSentItems.m_noteGuids.insert(*note.guid()))

        if (note.resources() && !note.resources()->isEmpty()) {
            auto resources = *note.resources();
            for (auto it = resources.constBegin(), end = resources.constEnd();
                 it != end; ++it)
            {
                Q_UNUSED(guidsOfCompleteSentItems.m_resourceGuids.insert(
                    it->guid().value()))

                QNTRACE(
                    "tests:synchronization",
                    "Marked resource as processed: " << *it);
            }
        }
    }

    return 0;
}

bool FakeNoteStore::getNoteAsync(
    const bool withContent, const bool withResourcesData,
    const bool withResourcesRecognition, const bool withResourcesAlternateData,
    const bool withSharedNotes, const bool withNoteAppDataValues,
    const bool withResourceAppDataValues, const bool withNoteLimits,
    const QString & noteGuid, const QString & authToken,
    ErrorString & errorDescription)
{
    if (Q_UNLIKELY(noteGuid.isEmpty())) {
        errorDescription.setBase("Note guid is empty");
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

    m_data->m_getNoteAsyncRequests.enqueue(request);

    int timerId = startTimer(0);

    QNDEBUG(
        "tests:synchronization",
        "Started timer to postpone the get note "
            << "result, timer id = " << timerId);

    Q_UNUSED(m_data->m_getNoteAsyncDelayTimerIds.insert(timerId))
    return true;
}

qint32 FakeNoteStore::getResource(
    const bool withDataBody, const bool withRecognitionDataBody,
    const bool withAlternateDataBody, const bool withAttributes,
    const QString & authToken, qevercloud::Resource & resource,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    if (m_data->m_onceGetLinkedNotebookSyncChunkCalled) {
        if (m_data->m_APIRateLimitsTrigger ==
            APIRateLimitsTrigger::
                OnGetResourceAttemptAfterDownloadingLinkedNotebookSyncChunks)
        {
            m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
            rateLimitSeconds = 0;
            storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
            m_data->m_onceAPIRateLimitExceedingTriggered = true;

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
        }
    }
    else {
        if (m_data->m_APIRateLimitsTrigger ==
            APIRateLimitsTrigger::
                OnGetResourceAttemptAfterDownloadingUserOwnSyncChunks)
        {
            m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
            rateLimitSeconds = 0;
            storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
            m_data->m_onceAPIRateLimitExceedingTriggered = true;

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
        }
    }

    if (!resource.guid()) {
        errorDescription.setBase("Resource has no guid");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    const auto & resourceGuidIndex = m_data->m_resources.get<ResourceByGuid>();

    auto resourceIt = resourceGuidIndex.find(*resource.guid());
    if (resourceIt == resourceGuidIndex.end()) {
        errorDescription.setBase("Resource was not found");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    if (Q_UNLIKELY(!resourceIt->noteGuid())) {
        errorDescription.setBase("Found resource has no note guid");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::INTERNAL_ERROR);
    }

    const QString & noteGuid = *resourceIt->noteGuid();
    const auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    auto noteIt = noteGuidIndex.find(noteGuid);
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        errorDescription.setBase("Found no note containing the resource");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::INTERNAL_ERROR);
    }

    if (Q_UNLIKELY(!noteIt->notebookGuid())) {
        errorDescription.setBase("Found note has no notebook guid");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::INTERNAL_ERROR);
    }

    const QString & notebookGuid = *noteIt->notebookGuid();
    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(notebookGuid);
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        errorDescription.setBase(
            "Found no notebook containing the note with the resource");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INTERNAL_ERROR);
    }

    if (notebookIt->linkedNotebookGuid()) {
        qint32 checkRes = checkLinkedNotebookAuthTokenForNotebook(
            notebookGuid, authToken, errorDescription);

        if (checkRes != 0) {
            return checkRes;
        }
    }

    resource = *resourceIt;

    if (!withDataBody && resource.data()) {
        resource.mutableData()->setBody(std::nullopt);
    }

    if (!withRecognitionDataBody && resource.recognition()) {
        resource.mutableRecognition()->setBody(std::nullopt);
    }

    if (!withAlternateDataBody && resource.alternateData()) {
        resource.mutableAlternateData()->setBody(std::nullopt);
    }

    if (!withAttributes && resource.attributes()) {
        resource.setAttributes(std::nullopt);
    }

    if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
        GuidsOfCompleteSentItems & guidsOfCompleteSentItems =
            (notebookIt->linkedNotebookGuid()
                 ? m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                       [*notebookIt->linkedNotebookGuid()]
                 : m_data->m_guidsOfUserOwnCompleteSentItems);

        Q_UNUSED(
            guidsOfCompleteSentItems.m_resourceGuids.insert(*resource.guid()))

        QNTRACE(
            "tests:synchronization",
            "Marked resource as processed: " << resource);
    }

    return 0;
}

bool FakeNoteStore::getResourceAsync(
    const bool withDataBody, const bool withRecognitionDataBody,
    const bool withAlternateDataBody, const bool withAttributes,
    const QString & resourceGuid, const QString & authToken,
    ErrorString & errorDescription)
{
    if (Q_UNLIKELY(resourceGuid.isEmpty())) {
        errorDescription.setBase("Resource guid is empty");
        return false;
    }

    GetResourceAsyncRequest request;
    request.m_withDataBody = withDataBody;
    request.m_withRecognitionDataBody = withRecognitionDataBody;
    request.m_withAlternateDataBody = withAlternateDataBody;
    request.m_withAttributes = withAttributes;
    request.m_resourceGuid = resourceGuid;

    request.m_authToken = authToken;

    m_data->m_getResourceAsyncRequests.enqueue(request);
    int timerId = startTimer(0);

    QNDEBUG(
        "tests:synchronization",
        "Started timer to postpone the get resource result, timer id = "
            << timerId);

    Q_UNUSED(m_data->m_getResourceAsyncDelayTimerIds.insert(timerId))
    return true;
}

qint32 FakeNoteStore::authenticateToSharedNotebook(
    const QString & shareKey, qevercloud::AuthenticationResult & authResult,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    if (m_data->m_APIRateLimitsTrigger ==
        APIRateLimitsTrigger::OnAuthenticateToSharedNotebookAttempt)
    {
        m_data->m_APIRateLimitsTrigger = APIRateLimitsTrigger::Never;
        rateLimitSeconds = 0;
        storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach();
        m_data->m_onceAPIRateLimitExceedingTriggered = true;
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED);
    }

    const auto & index =
        m_data->m_linkedNotebooks.get<LinkedNotebookBySharedNotebookGlobalId>();

    auto it = index.find(shareKey);
    if (it == index.end()) {
        errorDescription.setBase(
            "Found no linked notebook corresponding to share key");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    const auto & linkedNotebook = *it;

    auto authTokenIt =
        m_data->m_linkedNotebookAuthTokens.find(*linkedNotebook.username());

    if (authTokenIt == m_data->m_linkedNotebookAuthTokens.end()) {
        errorDescription.setBase("No valid authentication token was provided");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::INVALID_AUTH);
    }

    authResult.setAuthenticationToken(authTokenIt.value());
    authResult.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

    authResult.setExpiration(
        QDateTime::currentDateTime().addYears(1).toMSecsSinceEpoch());

    authResult.setNoteStoreUrl(QStringLiteral("Fake note store URL"));
    authResult.setWebApiUrlPrefix(QStringLiteral("Fake web API url prefix"));
    return 0;
}

void FakeNoteStore::timerEvent(QTimerEvent * pEvent)
{
    if (Q_UNLIKELY(!pEvent)) {
        return;
    }

    auto noteIt = m_data->m_getNoteAsyncDelayTimerIds.find(pEvent->timerId());
    if (noteIt != m_data->m_getNoteAsyncDelayTimerIds.end()) {
        QNDEBUG(
            "tests:synchronization",
            "getNoteAsync delay timer event, "
                << "timer id = " << pEvent->timerId());

        Q_UNUSED(m_data->m_getNoteAsyncDelayTimerIds.erase(noteIt))
        killTimer(pEvent->timerId());

        if (!m_data->m_getNoteAsyncRequests.isEmpty()) {
            auto request = m_data->m_getNoteAsyncRequests.dequeue();

            qint32 rateLimitSeconds = 0;
            ErrorString errorDescription;

            qevercloud::Note note;
            note.setGuid(request.m_noteGuid);

            qint32 res = getNote(
                request.m_withContent, request.m_withResourcesData,
                request.m_withResourcesRecognition,
                request.m_withResourcesAlternateData, note, errorDescription,
                rateLimitSeconds);

            if ((res == 0) && note.notebookGuid()) {
                const auto * pNotebook = findNotebook(*note.notebookGuid());
                if (Q_UNLIKELY(!pNotebook)) {
                    errorDescription.setBase("No notebook was found for note");
                    res = static_cast<qint32>(
                        qevercloud::EDAMErrorCode::DATA_CONFLICT);
                }
                else if (pNotebook->linkedNotebookGuid()) {
                    res = checkLinkedNotebookAuthTokenForNotebook(
                        *note.notebookGuid(), request.m_authToken,
                        errorDescription);
                }
                else if (
                    !request.m_authToken.isEmpty() &&
                    (request.m_authToken != m_data->m_authenticationToken))
                {
                    errorDescription.setBase(
                        "Note's notebook doesn't belong to a linked notebook "
                        "but linked notebook auth token is not empty");

                    res = static_cast<qint32>(
                        qevercloud::EDAMErrorCode::INVALID_AUTH);
                }
            }

            Q_EMIT getNoteAsyncFinished(
                res, note, rateLimitSeconds, errorDescription);
        }
        else {
            QNWARNING(
                "tests:synchronization",
                "Get note async requests queue is empty");
        }

        return;
    }

    auto resourceIt =
        m_data->m_getResourceAsyncDelayTimerIds.find(pEvent->timerId());

    if (resourceIt != m_data->m_getResourceAsyncDelayTimerIds.end()) {
        QNDEBUG(
            "tests:synchronization",
            "getResourceAsync delay timer event, timer id = "
                << pEvent->timerId());

        Q_UNUSED(m_data->m_getResourceAsyncDelayTimerIds.erase(resourceIt))
        killTimer(pEvent->timerId());

        if (!m_data->m_getResourceAsyncRequests.isEmpty()) {
            auto request = m_data->m_getResourceAsyncRequests.dequeue();

            qint32 rateLimitSeconds = 0;
            ErrorString errorDescription;

            qevercloud::Resource resource;
            resource.setGuid(request.m_resourceGuid);

            qint32 res = getResource(
                request.m_withDataBody, request.m_withRecognitionDataBody,
                request.m_withAlternateDataBody, request.m_withAttributes,
                request.m_authToken, resource, errorDescription,
                rateLimitSeconds);

            Q_EMIT getResourceAsyncFinished(
                res, resource, rateLimitSeconds, errorDescription);
        }
        else {
            QNWARNING(
                "tests:synchronization",
                "Get resource async requests queue is empty");
        }

        return;
    }

    INoteStore::timerEvent(pEvent);
}

qint32 FakeNoteStore::checkNotebookFields(
    const qevercloud::Notebook & notebook, ErrorString & errorDescription) const
{
    if (!notebook.name()) {
        errorDescription.setBase("Notebook name is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    const QString & notebookName = *notebook.name();
    if (notebookName.size() < qevercloud::EDAM_NOTEBOOK_NAME_LEN_MIN) {
        errorDescription.setBase("Notebook name length is too small");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (notebookName.size() > qevercloud::EDAM_NOTEBOOK_NAME_LEN_MAX) {
        errorDescription.setBase("Notebook name length is too large");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (notebookName != notebookName.trimmed()) {
        errorDescription.setBase(
            "Notebook name cannot begin or end with whitespace");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    // NOTE: tried to use qevercloud::EDAM_NOTEBOOK_NAME_REGEX to check the name
    // correctness but it appears that regex is too perlish for QRegExp to
    // handle i.e. QRegExp doesn't recognize the regexp in its full entirety and
    // hence doesn't match the text which should actually match

    if (notebook.stack()) {
        const QString & notebookStack = *notebook.stack();

        if (notebookStack.size() < qevercloud::EDAM_NOTEBOOK_STACK_LEN_MIN) {
            errorDescription.setBase("Notebook stack's length is too small");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        if (notebookStack.size() > qevercloud::EDAM_NOTEBOOK_STACK_LEN_MAX) {
            errorDescription.setBase("Notebook stack's length is too large");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        if (notebookStack != notebookStack.trimmed()) {
            errorDescription.setBase(
                "Notebook stack should not begin or end with whitespace");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        // NOTE: tried to use qevercloud::EDAM_NOTEBOOK_STACK_REGEX to check
        // the name correctness but it appears that regex is too perlish for
        // QRegExp to handle i.e. QRegExp doesn't recognize the regexp in its
        // full entirety and hence doesn't match the text which should actually
        // match
    }

    if (notebook.published() && notebook.publishing()->uri()) {
        const QString & publishingUri = *notebook.publishing()->uri();

        if (publishingUri.size() < qevercloud::EDAM_PUBLISHING_URI_LEN_MIN) {
            errorDescription.setBase(
                "Notebook publishing uri length is too small");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        if (publishingUri.size() > qevercloud::EDAM_PUBLISHING_URI_LEN_MAX) {
            errorDescription.setBase(
                "Notebook publising uri length is too large");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        for (auto it = qevercloud::EDAM_PUBLISHING_URI_PROHIBITED.begin(),
                  end = qevercloud::EDAM_PUBLISHING_URI_PROHIBITED.end();
             it != end; ++it)
        {
            if (publishingUri == *it) {
                errorDescription.setBase(
                    "Prohibited publishing URI value is set");

                return static_cast<qint32>(
                    qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
            }
        }

        QRegExp publishingUriRegExp(qevercloud::EDAM_PUBLISHING_URI_REGEX);
        if (!publishingUriRegExp.exactMatch(publishingUri)) {
            errorDescription.setBase(
                "Publishing URI doesn't match the mandatory regex");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }
    }

    if (notebook.publishing() && notebook.publishing()->publicDescription()) {
        const QString & description =
            *notebook.publishing()->publicDescription();

        if (description.size() <
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MIN) {
            errorDescription.setBase(
                "Publishing description length is too small");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        if (description.size() >
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MAX) {
            errorDescription.setBase(
                "Publishing description length is too large");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        QRegExp publishingDescriptionRegExp(
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_REGEX);

        if (!publishingDescriptionRegExp.exactMatch(description)) {
            errorDescription.setBase(
                "Notebook description doesn't match the mandatory regex");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkNoteFields(
    const qevercloud::Note & note, const CheckNoteFieldsPurpose purpose,
    ErrorString & errorDescription) const
{
    if (!note.notebookGuid()) {
        errorDescription.setBase("Note has no notebook guid set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    const auto & notebookIndex = m_data->m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookIndex.find(*note.notebookGuid());
    if (notebookIt == notebookIndex.end()) {
        errorDescription.setBase("Note.notebookGuid");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    const auto & notebook = *notebookIt;
    if (purpose == CheckNoteFieldsPurpose::CreateNote) {
        if (notebook.restrictions() &&
            notebook.restrictions()->noCreateNotes() &&
            *notebook.restrictions()->noCreateNotes())
        {
            errorDescription.setBase(
                "No permission to create notes within this notebook");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::PERMISSION_DENIED);
        }
    }
    else if (purpose == CheckNoteFieldsPurpose::UpdateNote) {
        if (notebook.restrictions() &&
            notebook.restrictions()->noUpdateNotes() &&
            *notebook.restrictions()->noUpdateNotes())
        {
            errorDescription.setBase(
                "No permission to update notes within this notebook");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::PERMISSION_DENIED);
        }
    }

    if (note.title()) {
        const QString & title = *note.title();

        if (title.size() < qevercloud::EDAM_NOTE_TITLE_LEN_MIN) {
            errorDescription.setBase("Note title length is too small");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        if (title.size() > qevercloud::EDAM_NOTE_TITLE_LEN_MAX) {
            errorDescription.setBase("Note title length is too large");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        // NOTE: tried to use qevercloud::EDAM_NOTE_TITLE_REGEX to check
        // the name correctness but it appears that regex is too perlish for
        // QRegExp to handle i.e. QRegExp doesn't recognize the regexp in its
        // full entirety and hence doesn't match the text which should actually
        // match
    }

    if (note.content()) {
        const QString & content = *note.content();

        if (content.size() < qevercloud::EDAM_NOTE_CONTENT_LEN_MIN) {
            errorDescription.setBase("Note content length is too small");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        if (content.size() > qevercloud::EDAM_NOTE_CONTENT_LEN_MAX) {
            errorDescription.setBase("Note content length is too large");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }
    }

    if (note.resources() && !note.resources()->isEmpty()) {
        for (auto it = note.resources()->constBegin(),
             end = note.resources()->constEnd();
             it != end; ++it)
        {
            const auto & resource = *it;
            qint32 checkRes = checkResourceFields(resource, errorDescription);
            if (checkRes != 0) {
                return checkRes;
            }
        }
    }

    if (note.attributes()) {
        const auto & attributes = *note.attributes();

#define CHECK_STRING_ATTRIBUTE(attr)                                           \
    if (attributes.attr()) {                                                   \
        if (attributes.attr()->size() < qevercloud::EDAM_ATTRIBUTE_LEN_MIN)   \
        {                                                                      \
            errorDescription.setBase(                                          \
                QStringLiteral("Attribute length is too small: ") +            \
                QString::fromUtf8(#attr));                                     \
            return static_cast<qint32>(                                        \
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);                   \
        }                                                                      \
                                                                               \
        if (attributes.attr()->size() < qevercloud::EDAM_ATTRIBUTE_LEN_MAX)    \
        {                                                                      \
            errorDescription.setBase(                                          \
                QStringLiteral("Attribute length is too large: ") +            \
                QString::fromUtf8(#attr));                                     \
            return static_cast<qint32>(                                        \
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);                   \
        }                                                                      \
    }

        CHECK_STRING_ATTRIBUTE(author)
        CHECK_STRING_ATTRIBUTE(source)
        CHECK_STRING_ATTRIBUTE(sourceURL)
        CHECK_STRING_ATTRIBUTE(sourceApplication)
        CHECK_STRING_ATTRIBUTE(placeName)
        CHECK_STRING_ATTRIBUTE(contentClass)

        if (attributes.applicationData()) {
            qint32 res = checkAppData(
                *attributes.applicationData(), errorDescription);

            if (res != 0) {
                return res;
            }
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkResourceFields(
    const qevercloud::Resource & resource, ErrorString & errorDescription) const
{
    if (resource.mime()) {
        const QString & mime = *resource.mime();

        if (mime.size() < qevercloud::EDAM_MIME_LEN_MIN) {
            errorDescription.setBase(
                "Note's resource mime type length is too small");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        if (mime.size() > qevercloud::EDAM_MIME_LEN_MAX) {
            errorDescription.setBase(
                "Note's resource mime type length is too large");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }

        QRegExp mimeRegExp(qevercloud::EDAM_MIME_REGEX);
        if (!mimeRegExp.exactMatch(mime)) {
            errorDescription.setBase(
                "Note's resource mime type doesn't match the mandatory regex");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
        }
    }

    if (resource.attributes()) {
        const auto & attributes = *resource.attributes();

        CHECK_STRING_ATTRIBUTE(sourceURL)
        CHECK_STRING_ATTRIBUTE(cameraMake)
        CHECK_STRING_ATTRIBUTE(cameraModel)

        if (attributes.applicationData()) {
            qint32 res = checkAppData(
                *attributes.applicationData(), errorDescription);

            if (res != 0) {
                return res;
            }
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkTagFields(
    const qevercloud::Tag & tag, ErrorString & errorDescription) const
{
    if (!tag.name()) {
        errorDescription.setBase("Tag name is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    const QString & tagName = *tag.name();
    if (tagName.size() < qevercloud::EDAM_TAG_NAME_LEN_MIN) {
        errorDescription.setBase("Tag name length is too small");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (tagName.size() > qevercloud::EDAM_TAG_NAME_LEN_MAX) {
        errorDescription.setBase("Tag name length is too large");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    // NOTE: tried to use qevercloud::EDAM_TAG_NAME_REGEX to check the name
    // correctness but it appears that regex is too perlish for QRegExp to
    // handle i.e. QRegExp doesn't recognize the regexp in its full entirety
    // and hence doesn't match the text which should actually match

    if (tagName != tagName.trimmed()) {
        errorDescription.setBase(
            "Tag name shouldn't start or end with whitespace");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (tag.parentGuid()) {
        const TagDataByGuid & index = m_data->m_tags.get<TagByGuid>();
        auto it = index.find(*tag.parentGuid());
        if (it == index.end()) {
            errorDescription.setBase("Parent tag doesn't exist");
            return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkSavedSearchFields(
    const qevercloud::SavedSearch & savedSearch,
    ErrorString & errorDescription) const
{
    if (!savedSearch.name()) {
        errorDescription.setBase("Saved search name is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (!savedSearch.query()) {
        errorDescription.setBase("Saved search query is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    const QString & savedSearchName = *savedSearch.name();
    if (savedSearchName.size() < qevercloud::EDAM_SAVED_SEARCH_NAME_LEN_MIN) {
        errorDescription.setBase("Saved search name length is too small");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (savedSearchName.size() > qevercloud::EDAM_SAVED_SEARCH_NAME_LEN_MAX) {
        errorDescription.setBase("Saved search name length is too large");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    // NOTE: tried to use qevercloud::EDAM_SAVED_SEARCH_NAME_REGEX to check
    // the name correctness but it appears that regex is too perlish for QRegExp
    // to handle i.e. QRegExp doesn't recognize the regexp in its full entirety
    // and hence doesn't match the text which should actually match

    if (savedSearchName != savedSearchName.trimmed()) {
        errorDescription.setBase(
            "Saved search name shouldn't start or end with whitespace");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    const QString & savedSearchQuery = *savedSearch.query();
    if (savedSearchQuery.size() < qevercloud::EDAM_SEARCH_QUERY_LEN_MIN) {
        errorDescription.setBase("Saved search query length is too small");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (savedSearchQuery.size() > qevercloud::EDAM_SEARCH_QUERY_LEN_MAX) {
        errorDescription.setBase("Saved search query length is too large");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    // NOTE: tried to use qevercloud::EDAM_SEARCH_QUERY_REGEX to check the name
    // correctness but it appears that regex is too perlish for QRegExp to
    // handle i.e. QRegExp doesn't recognize the regexp in its full entirety
    // and hence doesn't match the text which should actually match

    return 0;
}

qint32 FakeNoteStore::checkLinkedNotebookFields(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription) const
{
    if (!linkedNotebook.username()) {
        errorDescription.setBase("Linked notebook owner is not set");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_REQUIRED);
    }

    if (!linkedNotebook.shardId() && !linkedNotebook.uri()) {
        errorDescription.setBase(
            "Neither linked notebook's shard id nor uri is set");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_REQUIRED);
    }

    const auto & usernameIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByUsername>();

    auto linkedNotebookIt = usernameIndex.find(*linkedNotebook.username());
    if (linkedNotebookIt == usernameIndex.end()) {
        errorDescription.setBase(
            "Found no linked notebook corresponding to the owner");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    const auto & existingLinkedNotebook = *linkedNotebookIt;
    if (linkedNotebook.shardId()) {
        if (!existingLinkedNotebook.shardId()) {
            errorDescription.setBase(
                "Linked notebook belonging to this owner has no shard id");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE);
        }

        if (*existingLinkedNotebook.shardId() != *linkedNotebook.shardId()) {
            errorDescription.setBase(
                "Linked notebook belonging to this owner has another shard id");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE);
        }
    }
    else if (linkedNotebook.uri()) {
        if (!existingLinkedNotebook.uri()) {
            errorDescription.setBase(
                "Linked notebook belonging to this owner has no uri");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE);
        }

        if (*existingLinkedNotebook.uri() != *linkedNotebook.uri()) {
            errorDescription.setBase(
                "Linked notebook belonging to this owner has another uri");

            return static_cast<qint32>(
                qevercloud::EDAMErrorCode::SHARD_UNAVAILABLE);
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkAppData(
    const qevercloud::LazyMap & appData, ErrorString & errorDescription) const
{
    QRegExp keyRegExp(qevercloud::EDAM_APPLICATIONDATA_NAME_REGEX);
    QRegExp valueRegExp(qevercloud::EDAM_APPLICATIONDATA_VALUE_REGEX);

    if (appData.keysOnly()) {
        for (auto it = appData.keysOnly()->constBegin(),
                  end = appData.keysOnly()->constEnd();
             it != end; ++it)
        {
            const QString & key = *it;
            qint32 res = checkAppDataKey(key, keyRegExp, errorDescription);
            if (res != 0) {
                return res;
            }
        }
    }

    if (appData.fullMap()) {
        for (auto it = appData.fullMap()->constBegin(),
                  end = appData.fullMap()->constEnd();
             it != end; ++it)
        {
            const QString & key = it.key();
            qint32 res = checkAppDataKey(key, keyRegExp, errorDescription);
            if (res != 0) {
                return res;
            }

            const QString & value = it.value();

            if (value.size() < qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MIN) {
                errorDescription.setBase(
                    QStringLiteral("Resource app data value ") +
                    QStringLiteral("length is too small: ") + value);

                return static_cast<qint32>(
                    qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
            }

            if (value.size() > qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MAX) {
                errorDescription.setBase(
                    QStringLiteral("Resource app data value ") +
                    QStringLiteral("length is too large: ") + value);

                return static_cast<qint32>(
                    qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
            }

            if (!valueRegExp.exactMatch(value)) {
                errorDescription.setBase(
                    "Resource app data value doesn't match the mandatory "
                    "regex");

                return static_cast<qint32>(
                    qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
            }
        }
    }

    return 0;
}

qint32 FakeNoteStore::checkAppDataKey(
    const QString & key, const QRegExp & keyRegExp,
    ErrorString & errorDescription) const
{
    if (key.size() < qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) {
        errorDescription.setBase(
            QStringLiteral("Resource app data key length ") +
            QStringLiteral("is too small: ") + key);

        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (key.size() > qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX) {
        errorDescription.setBase(
            QStringLiteral("Resource app data key length ") +
            QStringLiteral("is too large: ") + key);

        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (!keyRegExp.exactMatch(key)) {
        errorDescription.setBase(
            "Resource app data key doesn't match the mandatory regex");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    return 0;
}

qint32 FakeNoteStore::checkLinkedNotebookAuthToken(
    const qevercloud::LinkedNotebook & linkedNotebook,
    const QString & linkedNotebookAuthToken,
    ErrorString & errorDescription) const
{
    const QString & username = linkedNotebook.username().value();
    auto authTokenIt = m_data->m_linkedNotebookAuthTokens.find(username);

    if (authTokenIt == m_data->m_linkedNotebookAuthTokens.end()) {
        errorDescription.setBase(
            "Found no auth token for the given linked notebook");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    const QString & expectedLinkedNotebookAuthToken = authTokenIt.value();
    if (linkedNotebookAuthToken != expectedLinkedNotebookAuthToken) {
        errorDescription.setBase("Wrong linked notebook auth token");
        QNWARNING(
            "tests:synchronization",
            errorDescription
                << ", expected: " << expectedLinkedNotebookAuthToken
                << ", got: " << linkedNotebookAuthToken
                << ", linked notebook: " << linkedNotebook);

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    return 0;
}

qint32 FakeNoteStore::checkLinkedNotebookAuthTokenForNotebook(
    const QString & notebookGuid, const QString & linkedNotebookAuthToken,
    ErrorString & errorDescription) const
{
    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    auto notebookIt = notebookGuidIndex.find(notebookGuid);
    if (notebookIt == notebookGuidIndex.end()) {
        errorDescription.setBase("No notebook with specified guid was found");
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    const auto & notebook = *notebookIt;
    if (!notebook.linkedNotebookGuid()) {
        errorDescription.setBase(
            "Notebook doesn't belong to a linked notebook");
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    if (linkedNotebookAuthToken.isEmpty()) {
        errorDescription.setBase(
            "Notebook belongs to a linked notebook but linked notebook "
            "auth token is empty");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    const QString & linkedNotebookGuid = *notebook.linkedNotebookGuid();

    const auto & linkedNotebookGuidIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

    auto linkedNotebookIt = linkedNotebookGuidIndex.find(linkedNotebookGuid);
    if (linkedNotebookIt == linkedNotebookGuidIndex.end()) {
        errorDescription.setBase(
            "Found no linked notebook corresponding to the notebook");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    return checkLinkedNotebookAuthToken(
        *linkedNotebookIt, linkedNotebookAuthToken, errorDescription);
}

qint32 FakeNoteStore::checkLinkedNotebookAuthTokenForTag(
    const qevercloud::Tag & tag, const QString & linkedNotebookAuthToken,
    ErrorString & errorDescription) const
{
    if (!linkedNotebookAuthToken.isEmpty() && !tag.linkedNotebookGuid()) {
        errorDescription.setBase("Excess linked notebook auth token");
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    if (!tag.linkedNotebookGuid()) {
        errorDescription.setBase("Tag doesn't belong to a linked notebook");
        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    if (linkedNotebookAuthToken.isEmpty()) {
        errorDescription.setBase(
            "Tag belongs to a linked notebook "
            "but linked notebook auth token is empty");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    const auto & linkedNotebookGuidIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

    auto it = linkedNotebookGuidIndex.find(*tag.linkedNotebookGuid());
    if (it == linkedNotebookGuidIndex.end()) {
        errorDescription.setBase(
            "Tag belongs to a linked notebook but it was not found by guid");

        return static_cast<qint32>(
            qevercloud::EDAMErrorCode::PERMISSION_DENIED);
    }

    return checkLinkedNotebookAuthToken(
        *it, linkedNotebookAuthToken, errorDescription);
}

QString FakeNoteStore::nextName(const QString & name) const
{
    int lastIndex = name.lastIndexOf(QStringLiteral("_"));
    if (lastIndex >= 0) {
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

qint32 FakeNoteStore::getSyncChunkImpl(
    const qint32 afterUSN, const qint32 maxEntries, const bool fullSyncOnly,
    const QString & linkedNotebookGuid,
    const qevercloud::SyncChunkFilter & filter,
    qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription)
{
    if (afterUSN < 0) {
        errorDescription.setBase("After USN is negative");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    if (maxEntries < 1) {
        errorDescription.setBase("Max entries is less than 1");
        return static_cast<qint32>(qevercloud::EDAMErrorCode::BAD_DATA_FORMAT);
    }

    syncChunk = qevercloud::SyncChunk{};
    syncChunk.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

    if (filter.notebookGuids() && !filter.notebookGuids()->isEmpty() &&
        filter.includeExpunged() && *filter.includeExpunged())
    {
        errorDescription.setBase(
            "Can't set notebook guids along with include expunged");

        return static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT);
    }

    const auto & savedSearchUsnIndex =
        m_data->m_savedSearches.get<SavedSearchByUSN>();

    const auto & tagUsnIndex = m_data->m_tags.get<TagByUSN>();
    const auto & notebookUsnIndex = m_data->m_notebooks.get<NotebookByUSN>();
    const auto & noteUsnIndex = m_data->m_notes.get<NoteByUSN>();
    const auto & resourceUsnIndex = m_data->m_resources.get<ResourceByUSN>();

    const auto & linkedNotebookUsnIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByUSN>();

    syncChunk.setUpdateCount(currentMaxUsn(linkedNotebookGuid));
    QNDEBUG(
        "tests:synchronization",
        "Sync chunk update count = " << syncChunk.updateCount());

    auto & guidsOfCompleteSentItems =
        (linkedNotebookGuid.isEmpty()
             ? m_data->m_guidsOfUserOwnCompleteSentItems
             : m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                   [linkedNotebookGuid]);

    auto savedSearchIt = savedSearchUsnIndex.end();
    if (linkedNotebookGuid.isEmpty() && filter.includeSearches() &&
        *filter.includeSearches())
    {
        savedSearchIt = std::upper_bound(
            savedSearchUsnIndex.begin(), savedSearchUsnIndex.end(), afterUSN,
            CompareByUSN<qevercloud::SavedSearch>());
    }

    auto tagIt = tagUsnIndex.end();
    if (filter.includeTags() && *filter.includeTags()) {
        tagIt = std::upper_bound(
            tagUsnIndex.begin(), tagUsnIndex.end(), afterUSN,
            CompareByUSN<qevercloud::Tag>());

        tagIt = advanceIterator(tagIt, tagUsnIndex, linkedNotebookGuid);
    }

    auto notebookIt = notebookUsnIndex.end();
    if (filter.includeNotebooks() && *filter.includeNotebooks()) {
        notebookIt = std::upper_bound(
            notebookUsnIndex.begin(), notebookUsnIndex.end(), afterUSN,
            CompareByUSN<qevercloud::Notebook>());

        notebookIt =
            advanceIterator(notebookIt, notebookUsnIndex, linkedNotebookGuid);
    }

    auto noteIt = noteUsnIndex.end();
    if (filter.includeNotes() && *filter.includeNotes()) {
        noteIt = std::upper_bound(
            noteUsnIndex.begin(), noteUsnIndex.end(), afterUSN,
            CompareByUSN<qevercloud::Note>());

        noteIt = nextNoteByUsnIterator(noteIt, linkedNotebookGuid);
    }

    auto resourceIt = resourceUsnIndex.end();
    if (!fullSyncOnly && filter.includeResources() &&
        *filter.includeResources())
    {
        resourceIt = std::upper_bound(
            resourceUsnIndex.begin(), resourceUsnIndex.end(), afterUSN,
            CompareByUSN<qevercloud::Resource>());

        resourceIt = nextResourceByUsnIterator(resourceIt, linkedNotebookGuid);
    }

    auto linkedNotebookIt = linkedNotebookUsnIndex.end();
    if (linkedNotebookGuid.isEmpty() && filter.includeLinkedNotebooks() &&
        *filter.includeLinkedNotebooks())
    {
        linkedNotebookIt = std::upper_bound(
            linkedNotebookUsnIndex.begin(), linkedNotebookUsnIndex.end(),
            afterUSN, CompareByUSN<qevercloud::LinkedNotebook>());
    }

    while (true) {
        auto nextItemType = NextItemType::None;
        qint32 lastItemUsn = std::numeric_limits<qint32>::max();

        if (savedSearchIt != savedSearchUsnIndex.end()) {
            const auto & nextSearch = *savedSearchIt;
            QNDEBUG(
                "tests:synchronization",
                "Checking saved search for the possibility to include it into "
                    << "the sync chunk: " << nextSearch);

            qint32 usn = nextSearch.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::SavedSearch;
                QNDEBUG(
                    "tests:synchronization",
                    "Will include saved search into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (tagIt != tagUsnIndex.end())) {
            const auto & nextTag = *tagIt;
            QNDEBUG(
                "tests:synchronization",
                "Checking tag for the possibility to include it into the sync "
                    << "chunk: " << nextTag);

            qint32 usn = nextTag.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Tag;
                QNDEBUG(
                    "tests:synchronization",
                    "Will include tag into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (notebookIt != notebookUsnIndex.end()))
        {
            const auto & nextNotebook = *notebookIt;
            QNDEBUG(
                "tests:synchronization",
                "Checking notebook for the possibility to include it into "
                    << "the sync chunk: " << nextNotebook);

            qint32 usn = nextNotebook.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Notebook;
                QNDEBUG(
                    "tests:synchronization",
                    "Will include notebook into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (noteIt != noteUsnIndex.end())) {
            const auto & nextNote = *noteIt;
            QNDEBUG(
                "tests:synchronization",
                "Checking note for the possibility to include it into the sync "
                    << "chunk: " << nextNote);

            qint32 usn = nextNote.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Note;
                QNDEBUG(
                    "tests:synchronization",
                    "Will include note into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (resourceIt != resourceUsnIndex.end()))
        {
            const auto & nextResource = *resourceIt;
            QNDEBUG(
                "tests:synchronization",
                "Checking resource for the possibility to include it into "
                    << "the sync chunk: " << nextResource);

            qint32 usn = nextResource.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Resource;
                QNDEBUG(
                    "tests:synchronization",
                    "Will include resource into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (linkedNotebookIt != linkedNotebookUsnIndex.end()))
        {
            const auto & nextLinkedNotebook = *linkedNotebookIt;
            QNDEBUG(
                "tests:synchronization",
                "Checking linked notebook for the possibility to include it "
                    << "into the sync chunk: " << nextLinkedNotebook);

            qint32 usn = nextLinkedNotebook.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                nextItemType = NextItemType::LinkedNotebook;
                QNDEBUG(
                    "tests:synchronization",
                    "Will include linked notebook into the sync chunk");
            }
        }

        if (nextItemType == NextItemType::None) {
            break;
        }

        switch (nextItemType) {
        case NextItemType::SavedSearch:
        {
            if (!syncChunk.searches()) {
                syncChunk.setSearches(QList<qevercloud::SavedSearch>{});
            }

            syncChunk.mutableSearches()->append(*savedSearchIt);

            syncChunk.setChunkHighUSN(
                savedSearchIt->updateSequenceNum().value());

            QNDEBUG(
                "tests:synchronization",
                "Added saved search to sync chunk: "
                    << *savedSearchIt
                    << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

            if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
                Q_UNUSED(guidsOfCompleteSentItems.m_savedSearchGuids.insert(
                    savedSearchIt->guid().value()))
            }

            ++savedSearchIt;
        } break;
        case NextItemType::Tag:
        {
            if (!syncChunk.tags()) {
                syncChunk.setTags(QList<qevercloud::Tag>{});
            }

            syncChunk.mutableTags()->append(*tagIt);
            syncChunk.setChunkHighUSN(tagIt->updateSequenceNum().value());
            QNDEBUG(
                "tests:synchronization",
                "Added tag to sync chunk: "
                    << *tagIt
                    << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

            if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
                Q_UNUSED(guidsOfCompleteSentItems.m_tagGuids.insert(
                    tagIt->guid().value()))
            }

            ++tagIt;
            tagIt = advanceIterator(tagIt, tagUsnIndex, linkedNotebookGuid);
        } break;
        case NextItemType::Notebook:
        {
            if (!syncChunk.notebooks()) {
                syncChunk.setNotebooks(QList<qevercloud::Notebook>{});
            }

            syncChunk.mutableNotebooks()->append(*notebookIt);
            syncChunk.setChunkHighUSN(notebookIt->updateSequenceNum().value());

            QNDEBUG(
                "tests:synchronization",
                "Added notebook to sync chunk: "
                    << *notebookIt
                    << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

            if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
                Q_UNUSED(guidsOfCompleteSentItems.m_notebookGuids.insert(
                    notebookIt->guid().value()))
            }

            ++notebookIt;

            notebookIt = advanceIterator(
                notebookIt, notebookUsnIndex, linkedNotebookGuid);
        } break;
        case NextItemType::Note:
        {
            if (!syncChunk.notes()) {
                syncChunk.setNotes(QList<qevercloud::Note>{});
            }

            auto qecNote = *noteIt;

            if (!filter.includeNoteResources() ||
                !*filter.includeNoteResources()) {
                qecNote.setResources(std::nullopt);
            }

            if (!filter.includeNoteAttributes() ||
                !*filter.includeNoteAttributes())
            {
                qecNote.setAttributes(std::nullopt);
            }
            else {
                if ((!filter.includeNoteApplicationDataFullMap() ||
                     !*filter.includeNoteApplicationDataFullMap()) &&
                    qecNote.attributes() &&
                    qecNote.attributes()->applicationData())
                {
                    qecNote.mutableAttributes()->mutableApplicationData()
                        ->setFullMap(std::nullopt);
                }

                if ((!filter.includeNoteResourceApplicationDataFullMap() ||
                     !*filter.includeNoteResourceApplicationDataFullMap()) &&
                    qecNote.resources())
                {
                    for (auto it = qecNote.mutableResources()->begin(),
                              end = qecNote.mutableResources()->end();
                         it != end; ++it)
                    {
                        auto & resource = *it;
                        if (resource.attributes() &&
                            resource.attributes()->applicationData()) {
                            resource.mutableAttributes()
                                ->mutableApplicationData()
                                ->setFullMap(std::nullopt);
                        }
                    }
                }
            }

            if (!filter.includeSharedNotes() ||
                !*filter.includeSharedNotes()) {
                qecNote.setSharedNotes(std::nullopt);
            }

            // Notes within the sync chunks should include only note
            // metadata bt no content, resource content, resource
            // recognition data or resource alternate data
            qecNote.setContent(std::nullopt);
            if (qecNote.resources()) {
                for (auto it = qecNote.mutableResources()->begin(),
                          end = qecNote.mutableResources()->end();
                     it != end; ++it)
                {
                    auto & resource = *it;
                    if (resource.data()) {
                        resource.mutableData()->setBody(std::nullopt);
                    }
                    if (resource.recognition()) {
                        resource.mutableRecognition()->setBody(std::nullopt);
                    }
                    if (resource.alternateData()) {
                        resource.mutableAlternateData()->setBody(std::nullopt);
                    }
                }
            }

            syncChunk.mutableNotes()->append(qecNote);
            syncChunk.setChunkHighUSN(noteIt->updateSequenceNum().value());

            QNDEBUG(
                "tests:synchronization",
                "Added note to sync chunk: "
                    << qecNote << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

            ++noteIt;
            noteIt = nextNoteByUsnIterator(noteIt, linkedNotebookGuid);
        } break;
        case NextItemType::Resource:
        {
            if (!syncChunk.resources()) {
                syncChunk.setResources(QList<qevercloud::Resource>{});
            }

            auto qecResource = *resourceIt;

            if ((!filter.includeResourceApplicationDataFullMap() ||
                 !*filter.includeResourceApplicationDataFullMap()) &&
                qecResource.attributes() &&
                qecResource.attributes()->applicationData())
            {
                qecResource.mutableAttributes()->mutableApplicationData()
                    ->setFullMap(std::nullopt);
            }

            // Resources within the sync chunks should not include data,
            // recognition data or alternate data
            if (qecResource.data()) {
                qecResource.mutableData()->setBody(std::nullopt);
            }

            if (qecResource.recognition()) {
                qecResource.mutableRecognition()->setBody(std::nullopt);
            }

            if (qecResource.alternateData()) {
                qecResource.mutableAlternateData()->setBody(std::nullopt);
            }

            syncChunk.mutableResources()->append(qecResource);
            syncChunk.setChunkHighUSN(resourceIt->updateSequenceNum().value());

            QNDEBUG(
                "tests:synchronization",
                "Added resource to sync "
                    << "chunk: " << qecResource
                    << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

            ++resourceIt;

            resourceIt =
                nextResourceByUsnIterator(resourceIt, linkedNotebookGuid);
        } break;
        case NextItemType::LinkedNotebook:
        {
            if (!syncChunk.linkedNotebooks()) {
                syncChunk.setLinkedNotebooks(
                    QList<qevercloud::LinkedNotebook>{});
            }

            syncChunk.mutableLinkedNotebooks()->append(*linkedNotebookIt);

            syncChunk.setChunkHighUSN(
                linkedNotebookIt->updateSequenceNum().value());

            QNDEBUG(
                "tests:synchronization",
                "Added linked notebook to sync chunk: "
                    << *linkedNotebookIt
                    << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

            if (!m_data->m_onceAPIRateLimitExceedingTriggered) {
                Q_UNUSED(guidsOfCompleteSentItems.m_linkedNotebookGuids.insert(
                    linkedNotebookIt->guid().value()))
            }

            ++linkedNotebookIt;
        } break;
        default:
            QNWARNING(
                "tests:synchronization",
                "Unexpected next item type: " << nextItemType);
            break;
        }
    }

    if (!syncChunk.chunkHighUSN()) {
        syncChunk.setChunkHighUSN(syncChunk.updateCount());
        QNDEBUG(
            "tests:synchronization",
            "Sync chunk's high USN was still not set, set it to the update "
                << "count: "
                << syncChunk.updateCount());
    }

    if (fullSyncOnly) {
        // No need to insert the information about expunged data items
        // when doing full sync
        return 0;
    }

    if (linkedNotebookGuid.isEmpty() &&
        !m_data->m_expungedSavedSearchGuids.isEmpty())
    {
        if (!syncChunk.expungedSearches()) {
            syncChunk.setExpungedSearches(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedSearches()->reserve(
            m_data->m_expungedSavedSearchGuids.size());

        for (auto it = m_data->m_expungedSavedSearchGuids.constBegin(),
                  end = m_data->m_expungedSavedSearchGuids.constEnd();
             it != end; ++it)
        {
            syncChunk.mutableExpungedSearches()->append(*it);
        }
    }

    if (linkedNotebookGuid.isEmpty() && !m_data->m_expungedTagGuids.isEmpty()) {
        if (!syncChunk.expungedTags()) {
            syncChunk.setExpungedTags(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedTags()->reserve(
            m_data->m_expungedTagGuids.size());

        for (auto it = m_data->m_expungedTagGuids.constBegin(),
                  end = m_data->m_expungedTagGuids.constEnd();
             it != end; ++it)
        {
            syncChunk.mutableExpungedTags()->append(*it);
        }
    }

    if (!m_data->m_expungedNotebookGuids.isEmpty()) {
        if (!syncChunk.expungedNotebooks()) {
            syncChunk.setExpungedNotebooks(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedNotebooks()->reserve(
            m_data->m_expungedNotebookGuids.size());

        for (auto it = m_data->m_expungedNotebookGuids.constBegin(),
                  end = m_data->m_expungedNotebookGuids.constEnd();
             it != end; ++it)
        {
            syncChunk.mutableExpungedNotebooks()->append(*it);
        }
    }

    if (!m_data->m_expungedNoteGuids.isEmpty()) {
        if (!syncChunk.expungedNotes()) {
            syncChunk.setExpungedNotes(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedNotes()->reserve(
            m_data->m_expungedNoteGuids.size());

        for (auto it = m_data->m_expungedNoteGuids.constBegin(),
                  end = m_data->m_expungedNoteGuids.constEnd();
             it != end; ++it)
        {
            syncChunk.mutableExpungedNotes()->append(*it);
        }
    }

    if (linkedNotebookGuid.isEmpty() &&
        !m_data->m_expungedLinkedNotebookGuids.isEmpty())
    {
        if (!syncChunk.expungedLinkedNotebooks()) {
            syncChunk.setExpungedLinkedNotebooks(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedLinkedNotebooks()->reserve(
            m_data->m_expungedLinkedNotebookGuids.size());

        for (auto it = m_data->m_expungedLinkedNotebookGuids.constBegin(),
                  end = m_data->m_expungedLinkedNotebookGuids.constEnd();
             it != end; ++it)
        {
            syncChunk.mutableExpungedLinkedNotebooks()->append(*it);
        }
    }

    return 0;
}

void FakeNoteStore::considerAllExistingDataItemsSentBeforeRateLimitBreachImpl(
    const QString & linkedNotebookGuid)
{
    QNDEBUG(
        "tests:synchronization",
        "FakeNoteStore"
            << "::considerAllExistingDataItemsSentBeforeRateLimitBreachImpl: "
            << "linked notebook guid = " << linkedNotebookGuid);

    auto & guidsOfCompleteSentItems =
        (linkedNotebookGuid.isEmpty()
             ? m_data->m_guidsOfUserOwnCompleteSentItems
             : m_data->m_guidsOfCompleteSentItemsByLinkedNotebookGuid
                   [linkedNotebookGuid]);

    if (linkedNotebookGuid.isEmpty()) {
        const auto & savedSearchGuidIndex =
            m_data->m_savedSearches.get<SavedSearchByGuid>();

        for (auto it = savedSearchGuidIndex.begin(),
                  end = savedSearchGuidIndex.end();
             it != end; ++it)
        {
            Q_UNUSED(
                guidsOfCompleteSentItems.m_savedSearchGuids.insert(
                    it->guid().value()))

            QNTRACE(
                "tests:synchronization",
                "Marked saved search as processed: " << *it);
        }
    }

    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    for (auto it = notebookGuidIndex.begin(), end = notebookGuidIndex.end();
         it != end; ++it)
    {
        const auto & notebook = *it;
        if (linkedNotebookGuid.isEmpty() ==
            notebook.linkedNotebookGuid().has_value()) {
            continue;
        }

        if (notebook.linkedNotebookGuid() &&
            (*notebook.linkedNotebookGuid() != linkedNotebookGuid))
        {
            continue;
        }

        Q_UNUSED(guidsOfCompleteSentItems.m_notebookGuids.insert(
            notebook.guid().value()))

        QNTRACE(
            "tests:synchronization",
            "Marked notebook as processed: " << notebook);
    }

    const auto & tagGuidIndex = m_data->m_tags.get<TagByGuid>();
    for (auto it = tagGuidIndex.begin(), end = tagGuidIndex.end(); it != end;
         ++it) {
        const auto & tag = *it;
        if (linkedNotebookGuid.isEmpty() ==
            tag.linkedNotebookGuid().has_value()) {
            continue;
        }

        if (tag.linkedNotebookGuid() &&
            (*tag.linkedNotebookGuid() != linkedNotebookGuid))
        {
            continue;
        }

        Q_UNUSED(guidsOfCompleteSentItems.m_tagGuids.insert(
            tag.guid().value()))

        QNTRACE("tests:synchronization", "Marked tag as processed: " << tag);
    }

    if (linkedNotebookGuid.isEmpty()) {
        const auto & linkedNotebookGuidIndex =
            m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

        for (auto it = linkedNotebookGuidIndex.begin(),
                  end = linkedNotebookGuidIndex.end();
             it != end; ++it)
        {
            Q_UNUSED(guidsOfCompleteSentItems.m_linkedNotebookGuids.insert(
                it->guid().value()))

            QNTRACE(
                "tests:synchronization",
                "Marked linked notebook as processed: " << *it);
        }
    }

    const auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    for (auto it = noteGuidIndex.begin(), end = noteGuidIndex.end(); it != end;
         ++it)
    {
        const auto & note = *it;
        auto notebookIt = notebookGuidIndex.find(note.notebookGuid().value());
        if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Skipping note for which no notebook was found: " << note);
            continue;
        }

        const auto & notebook = *notebookIt;
        if (linkedNotebookGuid.isEmpty() ==
            notebook.linkedNotebookGuid().has_value()) {
            continue;
        }

        if (notebook.linkedNotebookGuid() &&
            (*notebook.linkedNotebookGuid() != linkedNotebookGuid))
        {
            continue;
        }

        Q_UNUSED(guidsOfCompleteSentItems.m_noteGuids.insert(
            note.guid().value()))

        QNTRACE("tests:synchronization", "Marked note as processed: " << note);
    }

    const auto & resourceGuidIndex = m_data->m_resources.get<ResourceByGuid>();
    for (auto it = resourceGuidIndex.begin(), end = resourceGuidIndex.end();
         it != end; ++it)
    {
        const auto & resource = *it;
        auto noteIt = noteGuidIndex.find(resource.noteGuid().value());
        if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Skipping resource for which no note was found: " << resource);
            continue;
        }

        const auto & note = *noteIt;
        auto notebookIt = notebookGuidIndex.find(note.notebookGuid().value());
        if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Skipping resource for which note no notebook was found: "
                    << note);
            continue;
        }

        const auto & notebook = *notebookIt;
        if (linkedNotebookGuid.isEmpty() ==
            notebook.linkedNotebookGuid().has_value()) {
            continue;
        }

        if (notebook.linkedNotebookGuid() &&
            (*notebook.linkedNotebookGuid() != linkedNotebookGuid))
        {
            continue;
        }

        Q_UNUSED(guidsOfCompleteSentItems.m_resourceGuids.insert(
            resource.guid().value()))

        QNTRACE(
            "tests:synchronization",
            "Marked resource as processed: " << resource);
    }
}

void FakeNoteStore::storeCurrentMaxUsnsAsThoseBeforeRateLimitBreach()
{
    storeCurrentMaxUsnAsThatBeforeRateLimitBreachImpl();

    const auto & linkedNotebookGuidIndex =
        m_data->m_linkedNotebooks.get<LinkedNotebookByGuid>();

    for (auto it = linkedNotebookGuidIndex.begin(),
              end = linkedNotebookGuidIndex.end();
         it != end; ++it)
    {
        storeCurrentMaxUsnAsThatBeforeRateLimitBreachImpl(it->guid().value());
    }
}

void FakeNoteStore::storeCurrentMaxUsnAsThatBeforeRateLimitBreachImpl(
    const QString & linkedNotebookGuid)
{
    qint32 maxUsn = currentMaxUsn(linkedNotebookGuid);

    if (linkedNotebookGuid.isEmpty()) {
        m_data->m_maxUsnForUserOwnDataBeforeRateLimitBreach = maxUsn;
        QNTRACE(
            "tests:synchronization",
            "Max USN for user own data before rate limit breach: " << maxUsn);
    }
    else {
        m_data->m_maxUsnsForLinkedNotebooksDataBeforeRateLimitBreach
            [linkedNotebookGuid] = maxUsn;

        QNTRACE(
            "tests:synchronization",
            "Max USN for linked notebook with guid " << linkedNotebookGuid
                << " is " << maxUsn);
    }
}

template <class ConstIterator, class UsnIndex>
ConstIterator FakeNoteStore::advanceIterator(
    ConstIterator it, const UsnIndex & index,
    const QString & linkedNotebookGuid) const
{
    while (it != index.end()) {
        if (linkedNotebookGuid.isEmpty() && it->linkedNotebookGuid()) {
            ++it;
            continue;
        }

        if (!linkedNotebookGuid.isEmpty() && !it->linkedNotebookGuid()) {
            ++it;
            continue;
        }

        if (!linkedNotebookGuid.isEmpty() && it->linkedNotebookGuid() &&
            (linkedNotebookGuid != *it->linkedNotebookGuid()))
        {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

FakeNoteStore::NoteDataByUSN::const_iterator
FakeNoteStore::nextNoteByUsnIterator(
    NoteDataByUSN::const_iterator it,
    const QString & targetLinkedNotebookGuid) const
{
    const auto & noteUsnIndex = m_data->m_notes.get<NoteByUSN>();
    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();

    while (it != noteUsnIndex.end()) {
        const QString & notebookGuid = it->notebookGuid().value();
        auto noteNotebookIt = notebookGuidIndex.find(notebookGuid);
        if (Q_UNLIKELY(noteNotebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Found note which notebook guid "
                    << "doesn't correspond to any existing notebook: " << *it);
            ++it;
            continue;
        }

        const auto & notebook = *noteNotebookIt;
        if (notebook.linkedNotebookGuid() &&
            (targetLinkedNotebookGuid.isEmpty() ||
             (*notebook.linkedNotebookGuid() != targetLinkedNotebookGuid)))
        {
            ++it;
            continue;
        }
        else if (
            !notebook.linkedNotebookGuid() &&
            !targetLinkedNotebookGuid.isEmpty())
        {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

FakeNoteStore::ResourceDataByUSN::const_iterator
FakeNoteStore::nextResourceByUsnIterator(
    ResourceDataByUSN::const_iterator it,
    const QString & targetLinkedNotebookGuid) const
{
    const auto & resourceUsnIndex = m_data->m_resources.get<ResourceByUSN>();
    const auto & noteGuidIndex = m_data->m_notes.get<NoteByGuid>();
    const auto & notebookGuidIndex = m_data->m_notebooks.get<NotebookByGuid>();
    while (it != resourceUsnIndex.end()) {
        const QString & noteGuid = it->noteGuid().value();

        auto resourceNoteIt = noteGuidIndex.find(noteGuid);
        if (Q_UNLIKELY(resourceNoteIt == noteGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Found resource which note guid "
                    << "doesn't correspond to any existing note: " << *it);
            ++it;
            continue;
        }

        const auto & note = *resourceNoteIt;
        const QString & notebookGuid = note.notebookGuid().value();
        auto noteNotebookIt = notebookGuidIndex.find(notebookGuid);
        if (Q_UNLIKELY(noteNotebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests:synchronization",
                "Found note which notebook guid "
                    << "doesn't correspond to any existing notebook: " << note);
            ++it;
            continue;
        }

        const auto & notebook = *noteNotebookIt;
        if (notebook.linkedNotebookGuid() &&
            (targetLinkedNotebookGuid.isEmpty() ||
             (notebook.linkedNotebookGuid() != targetLinkedNotebookGuid)))
        {
            ++it;
            continue;
        }
        else if (
            !notebook.linkedNotebookGuid() &&
            !targetLinkedNotebookGuid.isEmpty())
        {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

////////////////////////////////////////////////////////////////////////////////

QDebug & operator<<(
    QDebug & dbg, const FakeNoteStore::NextItemType nextItemType)
{
    using NextItemType = FakeNoteStore::NextItemType;

    switch (nextItemType) {
    case NextItemType::None:
        dbg << "none";
        break;
    case NextItemType::SavedSearch:
        dbg << "saved search";
        break;
    case NextItemType::Tag:
        dbg << "tag";
        break;
    case NextItemType::Notebook:
        dbg << "notebook";
        break;
    case NextItemType::Note:
        dbg << "note";
        break;
    case NextItemType::Resource:
        dbg << "resource";
        break;
    case NextItemType::LinkedNotebook:
        dbg << "linked notebook";
        break;
    default:
        dbg << "unknown (" << static_cast<qint64>(nextItemType) << ")";
        break;
    }

    return dbg;
}

} // namespace quentier
