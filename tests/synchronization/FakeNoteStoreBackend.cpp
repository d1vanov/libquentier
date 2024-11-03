/*
 * Copyright 2024 Dmitry Ivanov
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

#include "FakeNoteStoreBackend.h"

#include "note_store/Checks.h"
#include "note_store/Compat.h"
#include "utils/ExceptionUtils.h"
#include "utils/HttpUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/IRequestContext.h>
#include <qevercloud/exceptions/builders/EDAMNotFoundExceptionBuilder.h>
#include <qevercloud/exceptions/builders/EDAMSystemExceptionBuilder.h>
#include <qevercloud/exceptions/builders/EDAMUserExceptionBuilder.h>
#include <qevercloud/types/NoteResultSpec.h>
#include <qevercloud/types/SyncChunkFilter.h>
#include <qevercloud/utility/ToRange.h>

#include <QDateTime>
#include <QTest>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace quentier::synchronization::tests {

namespace {

// Strips fields with content from the passed in note to mimic the behaviour of
// qevercloud::INoteStore::createNote and qevercloud::INoteStore::updateNote
[[nodiscard]] qevercloud::Note noteMetadata(qevercloud::Note note)
{
    qevercloud::Note n = std::move(note);
    n.setContent(std::nullopt);
    if (n.resources()) {
        for (auto & resource: *n.mutableResources()) {
            if (resource.data()) {
                resource.mutableData()->setBody(std::nullopt);
            }
        }
    }

    return n;
}

[[nodiscard]] QString nextName(const QString & name)
{
    auto lastIndex = name.lastIndexOf(QStringLiteral("_"));
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

template <class ConstIterator, class UsnIndex>
[[nodiscard]] ConstIterator advanceIterator(
    ConstIterator it, const UsnIndex & index,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid) noexcept
{
    while (it != index.end()) {
        if (linkedNotebookGuid != it->linkedNotebookGuid()) {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

template <class T>
class CompareByUSN
{
public:
    [[nodiscard]] bool operator()(const qint32 usn, const T & item) const
    {
        return usn < item.updateSequenceNum().value();
    }
};

// Enum used to help maintain the bookkeeping
// on which kind of item should be the next one inserted into
// the sync chunk
enum class NextItemType
{
    None = 0,
    SavedSearch,
    Tag,
    Notebook,
    Note,
    Resource,
    LinkedNotebook
};

QDebug & operator<<(QDebug & dbg, NextItemType nextItemType)
{
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

} // namespace

FakeNoteStoreBackend::FakeNoteStoreBackend(
    QString authenticationToken, QList<QNetworkCookie> cookies,
    QObject * parent) :
    QObject(parent), m_authenticationToken{std::move(authenticationToken)},
    m_cookies{std::move(cookies)}
{}

FakeNoteStoreBackend::~FakeNoteStoreBackend() = default;

QHash<qevercloud::Guid, qevercloud::SavedSearch>
    FakeNoteStoreBackend::savedSearches() const
{
    QHash<qevercloud::Guid, qevercloud::SavedSearch> result;
    result.reserve(static_cast<int>(m_savedSearches.size()));

    for (const auto & savedSearch: m_savedSearches) {
        Q_ASSERT(savedSearch.guid());
        result[*savedSearch.guid()] = savedSearch;
    }

    return result;
}

FakeNoteStoreBackend::ItemData FakeNoteStoreBackend::putSavedSearch(
    qevercloud::SavedSearch search)
{
    ItemData result;

    if (!search.guid()) {
        result.guid = UidGenerator::Generate();
        search.setGuid(result.guid);
    }

    if (!search.name()) {
        search.setName(QStringLiteral("Saved search"));
    }

    const auto originalName = *search.name();

    auto & nameIndex =
        m_savedSearches.get<note_store::SavedSearchByNameUpperTag>();

    auto nameIt = nameIndex.find(search.name()->toUpper());
    while (nameIt != nameIndex.end()) {
        if (nameIt->guid() == search.guid()) {
            break;
        }

        QString name = nextName(*search.name());
        search.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    if (originalName != *search.name()) {
        result.name = *search.name();
    }

    qint32 maxUsn = currentUserOwnMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNum(maxUsn);
    setMaxUsn(maxUsn);
    result.usn = maxUsn;

    removeExpungedSavedSearchGuid(*search.guid());

    auto & savedSearchGuidIndex =
        m_savedSearches.get<note_store::SavedSearchByGuidTag>();

    const auto it = savedSearchGuidIndex.find(*search.guid());
    if (it == savedSearchGuidIndex.end()) {
        m_savedSearches.emplace(std::move(search));
    }
    else {
        savedSearchGuidIndex.replace(it, std::move(search));
    }

    return result;
}

std::optional<qevercloud::SavedSearch> FakeNoteStoreBackend::findSavedSearch(
    const qevercloud::Guid & guid) const
{
    const auto & index =
        m_savedSearches.get<note_store::SavedSearchByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void FakeNoteStoreBackend::removeSavedSearch(const qevercloud::Guid & guid)
{
    auto & index = m_savedSearches.get<note_store::SavedSearchByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        index.erase(it);
    }
}

void FakeNoteStoreBackend::putExpungedSavedSearchGuid(
    const qevercloud::Guid & guid)
{
    removeSavedSearch(guid);
    m_expungedSavedSearchGuidsAndUsns.insert(guid, ++m_userOwnMaxUsn);
}

bool FakeNoteStoreBackend::containsExpungedSavedSearchGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedSavedSearchGuidsAndUsns.contains(guid);
}

void FakeNoteStoreBackend::removeExpungedSavedSearchGuid(
    const qevercloud::Guid & guid)
{
    m_expungedSavedSearchGuidsAndUsns.remove(guid);
}

QHash<qevercloud::Guid, qevercloud::Tag> FakeNoteStoreBackend::tags() const
{
    QHash<qevercloud::Guid, qevercloud::Tag> result;
    result.reserve(static_cast<int>(m_tags.size()));

    for (const auto & tag: m_tags) {
        Q_ASSERT(tag.guid());
        result[*tag.guid()] = tag;
    }

    return result;
}

FakeNoteStoreBackend::ItemData FakeNoteStoreBackend::putTag(qevercloud::Tag tag)
{
    ItemData result;

    if (!tag.guid()) {
        result.guid = UidGenerator::Generate();
        tag.setGuid(result.guid);
    }

    if (!tag.name()) {
        tag.setName(QStringLiteral("Tag"));
    }

    const auto originalName = *tag.name();

    if (tag.linkedNotebookGuid()) {
        const qevercloud::Guid & linkedNotebookGuid = *tag.linkedNotebookGuid();

        const auto & index =
            m_linkedNotebooks.get<note_store::LinkedNotebookByGuidTag>();

        const auto it = index.find(linkedNotebookGuid);
        if (Q_UNLIKELY(it == index.end())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "Detected attempt to put linked notebook's tag for nonexistent "
                "linked notebook")}};
        }
    }

    auto & nameIndex = m_tags.get<note_store::TagByNameUpperTag>();
    auto nameIt = nameIndex.find(tag.name()->toUpper());
    while (nameIt != nameIndex.end()) {
        if (nameIt->guid() == tag.guid()) {
            break;
        }

        QString name = nextName(*tag.name());
        tag.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    if (originalName != *tag.name()) {
        result.name = *tag.name();
    }

    std::optional<qint32> maxUsn = tag.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*tag.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (tag.linkedNotebookGuid()) {
        if (!maxUsn) {
            maxUsn = 0;
        }
    }
    else if (!maxUsn) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Failed to find user own max USN on attempt to put tag")}};
    }

    ++(*maxUsn);
    tag.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, tag.linkedNotebookGuid());
    result.usn = *maxUsn;

    if (!tag.linkedNotebookGuid()) {
        removeExpungedUserOwnTagGuid(*tag.guid());
    }

    auto & tagGuidIndex = m_tags.get<note_store::TagByGuidTag>();
    const auto tagIt = tagGuidIndex.find(*tag.guid());
    if (tagIt == tagGuidIndex.end()) {
        m_tags.emplace(std::move(tag));
    }
    else {
        tagGuidIndex.replace(tagIt, std::move(tag));
    }

    return result;
}

std::optional<qevercloud::Tag> FakeNoteStoreBackend::findTag(
    const QString & guid) const
{
    const auto & index = m_tags.get<note_store::TagByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void FakeNoteStoreBackend::removeTag(const qevercloud::Guid & guid)
{
    auto & index = m_tags.get<note_store::TagByGuidTag>();
    auto tagIt = index.find(guid);
    if (tagIt == index.end()) {
        return;
    }

    const auto & parentTagGuidIndex =
        m_tags.get<note_store::TagByParentTagGuidTag>();

    auto range = parentTagGuidIndex.equal_range(guid);

    QList<qevercloud::Guid> childTagGuids;
    childTagGuids.reserve(
        static_cast<int>(std::distance(range.first, range.second)));

    for (auto it = range.first; it != range.second; ++it) {
        childTagGuids << it->guid().value();
    }

    for (const auto & childTagGuid: std::as_const(childTagGuids)) {
        removeTag(childTagGuid);
    }

    // NOTE: have to once again evaluate the iterator if we deleted any child
    // tags since the deletion of child tags could cause the invalidation of
    // the previously found iterator
    if (!childTagGuids.isEmpty()) {
        tagIt = index.find(guid);
        if (Q_UNLIKELY(tagIt == index.end())) {
            QNWARNING(
                "tests::synchronization::FakeNoteStoreBackend",
                "Tag to be removed is not not found after the removal of its "
                    << "child tags: guid = " << guid);
            return;
        }
    }

    auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    for (auto noteIt = noteGuidIndex.begin(); noteIt != noteGuidIndex.end();
         ++noteIt)
    {
        const auto & note = *noteIt;
        if (!note.tagGuids() || note.tagGuids()->isEmpty()) {
            continue;
        }

        QStringList tagGuids = *note.tagGuids();
        Q_ASSERT(note.tagLocalIds().size() == tagGuids.size());
        auto tagGuidIndex = tagGuids.indexOf(guid);
        if (tagGuidIndex >= 0) {
            tagGuids.removeAt(tagGuidIndex);
            auto noteCopy = note;
            noteCopy.setTagGuids(tagGuids);
            noteCopy.mutableTagLocalIds().removeAt(tagGuidIndex);
            noteGuidIndex.replace(noteIt, noteCopy);
        }
    }

    index.erase(tagIt);
}

void FakeNoteStoreBackend::putExpungedUserOwnTagGuid(
    const qevercloud::Guid & guid)
{
    removeTag(guid);
    m_expungedUserOwnTagGuidsAndUsns.insert(guid, ++m_userOwnMaxUsn);
}

bool FakeNoteStoreBackend::containsExpungedUserOwnTagGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedUserOwnTagGuidsAndUsns.contains(guid);
}

void FakeNoteStoreBackend::removeExpungedUserOwnTagGuid(
    const qevercloud::Guid & guid)
{
    m_expungedUserOwnTagGuidsAndUsns.remove(guid);
}

void FakeNoteStoreBackend::putExpungedLinkedNotebookTagGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & tagGuid)
{
    removeTag(tagGuid);

    std::optional<qint32> maxUsn =
        currentLinkedNotebookMaxUsn(linkedNotebookGuid);
    if (!maxUsn) {
        maxUsn = 0;
    }

    m_expungedLinkedNotebookTagGuidsAndUsns[linkedNotebookGuid].insert(
        tagGuid, ++(*maxUsn));

    setMaxUsn(*maxUsn, linkedNotebookGuid);
}

bool FakeNoteStoreBackend::containsExpungedLinkedNotebookTagGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & tagGuid) const
{
    const auto it =
        m_expungedLinkedNotebookTagGuidsAndUsns.constFind(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookTagGuidsAndUsns.constEnd()) {
        return false;
    }

    return it->contains(tagGuid);
}

void FakeNoteStoreBackend::removeExpungedLinkedNotebookTagGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & tagGuid)
{
    const auto it =
        m_expungedLinkedNotebookTagGuidsAndUsns.find(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookTagGuidsAndUsns.end()) {
        return;
    }

    if (!it->remove(tagGuid)) {
        return;
    }

    if (it->isEmpty()) {
        m_expungedLinkedNotebookTagGuidsAndUsns.erase(it);
    }
}

QHash<qevercloud::Guid, qevercloud::Notebook> FakeNoteStoreBackend::notebooks()
    const
{
    QHash<qevercloud::Guid, qevercloud::Notebook> result;
    result.reserve(static_cast<int>(m_notebooks.size()));

    for (const auto & notebook: m_notebooks) {
        Q_ASSERT(notebook.guid());
        result[*notebook.guid()] = notebook;
    }

    return result;
}

FakeNoteStoreBackend::ItemData FakeNoteStoreBackend::putNotebook(
    qevercloud::Notebook notebook)
{
    ItemData result;

    if (!notebook.guid()) {
        result.guid = UidGenerator::Generate();
        notebook.setGuid(result.guid);
    }

    if (!notebook.name()) {
        notebook.setName(QStringLiteral("Notebook"));
    }

    const auto originalName = *notebook.name();

    if (notebook.linkedNotebookGuid()) {
        const qevercloud::Guid & linkedNotebookGuid =
            *notebook.linkedNotebookGuid();

        const auto & index =
            m_linkedNotebooks.get<note_store::LinkedNotebookByGuidTag>();

        const auto it = index.find(linkedNotebookGuid);
        if (Q_UNLIKELY(it == index.end())) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "Detected attempt to put linked notebook's notebook for "
                "nonexistent linked notebook")}};
        }
    }

    auto & nameIndex = m_notebooks.get<note_store::NotebookByNameUpperTag>();
    auto nameIt = nameIndex.find(notebook.name()->toUpper());
    while (nameIt != nameIndex.end()) {
        if (nameIt->guid() == notebook.guid()) {
            break;
        }

        QString name = nextName(*notebook.name());
        notebook.setName(name);
        nameIt = nameIndex.find(name.toUpper());
    }

    if (originalName != *notebook.name()) {
        result.name = *notebook.name();
    }

    std::optional<qint32> maxUsn = notebook.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*notebook.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (notebook.linkedNotebookGuid()) {
        if (!maxUsn) {
            maxUsn = 0;
        }
    }
    else if (!maxUsn) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Failed to find user own max USN on attempt to put notebook")}};
    }

    ++(*maxUsn);
    notebook.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());
    result.usn = *maxUsn;

    if (!notebook.linkedNotebookGuid()) {
        removeExpungedUserOwnNotebookGuid(*notebook.guid());
    }

    auto & notebookGuidIndex = m_notebooks.get<note_store::NotebookByGuidTag>();
    const auto notebookIt = notebookGuidIndex.find(*notebook.guid());
    if (notebookIt == notebookGuidIndex.end()) {
        m_notebooks.emplace(std::move(notebook));
    }
    else {
        notebookGuidIndex.replace(notebookIt, std::move(notebook));
    }

    return result;
}

std::optional<qevercloud::Notebook> FakeNoteStoreBackend::findNotebook(
    const qevercloud::Guid & guid) const
{
    const auto & index = m_notebooks.get<note_store::NotebookByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void FakeNoteStoreBackend::removeNotebook(const qevercloud::Guid & guid)
{
    auto & index = m_notebooks.get<note_store::NotebookByGuidTag>();
    auto notebookIt = index.find(guid);
    if (notebookIt == index.end()) {
        return;
    }

    const auto & noteDataByNotebookGuid =
        m_notes.get<note_store::NoteByNotebookGuidTag>();

    auto range = noteDataByNotebookGuid.equal_range(guid);

    QList<qevercloud::Guid> noteGuids;

    noteGuids.reserve(
        static_cast<int>(std::distance(range.first, range.second)));

    for (auto it = range.first; it != range.second; ++it) {
        noteGuids << it->guid().value();
    }

    for (const auto & noteGuid: std::as_const(noteGuids)) {
        removeNote(noteGuid);
    }

    index.erase(notebookIt);
}

QList<qevercloud::Notebook>
    FakeNoteStoreBackend::findNotebooksForLinkedNotebookGuid(
        const qevercloud::Guid & linkedNotebookGuid) const
{
    const auto & index =
        m_notebooks.get<note_store::NotebookByLinkedNotebookGuidTag>();

    auto range = index.equal_range(linkedNotebookGuid);
    QList<qevercloud::Notebook> notebooks;

    notebooks.reserve(
        static_cast<int>(std::distance(range.first, range.second)));

    for (auto it = range.first; it != range.second; ++it) {
        notebooks << *it;
    }

    return notebooks;
}

void FakeNoteStoreBackend::putExpungedUserOwnNotebookGuid(
    const qevercloud::Guid & guid)
{
    removeNotebook(guid);
    m_expungedUserOwnNotebookGuidsAndUsns.insert(guid, ++m_userOwnMaxUsn);
}

bool FakeNoteStoreBackend::containsExpungedUserOwnNotebookGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedUserOwnNotebookGuidsAndUsns.contains(guid);
}

void FakeNoteStoreBackend::removeExpungedUserOwnNotebookGuid(
    const qevercloud::Guid & guid)
{
    m_expungedUserOwnNotebookGuidsAndUsns.remove(guid);
}

void FakeNoteStoreBackend::putExpungedLinkedNotebookNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & notebookGuid)
{
    removeNotebook(notebookGuid);

    std::optional<qint32> maxUsn =
        currentLinkedNotebookMaxUsn(linkedNotebookGuid);
    if (!maxUsn) {
        maxUsn = 0;
    }

    m_expungedLinkedNotebookNotebookGuidsAndUsns[linkedNotebookGuid].insert(
        notebookGuid, ++(*maxUsn));

    setMaxUsn(*maxUsn, linkedNotebookGuid);
}

bool FakeNoteStoreBackend::containsExpungedLinkedNotebookNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & notebookGuid) const
{
    const auto it = m_expungedLinkedNotebookNotebookGuidsAndUsns.constFind(
        linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNotebookGuidsAndUsns.constEnd()) {
        return false;
    }

    return it->contains(notebookGuid);
}

void FakeNoteStoreBackend::removeExpungedLinkedNotebookNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & notebookGuid)
{
    const auto it =
        m_expungedLinkedNotebookNotebookGuidsAndUsns.find(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNotebookGuidsAndUsns.end()) {
        return;
    }

    if (!it->remove(notebookGuid)) {
        return;
    }

    if (it->isEmpty()) {
        m_expungedLinkedNotebookNotebookGuidsAndUsns.erase(it);
    }
}

QHash<qevercloud::Guid, qevercloud::Note> FakeNoteStoreBackend::notes() const
{
    QHash<qevercloud::Guid, qevercloud::Note> result;
    result.reserve(static_cast<int>(m_notes.size()));

    for (const auto & note: m_notes) {
        Q_ASSERT(note.guid());
        result[*note.guid()] = note;
    }

    return result;
}

FakeNoteStoreBackend::ItemData FakeNoteStoreBackend::putNote(
    qevercloud::Note note)
{
    if (!note.notebookGuid()) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Detected attempt to put note without notebook guid")}};
    }

    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    const auto notebookIt = notebookGuidIndex.find(*note.notebookGuid());
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Detected attempt to put note without existing notebook")}};
    }

    ItemData result;

    if (!note.guid()) {
        result.guid = UidGenerator::Generate();
        note.setGuid(result.guid);
    }

    std::optional<qint32> maxUsn = notebookIt->linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*notebookIt->linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (notebookIt->linkedNotebookGuid()) {
        if (!maxUsn) {
            maxUsn = 0;
        }
    }
    else if (!maxUsn) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Failed to find user own max USN on attempt to put note")}};
    }

    ++(*maxUsn);
    note.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebookIt->linkedNotebookGuid());
    result.usn = *maxUsn;

    if (!notebookIt->linkedNotebookGuid()) {
        removeExpungedUserOwnNoteGuid(*note.guid());
    }

    auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    auto noteIt = noteGuidIndex.find(*note.guid());
    if (noteIt == noteGuidIndex.end()) {
        auto insertResult = noteGuidIndex.insert(note);
        noteIt = insertResult.first;
    }

    if (!note.resources() || note.resources()->isEmpty()) {
        noteGuidIndex.replace(noteIt, note);
        return result;
    }

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

        auto resourceItemData = putResource(resource);
        resource.setUpdateSequenceNum(resourceItemData.usn);
        result.resourceUsns[*resource.guid()] = resourceItemData.usn;
    }

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
    noteGuidIndex.replace(noteIt, std::move(note));
    return result;
}

std::optional<qevercloud::Note> FakeNoteStoreBackend::findNote(
    const qevercloud::Guid & guid) const
{
    const auto & index = m_notes.get<note_store::NoteByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void FakeNoteStoreBackend::removeNote(const qevercloud::Guid & guid)
{
    auto & index = m_notes.get<note_store::NoteByGuidTag>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return;
    }

    const auto & note = *it;
    if (note.resources() && !note.resources()->isEmpty()) {
        const auto resources = *note.resources();
        for (const auto & resource: std::as_const(resources)) {
            removeResource(resource.guid().value());
        }
    }

    index.erase(it);
}

QList<qevercloud::Note> FakeNoteStoreBackend::getNotesByConflictSourceNoteGuid(
    const qevercloud::Guid & conflictSourceNoteGuid) const
{
    const auto & index =
        m_notes.get<note_store::NoteByConflictSourceNoteGuidTag>();
    auto range = index.equal_range(conflictSourceNoteGuid);
    QList<qevercloud::Note> result;
    result.reserve(static_cast<int>(std::distance(range.first, range.second)));
    for (auto it = range.first; it != range.second; ++it) {
        result << *it;
    }
    return result;
}

void FakeNoteStoreBackend::putExpungedUserOwnNoteGuid(
    const qevercloud::Guid & guid)
{
    removeNote(guid);
    m_expungedUserOwnNoteGuidsAndUsns.insert(guid, ++m_userOwnMaxUsn);
}

bool FakeNoteStoreBackend::containsExpungedUserOwnNoteGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedUserOwnNoteGuidsAndUsns.contains(guid);
}

void FakeNoteStoreBackend::removeExpungedUserOwnNoteGuid(
    const qevercloud::Guid & guid)
{
    m_expungedUserOwnNoteGuidsAndUsns.remove(guid);
}

void FakeNoteStoreBackend::putExpungedLinkedNotebookNoteGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & noteGuid)
{
    removeNote(noteGuid);

    std::optional<qint32> maxUsn =
        currentLinkedNotebookMaxUsn(linkedNotebookGuid);
    if (!maxUsn) {
        maxUsn = 0;
    }

    m_expungedLinkedNotebookNoteGuidsAndUsns[linkedNotebookGuid].insert(
        noteGuid, ++(*maxUsn));

    setMaxUsn(*maxUsn, linkedNotebookGuid);
}

bool FakeNoteStoreBackend::containsExpungedLinkedNotebookNoteGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & noteGuid) const
{
    const auto it =
        m_expungedLinkedNotebookNoteGuidsAndUsns.constFind(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNoteGuidsAndUsns.constEnd()) {
        return false;
    }

    return it->contains(noteGuid);
}

void FakeNoteStoreBackend::removeExpungedLinkedNotebookNoteGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & noteGuid)
{
    const auto it =
        m_expungedLinkedNotebookNoteGuidsAndUsns.find(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNoteGuidsAndUsns.end()) {
        return;
    }

    if (!it->remove(noteGuid)) {
        return;
    }

    if (it->isEmpty()) {
        m_expungedLinkedNotebookNoteGuidsAndUsns.erase(it);
    }
}

QHash<qevercloud::Guid, qevercloud::Resource> FakeNoteStoreBackend::resources()
    const
{
    QHash<qevercloud::Guid, qevercloud::Resource> result;
    result.reserve(static_cast<int>(m_resources.size()));

    for (const auto & resource: m_resources) {
        Q_ASSERT(resource.guid());
        result[*resource.guid()] = resource;
    }

    return result;
}

FakeNoteStoreBackend::ItemData FakeNoteStoreBackend::putResource(
    qevercloud::Resource resource)
{
    if (!resource.noteGuid()) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Detected attempt to put resource without note guid")}};
    }

    const auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto noteIt = noteGuidIndex.find(*resource.noteGuid());
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Detected attempt to put resource without existing note")}};
    }

    Q_ASSERT(noteIt->notebookGuid());

    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();
    const auto notebookIt = notebookGuidIndex.find(*noteIt->notebookGuid());
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Could not find notebook corresponding to the note of the "
            "resource")}};
    }

    ItemData result;

    if (!resource.guid()) {
        result.guid = UidGenerator::Generate();
        resource.setGuid(result.guid);
    }

    std::optional<qint32> maxUsn = notebookIt->linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*notebookIt->linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (notebookIt->linkedNotebookGuid()) {
        if (!maxUsn) {
            maxUsn = 0;
        }
    }
    else if (!maxUsn) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Failed to find user own max USN on attempt to put resource")}};
    }

    ++(*maxUsn);
    resource.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebookIt->linkedNotebookGuid());
    result.usn = *maxUsn;

    auto & resourceGuidIndex = m_resources.get<note_store::ResourceByGuidTag>();
    auto resourceIt = resourceGuidIndex.find(*resource.guid());
    if (resourceIt == resourceGuidIndex.end()) {
        resourceGuidIndex.emplace(std::move(resource));
    }
    else {
        resourceGuidIndex.replace(resourceIt, std::move(resource));
    }

    return result;
}

std::optional<qevercloud::Resource> FakeNoteStoreBackend::findResource(
    const qevercloud::Guid & guid) const
{
    const auto & index = m_resources.get<note_store::ResourceByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void FakeNoteStoreBackend::removeResource(const qevercloud::Guid & guid)
{
    auto & index = m_resources.get<note_store::ResourceByGuidTag>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return;
    }

    const auto & noteGuid = it->noteGuid().value();
    auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto noteIt = noteGuidIndex.find(noteGuid);
    if (noteIt != noteGuidIndex.end()) {
        qevercloud::Note note{*noteIt};
        if (note.resources() && !note.resources()->isEmpty()) {
            auto resourceIt = std::find_if(
                note.mutableResources()->begin(),
                note.mutableResources()->end(),
                [resourceGuid =
                     it->guid()](const qevercloud::Resource & resource) {
                    return resource.guid() == resourceGuid;
                });
            if (resourceIt != note.mutableResources()->end()) {
                note.mutableResources()->erase(resourceIt);
            }
        }
        noteGuidIndex.replace(noteIt, note);
    }
    else {
        QNWARNING(
            "tests::synchronization::FakeNoteStoreBackend",
            "Found no note corresponding to the removed resource: " << *it);
    }

    index.erase(it);
}

QHash<qevercloud::Guid, qevercloud::LinkedNotebook>
    FakeNoteStoreBackend::linkedNotebooks() const
{
    QHash<qevercloud::Guid, qevercloud::LinkedNotebook> result;
    result.reserve(static_cast<int>(m_linkedNotebooks.size()));

    for (const auto & linkedNotebook: m_linkedNotebooks) {
        Q_ASSERT(linkedNotebook.guid());
        result[*linkedNotebook.guid()] = linkedNotebook;
    }

    return result;
}

FakeNoteStoreBackend::ItemData FakeNoteStoreBackend::putLinkedNotebook(
    qevercloud::LinkedNotebook linkedNotebook)
{
    if (!linkedNotebook.shardId() && !linkedNotebook.uri()) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Detected attempt to put linked notebook without either shard id "
            "or uri")}};
    }

    ItemData result;

    if (!linkedNotebook.guid()) {
        result.guid = UidGenerator::Generate();
        linkedNotebook.setGuid(result.guid);
    }

    if (!linkedNotebook.username()) {
        result.name = nextName(QStringLiteral("Linked notebook"));
        linkedNotebook.setUsername(result.name);
    }

    qint32 maxUsn = currentUserOwnMaxUsn();
    ++maxUsn;
    linkedNotebook.setUpdateSequenceNum(maxUsn);
    setMaxUsn(maxUsn);
    result.usn = maxUsn;

    removeExpungedLinkedNotebookGuid(*linkedNotebook.guid());

    auto & index = m_linkedNotebooks.get<note_store::LinkedNotebookByGuidTag>();
    const auto it = index.find(*linkedNotebook.guid());
    if (it == index.end()) {
        index.emplace(std::move(linkedNotebook));
    }
    else {
        index.replace(it, std::move(linkedNotebook));
    }

    return result;
}

std::optional<qevercloud::LinkedNotebook>
    FakeNoteStoreBackend::findLinkedNotebook(
        const qevercloud::Guid & guid) const
{
    const auto & index =
        m_linkedNotebooks.get<note_store::LinkedNotebookByGuidTag>();

    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void FakeNoteStoreBackend::removeLinkedNotebook(const qevercloud::Guid & guid)
{
    auto & index = m_linkedNotebooks.get<note_store::LinkedNotebookByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        index.erase(it);
    }
}

void FakeNoteStoreBackend::putExpungedLinkedNotebookGuid(
    const qevercloud::Guid & guid)
{
    removeLinkedNotebook(guid);
    m_expungedLinkedNotebookGuidsAndUsns.insert(guid, ++m_userOwnMaxUsn);
}

bool FakeNoteStoreBackend::containsExpungedLinkedNotebookGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedLinkedNotebookGuidsAndUsns.contains(guid);
}

void FakeNoteStoreBackend::removeExpungedLinkedNotebookGuid(
    const qevercloud::Guid & guid)
{
    m_expungedLinkedNotebookGuidsAndUsns.remove(guid);
}

qevercloud::SyncState FakeNoteStoreBackend::userOwnSyncState() const
{
    return m_userOwnSyncState;
}

void FakeNoteStoreBackend::putUserOwnSyncState(qevercloud::SyncState syncState)
{
    m_userOwnSyncState = std::move(syncState);
}

QHash<qevercloud::Guid, qevercloud::SyncState>
    FakeNoteStoreBackend::linkedNotebookSyncStates() const
{
    return m_linkedNotebookSyncStates;
}

void FakeNoteStoreBackend::putLinkedNotebookSyncState(
    const qevercloud::Guid & linkedNotebookGuid,
    qevercloud::SyncState syncState)
{
    m_linkedNotebookSyncStates[linkedNotebookGuid] = std::move(syncState);
}

std::optional<qevercloud::SyncState>
    FakeNoteStoreBackend::findLinkedNotebookSyncState(
        const qevercloud::Guid & linkedNotebookGuid) const
{
    if (const auto it =
            m_linkedNotebookSyncStates.constFind(linkedNotebookGuid);
        it != m_linkedNotebookSyncStates.constEnd())
    {
        return it.value();
    }

    return std::nullopt;
}

void FakeNoteStoreBackend::removeLinkedNotebookSyncState(
    const qevercloud::Guid & linkedNotebookGuid)
{
    m_linkedNotebookSyncStates.remove(linkedNotebookGuid);
}

void FakeNoteStoreBackend::clearLinkedNotebookSyncStates()
{
    m_linkedNotebookSyncStates.clear();
}

qint32 FakeNoteStoreBackend::currentUserOwnMaxUsn() const noexcept
{
    return m_userOwnMaxUsn;
}

std::optional<qint32> FakeNoteStoreBackend::currentLinkedNotebookMaxUsn(
    const qevercloud::Guid & linkedNotebookGuid) const noexcept
{
    if (const auto it = m_linkedNotebookMaxUsns.constFind(linkedNotebookGuid);
        it != m_linkedNotebookMaxUsns.constEnd())
    {
        return it.value();
    }

    return std::nullopt;
}

std::optional<
    std::pair<StopSynchronizationErrorTrigger, StopSynchronizationError>>
    FakeNoteStoreBackend::stopSynchronizationError() const
{
    if (!m_stopSynchronizationErrorData) {
        return std::nullopt;
    }

    return std::make_pair(
        m_stopSynchronizationErrorData->trigger,
        m_stopSynchronizationErrorData->error);
}

void FakeNoteStoreBackend::setStopSynchronizationError(
    const StopSynchronizationErrorTrigger trigger,
    const StopSynchronizationError error)
{
    m_stopSynchronizationErrorData =
        StopSynchronizationErrorData{trigger, error};
}

void FakeNoteStoreBackend::clearStopSynchronizationError()
{
    m_stopSynchronizationErrorData.reset();
}

quint32 FakeNoteStoreBackend::maxNumSavedSearches() const noexcept
{
    return m_maxNumSavedSearches;
}

void FakeNoteStoreBackend::setMaxNumSavedSearches(
    const quint32 maxNumSavedSearches) noexcept
{
    m_maxNumSavedSearches = maxNumSavedSearches;
}

quint32 FakeNoteStoreBackend::maxNumTags() const noexcept
{
    return m_maxNumTags;
}

void FakeNoteStoreBackend::setMaxNumTags(const quint32 maxNumTags) noexcept
{
    m_maxNumTags = maxNumTags;
}

quint32 FakeNoteStoreBackend::maxNumNotebooks() const noexcept
{
    return m_maxNumNotebooks;
}

void FakeNoteStoreBackend::setMaxNumNotebooks(
    const quint32 maxNumNotebooks) noexcept
{
    m_maxNumNotebooks = maxNumNotebooks;
}

quint32 FakeNoteStoreBackend::maxNumNotes() const noexcept
{
    return m_maxNumNotes;
}

void FakeNoteStoreBackend::setMaxNumNotes(const quint32 maxNumNotes) noexcept
{
    m_maxNumNotes = maxNumNotes;
}

quint64 FakeNoteStoreBackend::maxNoteSize() const noexcept
{
    return m_maxNoteSize;
}

void FakeNoteStoreBackend::setMaxNoteSize(quint64 maxNoteSize) noexcept
{
    m_maxNoteSize = maxNoteSize;
}

quint32 FakeNoteStoreBackend::maxNumResourcesPerNote() const noexcept
{
    return m_maxNumResourcesPerNote;
}

void FakeNoteStoreBackend::setMaxNumResourcesPerNote(
    const quint32 maxNumResourcesPerNote) noexcept
{
    m_maxNumResourcesPerNote = maxNumResourcesPerNote;
}

quint32 FakeNoteStoreBackend::maxNumTagsPerNote() const noexcept
{
    return m_maxNumTagsPerNote;
}

void FakeNoteStoreBackend::setMaxNumTagsPerNote(
    const quint32 maxNumTagsPerNote) noexcept
{
    m_maxNumTagsPerNote = maxNumTagsPerNote;
}

quint64 FakeNoteStoreBackend::maxResourceSize() const noexcept
{
    return m_maxResourceSize;
}

void FakeNoteStoreBackend::setMaxResourceSize(
    const quint64 maxResourceSize) noexcept
{
    m_maxResourceSize = maxResourceSize;
}

QHash<qevercloud::Guid, QString>
    FakeNoteStoreBackend::linkedNotebookAuthTokensByGuid() const
{
    return m_linkedNotebookAuthTokensByGuid;
}

void FakeNoteStoreBackend::setLinkedNotebookAuthTokensByGuid(
    QHash<qevercloud::Guid, QString> tokens)
{
    m_linkedNotebookAuthTokensByGuid = std::move(tokens);
}

void FakeNoteStoreBackend::setUriForRequestId(
    const QUuid requestId, QByteArray uri)
{
    m_uriByRequestId[requestId] = std::move(uri);
}

void FakeNoteStoreBackend::removeUriForRequestId(const QUuid requestId)
{
    m_uriByRequestId.remove(requestId);
}

void FakeNoteStoreBackend::onCreateNotebookRequest(
    qevercloud::Notebook notebook, const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateNotebook)
    {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (m_notebooks.size() + 1 > m_maxNumNotebooks) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("Notebook"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (auto exc = note_store::checkNotebook(notebook)) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{}, std::make_exception_ptr(std::move(*exc)),
            ctx->requestId());
        return;
    }

    // NOTE: notebook's linkedNotebookGuid field is not serialized on thrift
    // level and thus it won't be propagated inside the notebook to
    // FakeNoteStoreBackend. Instead linkedNotebookGuid is encoded in the
    // request's uri.
    if (const auto it = m_uriByRequestId.find(ctx->requestId());
        it != m_uriByRequestId.end())
    {
        auto linkedNotebookGuid = QString::fromUtf8(it.value());
        if (auto exc =
                checkLinkedNotebookAuthentication(linkedNotebookGuid, ctx))
        {
            Q_EMIT createNotebookRequestReady(
                qevercloud::Notebook{}, std::move(exc), ctx->requestId());
            return;
        }

        notebook.setLinkedNotebookGuid(std::move(linkedNotebookGuid));
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{}, std::move(exc), ctx->requestId());
        return;
    }

    if (notebook.linkedNotebookGuid() &&
        notebook.defaultNotebook().value_or(false))
    {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::PERMISSION_DENIED,
                QStringLiteral("Notebook.defaultNotebook"))),
            ctx->requestId());
        return;
    }

    auto & nameIndex = m_notebooks.get<note_store::NotebookByNameUpperTag>();
    const auto nameIt = nameIndex.find(notebook.name()->toUpper());
    if (nameIt != nameIndex.end()) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Notebook.name"))),
            ctx->requestId());
        return;
    }

    notebook.setGuid(UidGenerator::Generate());

    std::optional<qint32> maxUsn = notebook.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*notebook.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (Q_UNLIKELY(!maxUsn)) {
        // Evernote API reference doesn't really specify what would happen on
        // attempt to create a notebook not corresponding to a known linked
        // notebook so improvising
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Notebook"))),
            ctx->requestId());
        return;
    }

    ++(*maxUsn);
    notebook.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    m_notebooks.insert(notebook);
    Q_EMIT createNotebookRequestReady(
        std::move(notebook), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onUpdateNotebookRequest(
    qevercloud::Notebook notebook, const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateNotebook)
    {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (!notebook.guid()) {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Notebook.guid"))),
            ctx->requestId());
        return;
    }

    if (auto exc = note_store::checkNotebook(notebook)) {
        Q_EMIT updateNotebookRequestReady(
            0, std::make_exception_ptr(std::move(*exc)), ctx->requestId());
        return;
    }

    // NOTE: notebook's linkedNotebookGuid field is not serialized on thrift
    // level and thus it won't be propagated inside the notebook to
    // FakeNoteStoreBackend. Instead linkedNotebookGuid is encoded in the
    // request's uri.
    if (const auto it = m_uriByRequestId.find(ctx->requestId());
        it != m_uriByRequestId.end())
    {
        auto linkedNotebookGuid = QString::fromUtf8(it.value());
        if (auto exc =
                checkLinkedNotebookAuthentication(linkedNotebookGuid, ctx))
        {
            Q_EMIT updateNotebookRequestReady(
                0, std::move(exc), ctx->requestId());
            return;
        }

        notebook.setLinkedNotebookGuid(std::move(linkedNotebookGuid));
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateNotebookRequestReady(0, std::move(exc), ctx->requestId());
        return;
    }

    if (notebook.linkedNotebookGuid() &&
        notebook.defaultNotebook().value_or(false))
    {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::PERMISSION_DENIED,
                QStringLiteral("Notebook.defaultNotebook"))),
            ctx->requestId());
        return;
    }

    auto & index = m_notebooks.get<note_store::NotebookByGuidTag>();
    const auto it = index.find(*notebook.guid());
    if (it == index.end()) {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Notebook.guid"), *notebook.guid())),
            ctx->requestId());
        return;
    }

    const auto & originalNotebook = *it;
    if (originalNotebook.restrictions() &&
        originalNotebook.restrictions()->noUpdateNotebook() &&
        *originalNotebook.restrictions()->noUpdateNotebook())
    {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::PERMISSION_DENIED,
                QStringLiteral("Notebook"))),
            ctx->requestId());
        return;
    }

    if (originalNotebook.name().value().toUpper() != notebook.name()->toUpper())
    {
        auto & nameIndex =
            m_notebooks.get<note_store::NotebookByNameUpperTag>();

        const auto nameIt = nameIndex.find(notebook.name()->toUpper());
        if (nameIt != nameIndex.end()) {
            Q_EMIT updateNotebookRequestReady(
                0,
                std::make_exception_ptr(utils::createUserException(
                    qevercloud::EDAMErrorCode::DATA_CONFLICT,
                    QStringLiteral("Notebook.name"))),
                ctx->requestId());
            return;
        }
    }

    std::optional<qint32> maxUsn = notebook.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*notebook.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (Q_UNLIKELY(!maxUsn)) {
        // Evernote API reference doesn't really specify what would happen on
        // attempt to update a notebook not corresponding to a known linked
        // notebook so improvising
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Notebook"))),
            ctx->requestId());
        return;
    }

    ++(*maxUsn);
    notebook.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    index.replace(it, std::move(notebook));
    Q_EMIT updateNotebookRequestReady(*maxUsn, nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onCreateNoteRequest(
    qevercloud::Note note, const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateNote)
    {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (m_notes.size() + 1 > m_maxNumNotes) {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("Note"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (Q_UNLIKELY(!note.notebookGuid())) {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.notebookGuid"))
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    const auto notebookIt = notebookGuidIndex.find(*note.notebookGuid());
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.notebookGuid"))
                    .setKey(*note.notebookGuid())
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & notebook = *notebookIt;
    if (notebook.restrictions() &&
        notebook.restrictions()->noCreateNotes().value_or(false))
    {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::PERMISSION_DENIED)
                    .setMessage(QStringLiteral(
                        "Cannot create note due to notebook restrictions"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (auto exc = note_store::checkNote(
            note, m_maxNumResourcesPerNote, m_maxNumTagsPerNote))
    {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{}, std::make_exception_ptr(std::move(*exc)),
            ctx->requestId());
        return;
    }

    if (notebook.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebook.linkedNotebookGuid(), ctx))
        {
            Q_EMIT createNoteRequestReady(
                qevercloud::Note{}, std::move(exc), ctx->requestId());
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{}, std::move(exc), ctx->requestId());
        return;
    }

    std::optional<qint32> maxUsn = notebook.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*notebook.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (Q_UNLIKELY(!maxUsn)) {
        // Evernote API reference doesn't really specify what would happen on
        // attempt to create a note not corresponding to a known linked
        // notebook so improvising
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Note"))),
            ctx->requestId());
        return;
    }

    note.setGuid(UidGenerator::Generate());
    if (note.resources() && !note.resources()->isEmpty()) {
        for (auto & resource: *note.mutableResources()) {
            resource.setGuid(UidGenerator::Generate());
            resource.setNoteGuid(note.guid());
            ++(*maxUsn);
            resource.setUpdateSequenceNum(maxUsn);
            setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());
        }
    }

    ++(*maxUsn);
    note.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    m_notes.insert(note);
    if (note.resources() && !note.resources()->isEmpty()) {
        for (const auto & resource: std::as_const(*note.resources())) {
            m_resources.insert(resource);
        }
    }

    Q_EMIT createNoteRequestReady(
        noteMetadata(std::move(note)), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onUpdateNoteRequest(
    qevercloud::Note note, const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateNote)
    {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (Q_UNLIKELY(!note.guid())) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto noteIt = noteGuidIndex.find(*note.guid());
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.guid"))
                    .setKey(*note.guid())
                    .build()),
            ctx->requestId());
    }

    if (Q_UNLIKELY(!note.notebookGuid())) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.notebookGuid"))
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    const auto notebookIt = notebookGuidIndex.find(*note.notebookGuid());
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.notebookGuid"))
                    .setKey(*note.notebookGuid())
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & notebook = *notebookIt;
    if (notebook.restrictions() &&
        notebook.restrictions()->noUpdateNotes().value_or(false))
    {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::PERMISSION_DENIED)
                    .setMessage(QStringLiteral(
                        "Cannot update note due to notebook restrictions"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (auto exc = note_store::checkNote(
            note, m_maxNumResourcesPerNote, m_maxNumTagsPerNote))
    {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{}, std::make_exception_ptr(std::move(*exc)),
            ctx->requestId());
        return;
    }

    if (notebook.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebook.linkedNotebookGuid(), ctx))
        {
            Q_EMIT updateNoteRequestReady(
                qevercloud::Note{}, std::move(exc), ctx->requestId());
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{}, std::move(exc), ctx->requestId());
        return;
    }

    std::optional<qint32> maxUsn = notebook.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*notebook.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (Q_UNLIKELY(!maxUsn)) {
        // Evernote API reference doesn't really specify what would happen on
        // attempt to update a note not corresponding to a known linked
        // notebook so improvising
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Note"))),
            ctx->requestId());
        return;
    }

    qint32 localMaxUsn = *maxUsn;

    if (note.resources() && !note.resources()->isEmpty()) {
        using ResourceIt = note_store::ResourcesByGuid::iterator;
        QList<std::pair<ResourceIt, qevercloud::Resource>> updatedResources;

        auto & resourceGuidIndex =
            m_resources.get<note_store::ResourceByGuidTag>();

        for (auto & resource: *note.mutableResources()) {
            if (!resource.guid()) {
                Q_EMIT updateNoteRequestReady(
                    qevercloud::Note{},
                    std::make_exception_ptr(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                            .setMessage(QStringLiteral(
                                "Creation of new resources within the note "
                                "is not supported in this test environment"))
                            .build()),
                    ctx->requestId());
                return;
            }

            auto resourceIt = resourceGuidIndex.find(*resource.guid());
            if (Q_UNLIKELY(resourceIt == resourceGuidIndex.end())) {
                Q_EMIT updateNoteRequestReady(
                    qevercloud::Note{},
                    std::make_exception_ptr(
                        qevercloud::EDAMSystemExceptionBuilder{}
                            .setErrorCode(
                                qevercloud::EDAMErrorCode::DATA_CONFLICT)
                            .setMessage(QStringLiteral(
                                "Could not find updated note's resource by "
                                "guid"))
                            .build()),
                    ctx->requestId());
                return;
            }

            resource.setUpdateSequenceNum(++localMaxUsn);
            updatedResources << std::make_pair(resourceIt, resource);
        }

        for (const auto & pair: std::as_const(updatedResources)) {
            resourceGuidIndex.replace(pair.first, pair.second);
        }
    }

    *maxUsn = localMaxUsn;

    note.setUpdateSequenceNum(++(*maxUsn));
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    noteGuidIndex.replace(noteIt, note);
    Q_EMIT updateNoteRequestReady(
        noteMetadata(std::move(note)), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onCreateTagRequest(
    qevercloud::Tag tag, const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateTag)
    {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (m_tags.size() + 1 > m_maxNumTags) {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("Tag"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (tag.parentGuid()) {
        const auto & tagGuidIndex = m_tags.get<note_store::TagByGuidTag>();
        const auto tagIt = tagGuidIndex.find(*tag.parentGuid());
        if (Q_UNLIKELY(tagIt == tagGuidIndex.end())) {
            Q_EMIT createTagRequestReady(
                qevercloud::Tag{},
                std::make_exception_ptr(
                    qevercloud::EDAMNotFoundExceptionBuilder{}
                        .setIdentifier(QStringLiteral("Tag.parentGuid"))
                        .setKey(*tag.parentGuid())
                        .build()),
                ctx->requestId());
            return;
        }
    }

    if (auto exc = note_store::checkTag(tag)) {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{}, std::make_exception_ptr(std::move(*exc)),
            ctx->requestId());
        return;
    }

    Q_ASSERT(tag.name());

    const auto & name = *tag.name();

    const auto & tagNameIndex = m_tags.get<note_store::TagByNameUpperTag>();
    const auto tagNameIt = tagNameIndex.find(name.toUpper());
    if (Q_UNLIKELY(tagNameIt != tagNameIndex.end())) {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::DATA_CONFLICT)
                    .setParameter(QStringLiteral("Tag.name"))
                    .build()),
            ctx->requestId());
        return;
    }

    // NOTE: tag's linkedNotebookGuid field is not serialized on thrift level
    // and thus it won't be propagated inside the tag to FakeNoteStoreBackend.
    // Instead linkedNotebookGuid is encoded in the request's uri.
    if (const auto it = m_uriByRequestId.find(ctx->requestId());
        it != m_uriByRequestId.end())
    {
        auto linkedNotebookGuid = QString::fromUtf8(it.value());
        if (auto exc =
                checkLinkedNotebookAuthentication(linkedNotebookGuid, ctx))
        {
            Q_EMIT createTagRequestReady(
                qevercloud::Tag{}, std::move(exc), ctx->requestId());
            return;
        }

        tag.setLinkedNotebookGuid(std::move(linkedNotebookGuid));
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{}, std::move(exc), ctx->requestId());
        return;
    }

    tag.setGuid(UidGenerator::Generate());

    std::optional<qint32> maxUsn = tag.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*tag.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (Q_UNLIKELY(!maxUsn)) {
        // Evernote API reference doesn't really specify what would happen on
        // attempt to create a tag not corresponding to a known linked
        // notebook so improvising
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Tag"))),
            ctx->requestId());
        return;
    }

    ++(*maxUsn);
    tag.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, tag.linkedNotebookGuid());

    m_tags.insert(tag);
    Q_EMIT createTagRequestReady(std::move(tag), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onUpdateTagRequest(
    qevercloud::Tag tag, const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateTag)
    {
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (Q_UNLIKELY(!tag.guid())) {
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Tag.guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    auto & tagGuidIndex = m_tags.get<note_store::TagByGuidTag>();
    const auto tagIt = tagGuidIndex.find(*tag.guid());
    if (Q_UNLIKELY(tagIt == tagGuidIndex.end())) {
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Tag.guid"))
                    .setKey(*tag.guid())
                    .build()),
            ctx->requestId());
    }

    if (tag.parentGuid()) {
        const auto & tagGuidIndex = m_tags.get<note_store::TagByGuidTag>();
        const auto tagIt = tagGuidIndex.find(*tag.parentGuid());
        if (Q_UNLIKELY(tagIt == tagGuidIndex.end())) {
            Q_EMIT updateTagRequestReady(
                0,
                std::make_exception_ptr(
                    qevercloud::EDAMNotFoundExceptionBuilder{}
                        .setIdentifier(QStringLiteral("Tag.parentGuid"))
                        .setKey(*tag.parentGuid())
                        .build()),
                ctx->requestId());
            return;
        }
    }

    if (auto exc = note_store::checkTag(tag)) {
        Q_EMIT updateTagRequestReady(
            0, std::make_exception_ptr(std::move(*exc)), ctx->requestId());
        return;
    }

    Q_ASSERT(tag.name());

    const auto & name = *tag.name();

    const auto & tagNameIndex = m_tags.get<note_store::TagByNameUpperTag>();
    const auto tagNameIt = tagNameIndex.find(name.toUpper());
    if (Q_UNLIKELY(
            tagNameIt != tagNameIndex.end() && tagNameIt->guid() != tag.guid()))
    {
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::DATA_CONFLICT)
                    .setParameter(QStringLiteral("Tag.name"))
                    .build()),
            ctx->requestId());
        return;
    }

    // NOTE: tag's linkedNotebookGuid field is not serialized on thrift level
    // and thus it won't be propagated inside the tag to FakeNoteStoreBackend.
    // Instead linkedNotebookGuid is encoded in the request's uri.
    if (const auto it = m_uriByRequestId.find(ctx->requestId());
        it != m_uriByRequestId.end())
    {
        auto linkedNotebookGuid = QString::fromUtf8(it.value());
        if (auto exc =
                checkLinkedNotebookAuthentication(linkedNotebookGuid, ctx))
        {
            Q_EMIT updateTagRequestReady(0, std::move(exc), ctx->requestId());
            return;
        }

        tag.setLinkedNotebookGuid(std::move(linkedNotebookGuid));
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateTagRequestReady(0, std::move(exc), ctx->requestId());
        return;
    }

    std::optional<qint32> maxUsn = tag.linkedNotebookGuid()
        ? currentLinkedNotebookMaxUsn(*tag.linkedNotebookGuid())
        : std::make_optional(currentUserOwnMaxUsn());

    if (Q_UNLIKELY(!maxUsn)) {
        // Evernote API reference doesn't really specify what would happen on
        // attempt to create a tag not corresponding to a known linked
        // notebook so improvising
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Tag"))),
            ctx->requestId());
        return;
    }

    ++(*maxUsn);
    tag.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, tag.linkedNotebookGuid());

    tagGuidIndex.replace(tagIt, tag);
    Q_EMIT updateTagRequestReady(*maxUsn, nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onCreateSavedSearchRequest(
    qevercloud::SavedSearch search, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateSavedSearch)
    {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (m_savedSearches.size() + 1 > m_maxNumSavedSearches) {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("SavedSearch"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (auto exc = note_store::checkSavedSearch(search)) {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{}, std::make_exception_ptr(std::move(*exc)),
            ctx->requestId());
        return;
    }

    Q_ASSERT(search.name());

    const auto & name = *search.name();

    const auto & savedSearchNameIndex =
        m_savedSearches.get<note_store::SavedSearchByNameUpperTag>();
    const auto savedSearchNameIt = savedSearchNameIndex.find(name.toUpper());
    if (Q_UNLIKELY(savedSearchNameIt != savedSearchNameIndex.end())) {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::DATA_CONFLICT)
                    .setParameter(QStringLiteral("SavedSearch.name"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{}, std::move(exc), ctx->requestId());
        return;
    }

    search.setGuid(UidGenerator::Generate());

    auto maxUsn = currentUserOwnMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNum(maxUsn);
    setMaxUsn(maxUsn);

    m_savedSearches.insert(search);
    Q_EMIT createSavedSearchRequestReady(
        std::move(search), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onUpdateSavedSearchRequest(
    qevercloud::SavedSearch search, const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateSavedSearch)
    {
        Q_EMIT updateSavedSearchRequestReady(
            0,
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (Q_UNLIKELY(!search.guid())) {
        Q_EMIT updateSavedSearchRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("SavedSearch.guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    auto & savedSearchGuidIndex =
        m_savedSearches.get<note_store::SavedSearchByGuidTag>();

    const auto savedSearchIt = savedSearchGuidIndex.find(*search.guid());
    if (Q_UNLIKELY(savedSearchIt == savedSearchGuidIndex.end())) {
        Q_EMIT updateSavedSearchRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("SavedSearch.guid"))
                    .setKey(*search.guid())
                    .build()),
            ctx->requestId());
        return;
    }

    if (auto exc = note_store::checkSavedSearch(search)) {
        Q_EMIT updateSavedSearchRequestReady(
            0, std::make_exception_ptr(std::move(*exc)), ctx->requestId());
        return;
    }

    Q_ASSERT(search.name());

    const auto & name = *search.name();

    const auto & savedSearchNameIndex =
        m_savedSearches.get<note_store::SavedSearchByNameUpperTag>();
    const auto savedSearchNameIt = savedSearchNameIndex.find(name.toUpper());
    if (Q_UNLIKELY(
            savedSearchNameIt != savedSearchNameIndex.end() &&
            savedSearchNameIt->guid() != search.guid()))
    {
        Q_EMIT updateSavedSearchRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::DATA_CONFLICT)
                    .setParameter(QStringLiteral("SavedSearch.name"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateSavedSearchRequestReady(
            0, std::move(exc), ctx->requestId());
        return;
    }

    auto maxUsn = currentUserOwnMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNum(maxUsn);
    setMaxUsn(maxUsn);

    savedSearchGuidIndex.replace(savedSearchIt, search);
    Q_EMIT updateSavedSearchRequestReady(maxUsn, nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onGetSyncStateRequest(
    const qevercloud::IRequestContextPtr & ctx)
{
    QNDEBUG(
        "tests::synchronization::FakeNoteStoreBackend",
        "FakeNoteStoreBackend::onGetSyncStateRequest");

    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetUserOwnSyncState)
    {
        QNDEBUG(
            "tests::synchronization::FakeNoteStoreBackend",
            "Triggering stop synchronization error");

        Q_EMIT getSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getSyncStateRequestReady(
            qevercloud::SyncState{}, std::move(exc), ctx->requestId());
        return;
    }

    Q_EMIT getSyncStateRequestReady(
        m_userOwnSyncState, nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onGetLinkedNotebookSyncStateRequest(
    const qevercloud::LinkedNotebook & linkedNotebook,
    const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetLinkedNotebookSyncState)
    {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{}, std::move(exc), ctx->requestId());
        return;
    }

    if (!linkedNotebook.username()) {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_REQUIRED,
                QStringLiteral("LinkedNotebook.username"))),
            ctx->requestId());
        return;
    }

    const auto & username = *linkedNotebook.username();

    const auto & linkedNotebookUsernameIndex =
        m_linkedNotebooks.get<note_store::LinkedNotebookByUsernameTag>();

    const auto linkedNotebookIt = linkedNotebookUsernameIndex.find(username);
    if (Q_UNLIKELY(linkedNotebookIt == linkedNotebookUsernameIndex.end())) {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("LinkedNotebook.username"))
                    .setKey(username)
                    .build()),
            ctx->requestId());
        return;
    }

    Q_ASSERT(linkedNotebookIt->guid());

    const auto & guid = *linkedNotebookIt->guid();
    const auto it = m_linkedNotebookSyncStates.constFind(guid);
    if (Q_UNLIKELY(it == m_linkedNotebookSyncStates.constEnd())) {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("LinkedNotebook.username"))
                    .setKey(username)
                    .build()),
            ctx->requestId());
        return;
    }

    Q_EMIT getLinkedNotebookSyncStateRequestReady(
        it.value(), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onGetFilteredSyncChunkRequest(
    const qint32 afterUSN, const qint32 maxEntries,
    const qevercloud::SyncChunkFilter & filter,
    const qevercloud::IRequestContextPtr & ctx)
{
    QNDEBUG(
        "tests::synchronization::FakeNoteStoreBackend",
        "FakeNoteStoreBackend::onGetFilteredSyncChunkRequest: afterUsn = "
            << afterUSN << ", max entries = " << maxEntries);

    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetUserOwnSyncChunk)
    {
        Q_EMIT getFilteredSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (m_lastServedUserOwnSyncChunkHighUsn >= 0 &&
        afterUSN < m_lastServedUserOwnSyncChunkHighUsn)
    {
        QNWARNING(
            "tests::synchronization::FakeNoteStoreBackend",
            "Detected request of already served user own sync chunk data: "
                << "after usn = " << afterUSN << ", last served user own "
                << "sync chunk high usn = "
                << m_lastServedUserOwnSyncChunkHighUsn);
        throw RuntimeError{ErrorString{QStringLiteral(
            "Detected request of already served user own sync chunk data")}};
    }

    auto result = getSyncChunkImpl(
        afterUSN, maxEntries, (afterUSN == 0), std::nullopt, filter, ctx);

    if (result.first.chunkHighUSN()) {
        m_lastServedUserOwnSyncChunkHighUsn = *result.first.chunkHighUSN();
    }

    Q_EMIT getFilteredSyncChunkRequestReady(
        std::move(result.first), std::move(result.second), ctx->requestId());
}

void FakeNoteStoreBackend::onGetLinkedNotebookSyncChunkRequest(
    const qevercloud::LinkedNotebook & linkedNotebook, const qint32 afterUSN,
    const qint32 maxEntries, const bool fullSyncOnly,
    const qevercloud::IRequestContextPtr & ctx)
{
    QNDEBUG(
        "tests::synchronization::FakeNoteStoreBackend",
        "FakeNoteStoreBackend::onGetLinkedNotebookSyncChunkRequest: afterUsn = "
            << afterUSN << ", max entries = " << maxEntries
            << ", linked notebook guid = "
            << linkedNotebook.guid().value_or(QStringLiteral("<none>")));

    Q_ASSERT(ctx);

    m_onceGetLinkedNotebookSyncChunkCalled = true;

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetLinkedNotebookSyncChunk)
    {
        Q_EMIT getLinkedNotebookSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (!linkedNotebook.guid()) {
        Q_EMIT getLinkedNotebookSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("LinkedNotebook.guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (!linkedNotebook.username()) {
        Q_EMIT getLinkedNotebookSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("LinkedNotebook.username"))
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & username = *linkedNotebook.username();

    const auto & linkedNotebookNameIndex =
        m_linkedNotebooks.get<note_store::LinkedNotebookByUsernameTag>();

    const auto it = linkedNotebookNameIndex.find(username);
    if (Q_UNLIKELY(it == linkedNotebookNameIndex.end())) {
        Q_EMIT getLinkedNotebookSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("LinkedNotebook"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (const auto it = m_lastServedLinkedNotebookSyncChunkHighUsns.constFind(
            *linkedNotebook.guid());
        it != m_lastServedLinkedNotebookSyncChunkHighUsns.constEnd() &&
        afterUSN < it.value())
    {
        QNWARNING(
            "tests::synchronization::FakeNoteStoreBackend",
            "Detected request of already served linked notebook sync chunk "
                << "data: after usn = " << afterUSN << ", last served linked "
                << "notebook sync chunk high usn = " << it.value()
                << ", linked notebook guid = " << *linkedNotebook.guid());
        throw RuntimeError{ErrorString{QStringLiteral(
            "Detected request of already served linked notebook sync chunk "
            "data")}};
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

    auto result = getSyncChunkImpl(
        afterUSN, maxEntries, (afterUSN == 0), *linkedNotebook.guid(), filter,
        ctx);

    if (result.first.chunkHighUSN()) {
        m_lastServedLinkedNotebookSyncChunkHighUsns[*linkedNotebook.guid()] =
            *result.first.chunkHighUSN();
    }

    Q_EMIT getLinkedNotebookSyncChunkRequestReady(
        std::move(result.first), std::move(result.second), ctx->requestId());
}

void FakeNoteStoreBackend::onGetNoteWithResultSpecRequest(
    const qevercloud::Guid & guid,
    const qevercloud::NoteResultSpec & resultSpec,
    const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData) {
        if (m_onceGetLinkedNotebookSyncChunkCalled) {
            // Downloading note from a linked notebook
            if (m_stopSynchronizationErrorData->trigger ==
                StopSynchronizationErrorTrigger::
                    OnGetNoteAfterDownloadingLinkedNotebookSyncChunks)
            {
                Q_EMIT getNoteWithResultSpecRequestReady(
                    qevercloud::Note{},
                    std::make_exception_ptr(utils::createStopSyncException(
                        m_stopSynchronizationErrorData->error)),
                    ctx->requestId());
                return;
            }
        }
        else if (
            m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::
                OnGetNoteAfterDownloadingUserOwnSyncChunks)
        {
            Q_EMIT getNoteWithResultSpecRequestReady(
                qevercloud::Note{},
                std::make_exception_ptr(utils::createStopSyncException(
                    m_stopSynchronizationErrorData->error)),
                ctx->requestId());
            return;
        }
    }

    if (guid.isEmpty()) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Note.guid"))),
            ctx->requestId());
        return;
    }

    const auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto noteIt = noteGuidIndex.find(guid);
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Note.guid"), guid)),
            ctx->requestId());
        return;
    }

    if (Q_UNLIKELY(!noteIt->notebookGuid())) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(
                        QStringLiteral("Detected note without notebook guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    const auto notebookIt = notebookGuidIndex.find(*noteIt->notebookGuid());
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(
                        QStringLiteral("Detected note from unknown notebook"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (notebookIt->linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebookIt->linkedNotebookGuid(), ctx))
        {
            Q_EMIT getNoteWithResultSpecRequestReady(
                qevercloud::Note{}, std::move(exc), ctx->requestId());
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{}, std::move(exc), ctx->requestId());
        return;
    }

    if (m_onceServedNoteGuids.contains(guid)) {
        QTest::qFail(
            "Detected attempt to download the same note twice", __FILE__,
            __LINE__);
    }

    auto note = *noteIt;

    note.setLocalId(QString{});
    note.setLocalData({});
    note.setLocalOnly(false);
    note.setLocallyModified(false);
    note.setLocallyFavorited(false);
    note.setTagLocalIds(QStringList{});
    note.setNotebookLocalId(QString{});

    if (!resultSpec.includeContent().value_or(false)) {
        note.setContent(std::nullopt);
    }

    if (note.resources() && !note.resources()->isEmpty()) {
        const auto & resourceGuidIndex =
            m_resources.get<note_store::ResourceByGuidTag>();

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

            resource.setLocalId(UidGenerator::Generate());
            resource.setLocalData({});
            resource.setLocalOnly(false);
            resource.setLocallyModified(false);
            resource.setLocallyFavorited(false);
            resource.setNoteLocalId(QString{});

            if (!resultSpec.includeResourcesData().value_or(false) &&
                resource.data())
            {
                resource.mutableData()->setBody(std::nullopt);
            }

            if (!resultSpec.includeResourcesRecognition().value_or(false) &&
                resource.recognition())
            {
                resource.mutableRecognition()->setBody(std::nullopt);
            }

            if (!resultSpec.includeResourcesAlternateData().value_or(false) &&
                resource.alternateData())
            {
                resource.mutableAlternateData()->setBody(std::nullopt);
            }

            ++it;
        }

        note.setResources(resources);
    }

    m_onceServedNoteGuids.insert(guid);

    Q_EMIT getNoteWithResultSpecRequestReady(
        std::move(note), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onGetResourceRequest(
    const qevercloud::Guid & guid, bool withData, bool withRecognition,
    bool withAttributes, bool withAlternateData,
    const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData) {
        if (m_onceGetLinkedNotebookSyncChunkCalled) {
            // Downloading resource from a linked notebook
            if (m_stopSynchronizationErrorData->trigger ==
                StopSynchronizationErrorTrigger::
                    OnGetResourceAfterDownloadingLinkedNotebookSyncChunks)
            {
                Q_EMIT getResourceRequestReady(
                    qevercloud::Resource{},
                    std::make_exception_ptr(utils::createStopSyncException(
                        m_stopSynchronizationErrorData->error)),
                    ctx->requestId());
                return;
            }
        }
        else if (
            m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::
                OnGetResourceAfterDownloadingUserOwnSyncChunks)
        {
            Q_EMIT getResourceRequestReady(
                qevercloud::Resource{},
                std::make_exception_ptr(utils::createStopSyncException(
                    m_stopSynchronizationErrorData->error)),
                ctx->requestId());
            return;
        }
    }

    if (guid.isEmpty()) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Resource.guid"))),
            ctx->requestId());
        return;
    }

    const auto & resourceGuidIndex =
        m_resources.get<note_store::ResourceByGuidTag>();

    const auto resourceIt = resourceGuidIndex.find(guid);
    if (Q_UNLIKELY(resourceIt == resourceGuidIndex.end())) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Resource.guid"), guid)),
            ctx->requestId());
        return;
    }

    if (Q_UNLIKELY(!resourceIt->noteGuid())) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(
                        QStringLiteral("Detected resource without note guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto noteIt = noteGuidIndex.find(*resourceIt->noteGuid());
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(QStringLiteral(
                        "Detected resource without corresponding note"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (Q_UNLIKELY(!noteIt->notebookGuid())) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(
                        QStringLiteral("Detected note without notebook guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    const auto notebookIt = notebookGuidIndex.find(*noteIt->notebookGuid());
    if (Q_UNLIKELY(notebookIt == notebookGuidIndex.end())) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(
                        QStringLiteral("Detected note from unknown notebook"))
                    .build()),
            ctx->requestId());
        return;
    }

    if (notebookIt->linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebookIt->linkedNotebookGuid(), ctx))
        {
            Q_EMIT getResourceRequestReady(
                qevercloud::Resource{}, std::move(exc), ctx->requestId());
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{}, std::move(exc), ctx->requestId());
        return;
    }

    auto resource = *resourceIt;

    resource.setLocalId(UidGenerator::Generate());
    resource.setLocalData({});
    resource.setLocalOnly(false);
    resource.setLocallyModified(false);
    resource.setLocallyFavorited(false);
    resource.setNoteLocalId(QString{});

    if (!withData && resource.data()) {
        resource.mutableData()->setBody(std::nullopt);
    }

    if (!withRecognition && resource.recognition()) {
        resource.mutableRecognition()->setBody(std::nullopt);
    }

    if (!withAlternateData && resource.alternateData()) {
        resource.mutableAlternateData()->setBody(std::nullopt);
    }

    if (!withAttributes && resource.attributes()) {
        resource.setAttributes(std::nullopt);
    }

    Q_EMIT getResourceRequestReady(
        std::move(resource), nullptr, ctx->requestId());
}

void FakeNoteStoreBackend::onAuthenticateToSharedNotebookRequest(
    const QString & shareKeyOrGlobalId,
    const qevercloud::IRequestContextPtr & ctx)
{
    Q_ASSERT(ctx);

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnAuthenticateToSharedNotebook)
    {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)),
            ctx->requestId());
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{}, std::move(exc),
            ctx->requestId());
    }

    const auto & index =
        m_linkedNotebooks
            .get<note_store::LinkedNotebookBySharedNotebookGlobalIdTag>();

    const auto it = index.find(shareKeyOrGlobalId);
    if (Q_UNLIKELY(it == index.end())) {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INVALID_AUTH)
                    .setMessage(QStringLiteral("shareKey"))
                    .build()),
            ctx->requestId());
        return;
    }

    const auto & linkedNotebook = *it;
    if (Q_UNLIKELY(!linkedNotebook.guid())) {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{},
            std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(
                        QStringLiteral("Detected linked notebook without guid"))
                    .build()),
            ctx->requestId());
        return;
    }

    auto authTokenIt =
        m_linkedNotebookAuthTokensByGuid.find(*linkedNotebook.guid());

    if (Q_UNLIKELY(authTokenIt == m_linkedNotebookAuthTokensByGuid.end())) {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{},
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("SharedNotebook.id"))),
            ctx->requestId());
        return;
    }

    qevercloud::AuthenticationResult authResult;
    authResult.setAuthenticationToken(authTokenIt.value());
    authResult.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

    authResult.setExpiration(
        QDateTime::currentDateTime().addYears(1).toMSecsSinceEpoch());

    authResult.setNoteStoreUrl(QStringLiteral("Fake note store URL"));
    authResult.setWebApiUrlPrefix(QStringLiteral("Fake web API url prefix"));

    Q_EMIT authenticateToSharedNotebookRequestReady(
        std::move(authResult), nullptr, ctx->requestId());
}

std::exception_ptr FakeNoteStoreBackend::checkAuthentication(
    const qevercloud::IRequestContextPtr & ctx) const
{
    if (!ctx) {
        return std::make_exception_ptr(InvalidArgument{
            ErrorString{QStringLiteral("Request context is null")}});
    }

    if (ctx->authenticationToken() != m_authenticationToken) {
        return std::make_exception_ptr(InvalidArgument{ErrorString{
            QString::fromUtf8(
                "Invalid authentication token, expected %1, got %2")
                .arg(m_authenticationToken, ctx->authenticationToken())}});
    }

    // FIXME: uncomment after parsing of cookies into request context on server
    // side implementation is ready
    /*
    const auto cookies = ctx->cookies();
    for (const auto & cookie: m_cookies) {
        const auto it = std::find_if(
            cookies.constBegin(), cookies.constEnd(),
            [&cookie](const QNetworkCookie & c) {
                return c.name() == cookie.name();
            });
        if (it == cookies.constEnd()) {
            return std::make_exception_ptr(InvalidArgument{ErrorString{
                QString::fromUtf8(
                    "Missing network cookie in request: expected to find "
                    "cookie with name %1 but haven't found it")
                    .arg(QString::fromUtf8(cookie.name()))}});
        }

        if (it->value() != cookie.value()) {
            return std::make_exception_ptr(InvalidArgument{ErrorString{
                QString::fromUtf8(
                    "Network cookie contains unexpected value: expected for "
                    "cookie with name %1 to have value %2 but got %3")
                    .arg(
                        QString::fromUtf8(cookie.name()),
                        QString::fromUtf8(cookie.value()),
                        QString::fromUtf8(it->value()))}});
        }
    }
    */

    return nullptr;
}

std::exception_ptr FakeNoteStoreBackend::checkLinkedNotebookAuthentication(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::IRequestContextPtr & ctx) const
{
    if (!ctx) {
        return std::make_exception_ptr(InvalidArgument{
            ErrorString{QStringLiteral("Request context is null")}});
    }

    const auto it =
        m_linkedNotebookAuthTokensByGuid.constFind(linkedNotebookGuid);
    if (it == m_linkedNotebookAuthTokensByGuid.constEnd()) {
        return std::make_exception_ptr(InvalidArgument{ErrorString{
            QStringLiteral("Cannot find auth token for linked notebook")}});
    }

    if (it.value() != ctx->authenticationToken()) {
        return std::make_exception_ptr(InvalidArgument{
            ErrorString{QString::fromUtf8(
                            "Invalid authentication token, expected %1, got %2")
                            .arg(it.value(), ctx->authenticationToken())}});
    }

    return nullptr;
}

void FakeNoteStoreBackend::setMaxUsn(
    const qint32 maxUsn,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid)
{
    if (!linkedNotebookGuid) {
        m_userOwnMaxUsn = maxUsn;
        m_userOwnSyncState.setUpdateCount(maxUsn);
        return;
    }

    m_linkedNotebookMaxUsns[*linkedNotebookGuid] = maxUsn;
    m_linkedNotebookSyncStates[*linkedNotebookGuid].setUpdateCount(maxUsn);
}

std::pair<qevercloud::SyncChunk, std::exception_ptr>
    FakeNoteStoreBackend::getSyncChunkImpl(
        const qint32 afterUsn, const qint32 maxEntries, const bool fullSyncOnly,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        const qevercloud::SyncChunkFilter & filter,
        const qevercloud::IRequestContextPtr & ctx) const
{
    QNDEBUG(
        "tests::synchronization::FakeNoteStoreBackend",
        "FakeNoteStoreBackend::getSyncChunkImpl: afterUsn = "
            << afterUsn << ", max entries = " << maxEntries
            << ", linked notebook guid = "
            << linkedNotebookGuid.value_or(QStringLiteral("<none>")));

    if (auto exc = checkAuthentication(ctx)) {
        return std::make_pair(qevercloud::SyncChunk{}, std::move(exc));
    }

    if (afterUsn < 0) {
        return std::make_pair(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("afterUsn"))));
    }

    if (maxEntries < 1) {
        return std::make_pair(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("maxEntries"))));
    }

    qevercloud::SyncChunk syncChunk;
    syncChunk.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

    if (filter.notebookGuids() && !filter.notebookGuids()->isEmpty() &&
        filter.includeExpunged() && *filter.includeExpunged())
    {
        return std::make_pair(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT, QString{})));
    }

    const auto updateSyncChunkHighUsn =
        [&syncChunk](const qint32 usn) -> std::exception_ptr {
        if (syncChunk.chunkHighUSN() && *syncChunk.chunkHighUSN() >= usn) {
            QNWARNING(
                "tests::synchronization::FakeNoteStoreBackend",
                "Internal error during sync chunk collection: "
                    << "chunk high usn " << *syncChunk.chunkHighUSN()
                    << " is not less than the next item's usn " << usn);
            return std::make_exception_ptr(
                qevercloud::EDAMSystemExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                    .setMessage(
                        QString::fromUtf8(
                            "Internal error during sync chunk collection: "
                            "chunk high usn %1 is not less than the next "
                            "item's usn %2")
                            .arg(*syncChunk.chunkHighUSN(), usn))
                    .build());
        }

        syncChunk.setChunkHighUSN(usn);
        QNDEBUG(
            "tests::synchronization::FakeNoteStoreBackend",
            "Sync chunk high USN updated to " << *syncChunk.chunkHighUSN());
        return {};
    };

    const auto & savedSearchUsnIndex =
        m_savedSearches.get<note_store::SavedSearchByUSNTag>();

    const auto & tagUsnIndex = m_tags.get<note_store::TagByUSNTag>();
    const auto & notebookUsnIndex =
        m_notebooks.get<note_store::NotebookByUSNTag>();

    const auto & noteUsnIndex = m_notes.get<note_store::NoteByUSNTag>();
    const auto & resourceUsnIndex =
        m_resources.get<note_store::ResourceByUSNTag>();

    const auto & linkedNotebookUsnIndex =
        m_linkedNotebooks.get<note_store::LinkedNotebookByUSNTag>();

    const std::optional<qint32> maxUsn = linkedNotebookGuid
        ? currentLinkedNotebookMaxUsn(*linkedNotebookGuid)
        : std::make_optional(currentUserOwnMaxUsn());

    if (Q_UNLIKELY(!maxUsn)) {
        return std::make_pair(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("LinkedNotebook"))
                    .build()));
    }

    syncChunk.setUpdateCount(*maxUsn);
    QNDEBUG(
        "tests::synchronization::FakeNoteStoreBackend",
        "Sync chunk update count (max usn) = " << *maxUsn);

    auto savedSearchIt = savedSearchUsnIndex.end();
    if (!linkedNotebookGuid && filter.includeSearches().value_or(false)) {
        savedSearchIt = std::upper_bound(
            savedSearchUsnIndex.begin(), savedSearchUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::SavedSearch>());
    }

    auto tagIt = tagUsnIndex.end();
    if (filter.includeTags().value_or(false)) {
        tagIt = std::upper_bound(
            tagUsnIndex.begin(), tagUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Tag>());

        tagIt = advanceIterator(tagIt, tagUsnIndex, linkedNotebookGuid);
    }

    auto notebookIt = notebookUsnIndex.end();
    if (filter.includeNotebooks().value_or(false)) {
        notebookIt = std::upper_bound(
            notebookUsnIndex.begin(), notebookUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Notebook>());

        notebookIt =
            advanceIterator(notebookIt, notebookUsnIndex, linkedNotebookGuid);
    }

    auto noteIt = noteUsnIndex.end();
    if (filter.includeNotes().value_or(false)) {
        noteIt = std::upper_bound(
            noteUsnIndex.begin(), noteUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Note>());

        noteIt = nextNoteByUsnIterator(noteIt, linkedNotebookGuid);
    }

    auto resourceIt = resourceUsnIndex.end();
    if (!fullSyncOnly && filter.includeResources().value_or(false)) {
        resourceIt = std::upper_bound(
            resourceUsnIndex.begin(), resourceUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Resource>());

        resourceIt = nextResourceByUsnIterator(resourceIt, linkedNotebookGuid);
    }

    auto linkedNotebookIt = linkedNotebookUsnIndex.end();
    if (!linkedNotebookGuid && filter.includeLinkedNotebooks().value_or(false))
    {
        linkedNotebookIt = std::upper_bound(
            linkedNotebookUsnIndex.begin(), linkedNotebookUsnIndex.end(),
            afterUsn, CompareByUSN<qevercloud::LinkedNotebook>());
    }

    int syncChunkEntriesCounter = 0;
    while (true) {
        QNDEBUG(
            "tests::synchronization::FakeNoteStoreBackend",
            "Sync chunk collecting loop iteration, entries counter = "
                << syncChunkEntriesCounter);

        if (syncChunkEntriesCounter >= maxEntries) {
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Collected max number of sync chunk entries already");
            break;
        }

        auto nextItemType = NextItemType::None;
        qint32 lastItemUsn = std::numeric_limits<qint32>::max();

        if (savedSearchIt != savedSearchUsnIndex.end()) {
            const auto & nextSearch = *savedSearchIt;
            const qint32 usn = nextSearch.updateSequenceNum().value();
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Next saved search usn = " << usn);
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::SavedSearch;
            }
        }

        if (linkedNotebookIt != linkedNotebookUsnIndex.end()) {
            const auto & nextLinkedNotebook = *linkedNotebookIt;
            const qint32 usn = nextLinkedNotebook.updateSequenceNum().value();
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Next linked notebook usn = " << usn);
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::LinkedNotebook;
            }
        }

        if (tagIt != tagUsnIndex.end()) {
            const auto & nextTag = *tagIt;
            const qint32 usn = nextTag.updateSequenceNum().value();
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Next tag usn = " << usn);
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Tag;
            }
        }

        if (notebookIt != notebookUsnIndex.end()) {
            const auto & nextNotebook = *notebookIt;
            const qint32 usn = nextNotebook.updateSequenceNum().value();
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Next notebook usn = " << usn);
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Notebook;
            }
        }

        if (noteIt != noteUsnIndex.end()) {
            const auto & nextNote = *noteIt;
            const qint32 usn = nextNote.updateSequenceNum().value();
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Next note usn = " << usn);
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Note;
            }
        }

        if (resourceIt != resourceUsnIndex.end()) {
            const auto & nextResource = *resourceIt;
            const qint32 usn = nextResource.updateSequenceNum().value();
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Next resource usn = " << usn);
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Resource;
            }
        }

        QNDEBUG(
            "tests::synchronization::FakeNoteStoreBackend",
            "Next item type = " << nextItemType << ", usn = " << lastItemUsn);

        if (nextItemType == NextItemType::None) {
            break;
        }

        switch (nextItemType) {
        case NextItemType::SavedSearch:
        {
            if (!syncChunk.searches()) {
                syncChunk.setSearches(QList<qevercloud::SavedSearch>{});
            }

            qevercloud::SavedSearch search = *savedSearchIt;
            search.setLocalId(UidGenerator::Generate());
            search.setLocalData({});
            search.setLocalOnly(false);
            search.setLocallyModified(false);
            search.setLocallyFavorited(false);

            syncChunk.mutableSearches()->append(search);
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Added saved search to sync chunk: " << *savedSearchIt);

            ++syncChunkEntriesCounter;

            if (auto exc = updateSyncChunkHighUsn(
                    savedSearchIt->updateSequenceNum().value()))
            {
                return std::make_pair(qevercloud::SyncChunk{}, std::move(exc));
            }

            ++savedSearchIt;
        } break;
        case NextItemType::Tag:
        {
            if (!syncChunk.tags()) {
                syncChunk.setTags(QList<qevercloud::Tag>{});
            }

            qevercloud::Tag tag = *tagIt;
            tag.setLocalId(UidGenerator::Generate());
            tag.setLocalData({});
            tag.setLocalOnly(false);
            tag.setLocallyModified(false);
            tag.setLocallyFavorited(false);
            tag.setLinkedNotebookGuid(std::nullopt);
            tag.setParentTagLocalId(QString{});

            syncChunk.mutableTags()->append(tag);
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Added tag to sync chunk: " << *tagIt);

            ++syncChunkEntriesCounter;

            if (auto exc =
                    updateSyncChunkHighUsn(tagIt->updateSequenceNum().value()))
            {
                return std::make_pair(qevercloud::SyncChunk{}, std::move(exc));
            }

            ++tagIt;
            tagIt = advanceIterator(tagIt, tagUsnIndex, linkedNotebookGuid);
        } break;
        case NextItemType::Notebook:
        {
            if (!syncChunk.notebooks()) {
                syncChunk.setNotebooks(QList<qevercloud::Notebook>{});
            }

            qevercloud::Notebook notebook = *notebookIt;
            notebook.setLocalId(UidGenerator::Generate());
            notebook.setLocalData({});
            notebook.setLocalOnly(false);
            notebook.setLocallyModified(false);
            notebook.setLocallyFavorited(false);
            notebook.setLinkedNotebookGuid(std::nullopt);

            syncChunk.mutableNotebooks()->append(notebook);
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Added notebook to sync chunk: " << *notebookIt);

            ++syncChunkEntriesCounter;

            if (auto exc = updateSyncChunkHighUsn(
                    notebookIt->updateSequenceNum().value()))
            {
                return std::make_pair(qevercloud::SyncChunk{}, std::move(exc));
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
            Q_ASSERT(qecNote.guid());

            qecNote.setLocalId(UidGenerator::Generate());
            qecNote.setLocalData({});
            qecNote.setLocalOnly(false);
            qecNote.setLocallyModified(false);
            qecNote.setLocallyFavorited(false);
            qecNote.setTagLocalIds(QStringList{});
            qecNote.setNotebookLocalId(QString{});

            if (!filter.includeNoteResources().value_or(false)) {
                qecNote.setResources(std::nullopt);
            }

            if (!filter.includeNoteAttributes().value_or(false)) {
                qecNote.setAttributes(std::nullopt);
            }
            else {
                if (!filter.includeNoteApplicationDataFullMap().value_or(
                        false) &&
                    qecNote.attributes() &&
                    qecNote.attributes()->applicationData())
                {
                    qecNote.mutableAttributes()
                        ->mutableApplicationData()
                        ->setFullMap(std::nullopt);
                }

                if (!filter.includeNoteResourceApplicationDataFullMap()
                         .value_or(false) &&
                    qecNote.resources())
                {
                    for (auto & resource: *qecNote.mutableResources()) {
                        if (resource.attributes() &&
                            resource.attributes()->applicationData())
                        {
                            resource.mutableAttributes()
                                ->mutableApplicationData()
                                ->setFullMap(std::nullopt);
                        }
                    }
                }
            }

            if (!filter.includeSharedNotes().value_or(false)) {
                qecNote.setSharedNotes(std::nullopt);
            }

            // Notes within the sync chunks should include only note
            // metadata but no content, resource content, resource
            // recognition data or resource alternate data
            qecNote.setContent(std::nullopt);
            if (qecNote.resources()) {
                for (auto & resource: *qecNote.mutableResources()) {
                    if (resource.data()) {
                        resource.mutableData()->setBody(std::nullopt);
                    }
                    if (resource.recognition()) {
                        resource.mutableRecognition()->setBody(std::nullopt);
                    }
                    if (resource.alternateData()) {
                        resource.mutableAlternateData()->setBody(std::nullopt);
                    }

                    resource.setLocalId(UidGenerator::Generate());
                    resource.setLocalData({});
                    resource.setLocalOnly(false);
                    resource.setLocallyModified(false);
                    resource.setLocallyFavorited(false);
                    resource.setNoteLocalId(QString{});
                }
            }

            syncChunk.mutableNotes()->append(qecNote);
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Added note to sync chunk: " << qecNote);

            ++syncChunkEntriesCounter;

            if (auto exc =
                    updateSyncChunkHighUsn(noteIt->updateSequenceNum().value()))
            {
                return std::make_pair(qevercloud::SyncChunk{}, std::move(exc));
            }

            if (noteIt->resources() && !noteIt->resources()->isEmpty()) {
                for (const auto & resource: std::as_const(*noteIt->resources()))
                {
                    if (!syncChunk.chunkHighUSN() ||
                        syncChunk.chunkHighUSN() <
                            resource.updateSequenceNum().value())
                    {
                        syncChunk.setChunkHighUSN(
                            *resource.updateSequenceNum());

                        QNDEBUG(
                            "tests::synchronization::FakeNoteStoreBackend",
                            "Sync chunk high USN updated to "
                                << *syncChunk.chunkHighUSN());
                    }
                }
            }

            ++noteIt;
            noteIt = nextNoteByUsnIterator(noteIt, linkedNotebookGuid);
        } break;
        case NextItemType::Resource:
        {
            // If chunk high USN is already larger than that of this resource,
            // it must be due to some note being already added to this sync
            // chunk which already contains this resource
            if (!syncChunk.chunkHighUSN() ||
                *syncChunk.chunkHighUSN() <
                    resourceIt->updateSequenceNum().value())
            {
                if (!syncChunk.resources()) {
                    syncChunk.setResources(QList<qevercloud::Resource>{});
                }

                auto qecResource = *resourceIt;
                qecResource.setLocalId(UidGenerator::Generate());
                qecResource.setLocalData({});
                qecResource.setLocalOnly(false);
                qecResource.setLocallyModified(false);
                qecResource.setLocallyFavorited(false);
                qecResource.setNoteLocalId(QString{});

                if ((!filter.includeResourceApplicationDataFullMap() ||
                     !*filter.includeResourceApplicationDataFullMap()) &&
                    qecResource.attributes() &&
                    qecResource.attributes()->applicationData())
                {
                    qecResource.mutableAttributes()
                        ->mutableApplicationData()
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
                QNDEBUG(
                    "tests::synchronization::FakeNoteStoreBackend",
                    "Added resource to sync chunk: " << qecResource);

                ++syncChunkEntriesCounter;

                if (auto exc = updateSyncChunkHighUsn(
                        resourceIt->updateSequenceNum().value()))
                {
                    return std::make_pair(
                        qevercloud::SyncChunk{}, std::move(exc));
                }
            }
            else {
                bool foundResourceWithinNote = false;
                if (syncChunk.notes() && !syncChunk.notes()->isEmpty()) {
                    for (const auto & note: std::as_const(*syncChunk.notes())) {
                        if (!note.resources() || note.resources()->isEmpty()) {
                            continue;
                        }

                        const auto it = std::find_if(
                            note.resources()->constBegin(),
                            note.resources()->constEnd(),
                            [resourceIt](
                                const qevercloud::Resource & resource) {
                                return resource.guid() == resourceIt->guid();
                            });
                        if (it != note.resources()->constEnd()) {
                            foundResourceWithinNote = true;
                            break;
                        }
                    }
                }

                if (!foundResourceWithinNote) {
                    QNWARNING(
                        "tests::synchronization::FakeNoteStoreBackend",
                        "Internal error during sync chunk collection: "
                            << "chunk high usn "
                            << (syncChunk.chunkHighUSN()
                                    ? QString::number(*syncChunk.chunkHighUSN())
                                    : QStringLiteral("<none>"))
                            << " is not less than the next resource's usn "
                            << (resourceIt->updateSequenceNum()
                                    ? QString::number(
                                          *resourceIt->updateSequenceNum())
                                    : QStringLiteral("<none>"))
                            << " but the resource was not found within "
                            << "sync chunk's notes");
                    return std::make_pair(
                        qevercloud::SyncChunk{},
                        std::make_exception_ptr(
                            qevercloud::EDAMSystemExceptionBuilder{}
                                .setErrorCode(
                                    qevercloud::EDAMErrorCode::INTERNAL_ERROR)
                                .setMessage(
                                    QString::fromUtf8(
                                        "Internal error during sync "
                                        "chunk collection: "
                                        "chunk high usn %1 is not "
                                        "less than the next "
                                        "resource's usn %2")
                                        .arg(
                                            (syncChunk.chunkHighUSN()
                                                 ? QString::number(
                                                       *syncChunk
                                                            .chunkHighUSN())
                                                 : QStringLiteral("<none>")),
                                            (resourceIt->updateSequenceNum()
                                                 ? QString::number(
                                                       *resourceIt
                                                            ->updateSequenceNum())
                                                 : QStringLiteral("<none>"))))
                                .build()));
                }
            }

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
            QNDEBUG(
                "tests::synchronization::FakeNoteStoreBackend",
                "Added linked notebook to sync chunk: " << *linkedNotebookIt);

            ++syncChunkEntriesCounter;

            if (auto exc = updateSyncChunkHighUsn(
                    linkedNotebookIt->updateSequenceNum().value()))
            {
                return std::make_pair(qevercloud::SyncChunk{}, std::move(exc));
            }

            ++linkedNotebookIt;
        } break;
        default:
            QNWARNING(
                "tests::synchronization::FakeNoteStoreBackend",
                "Unexpected next item type: " << nextItemType);
            break;
        }
    }

    if (fullSyncOnly) {
        // No need to insert the information about expunged data items
        // when doing full sync
        return std::make_pair(std::move(syncChunk), nullptr);
    }

    // Processing of expunged items is not strictly correct - each fact of
    // item expunging actually causes the increase of corresponding USN - user's
    // own one of the one from some linked notebook. So expunged items should
    // really be accounted for in the loop above over different data item types.
    // However, for now a bit simpler scheme is involved which seems to be
    // good enough for sync integrational tests so far: expunged items are just
    // "retrofitted" into the already collected sync chunk if their
    // corresponding USNs are appropriate for the requested USN range.
    if (!linkedNotebookGuid && !m_expungedSavedSearchGuidsAndUsns.isEmpty()) {
        for (const auto it: qevercloud::toRange(
                 std::as_const(m_expungedSavedSearchGuidsAndUsns)))
        {
            const qint32 usn = it.value();
            if (usn < afterUsn) {
                continue;
            }

            if (!syncChunk.expungedSearches()) {
                syncChunk.setExpungedSearches(QList<qevercloud::Guid>{});
            }

            syncChunk.mutableExpungedSearches()->append(it.key());
            if (!syncChunk.chunkHighUSN() || *syncChunk.chunkHighUSN() < usn) {
                syncChunk.setChunkHighUSN(usn);
                QNDEBUG(
                    "tests::synchronization::FakeNoteStoreBackend",
                    "Sync chunk high USN updated to "
                        << *syncChunk.chunkHighUSN());
            }
        }
    }

    if (!linkedNotebookGuid && !m_expungedUserOwnTagGuidsAndUsns.isEmpty()) {
        for (const auto it: qevercloud::toRange(
                 std::as_const(m_expungedUserOwnTagGuidsAndUsns)))
        {
            const qint32 usn = it.value();
            if (usn < afterUsn) {
                continue;
            }

            if (!syncChunk.expungedTags()) {
                syncChunk.setExpungedTags(QList<qevercloud::Guid>{});
            }

            syncChunk.mutableExpungedTags()->append(it.key());
            if (!syncChunk.chunkHighUSN() || *syncChunk.chunkHighUSN() < usn) {
                syncChunk.setChunkHighUSN(usn);
                QNDEBUG(
                    "tests::synchronization::FakeNoteStoreBackend",
                    "Sync chunk high USN updated to "
                        << *syncChunk.chunkHighUSN());
            }
        }
    }
    else if (linkedNotebookGuid) {
        const auto it = m_expungedLinkedNotebookTagGuidsAndUsns.constFind(
            *linkedNotebookGuid);
        if (it != m_expungedLinkedNotebookTagGuidsAndUsns.constEnd()) {
            const auto & expungedTagGuidsAndUsns = it.value();
            for (const auto it:
                 qevercloud::toRange(std::as_const(expungedTagGuidsAndUsns)))
            {
                const qint32 usn = it.value();
                if (usn < afterUsn) {
                    continue;
                }

                if (!syncChunk.expungedTags()) {
                    syncChunk.setExpungedTags(QList<qevercloud::Guid>{});
                }

                syncChunk.mutableExpungedTags()->append(it.key());
                if (!syncChunk.chunkHighUSN() ||
                    *syncChunk.chunkHighUSN() < usn)
                {
                    syncChunk.setChunkHighUSN(usn);
                    QNDEBUG(
                        "tests::synchronization::FakeNoteStoreBackend",
                        "Sync chunk high USN updated to "
                            << *syncChunk.chunkHighUSN());
                }
            }
        }
    }

    if (!linkedNotebookGuid && !m_expungedUserOwnNotebookGuidsAndUsns.isEmpty())
    {
        for (const auto it: qevercloud::toRange(
                 std::as_const(m_expungedUserOwnNotebookGuidsAndUsns)))
        {
            const qint32 usn = it.value();
            if (usn < afterUsn) {
                continue;
            }

            if (!syncChunk.expungedNotebooks()) {
                syncChunk.setExpungedNotebooks(QList<qevercloud::Guid>{});
            }

            syncChunk.mutableExpungedNotebooks()->append(it.key());
            if (!syncChunk.chunkHighUSN() || *syncChunk.chunkHighUSN() < usn) {
                syncChunk.setChunkHighUSN(usn);
                QNDEBUG(
                    "tests::synchronization::FakeNoteStoreBackend",
                    "Sync chunk high USN updated to "
                        << *syncChunk.chunkHighUSN());
            }
        }
    }
    else if (linkedNotebookGuid) {
        const auto it = m_expungedLinkedNotebookNotebookGuidsAndUsns.constFind(
            *linkedNotebookGuid);
        if (it != m_expungedLinkedNotebookNotebookGuidsAndUsns.constEnd()) {
            const auto & expungedNotebookGuidsAndUsns = it.value();

            if (!syncChunk.expungedNotebooks()) {
                syncChunk.setExpungedNotebooks(QList<qevercloud::Guid>{});
            }

            for (const auto it: qevercloud::toRange(
                     std::as_const(expungedNotebookGuidsAndUsns)))
            {
                const qint32 usn = it.value();
                if (usn < afterUsn) {
                    continue;
                }

                syncChunk.mutableExpungedNotebooks()->append(it.key());
                if (!syncChunk.chunkHighUSN() ||
                    *syncChunk.chunkHighUSN() < usn)
                {
                    syncChunk.setChunkHighUSN(usn);
                    QNDEBUG(
                        "tests::synchronization::FakeNoteStoreBackend",
                        "Sync chunk high USN updated to "
                            << *syncChunk.chunkHighUSN());
                }
            }
        }
    }

    if (!linkedNotebookGuid && !m_expungedUserOwnNoteGuidsAndUsns.isEmpty()) {
        if (!syncChunk.expungedNotes()) {
            syncChunk.setExpungedNotes(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedNotes()->reserve(
            m_expungedUserOwnNoteGuidsAndUsns.size());

        for (const auto it: qevercloud::toRange(
                 std::as_const(m_expungedUserOwnNoteGuidsAndUsns)))
        {
            const qint32 usn = it.value();
            if (usn < afterUsn) {
                continue;
            }

            syncChunk.mutableExpungedNotes()->append(it.key());
            if (!syncChunk.chunkHighUSN() || *syncChunk.chunkHighUSN() < usn) {
                syncChunk.setChunkHighUSN(usn);
                QNDEBUG(
                    "tests::synchronization::FakeNoteStoreBackend",
                    "Sync chunk high USN updated to "
                        << *syncChunk.chunkHighUSN());
            }
        }
    }
    else if (linkedNotebookGuid) {
        const auto it = m_expungedLinkedNotebookNoteGuidsAndUsns.constFind(
            *linkedNotebookGuid);
        if (it != m_expungedLinkedNotebookNoteGuidsAndUsns.constEnd()) {
            const auto & expungedNoteGuidsAndUsns = it.value();
            for (const auto it:
                 qevercloud::toRange(std::as_const(expungedNoteGuidsAndUsns)))
            {
                const qint32 usn = it.value();
                if (usn < afterUsn) {
                    continue;
                }

                if (!syncChunk.expungedNotes()) {
                    syncChunk.setExpungedNotes(QList<qevercloud::Guid>{});
                }

                syncChunk.mutableExpungedNotes()->append(it.key());
                if (!syncChunk.chunkHighUSN() ||
                    *syncChunk.chunkHighUSN() < usn)
                {
                    syncChunk.setChunkHighUSN(usn);
                    QNDEBUG(
                        "tests::synchronization::FakeNoteStoreBackend",
                        "Sync chunk high USN updated to "
                            << *syncChunk.chunkHighUSN());
                }
            }
        }
    }

    if (!linkedNotebookGuid && !m_expungedLinkedNotebookGuidsAndUsns.isEmpty())
    {
        for (const auto it: qevercloud::toRange(
                 std::as_const(m_expungedLinkedNotebookGuidsAndUsns)))
        {
            const qint32 usn = it.value();
            if (usn < afterUsn) {
                continue;
            }

            if (!syncChunk.expungedLinkedNotebooks()) {
                syncChunk.setExpungedLinkedNotebooks(QList<qevercloud::Guid>{});
            }

            syncChunk.mutableExpungedLinkedNotebooks()->append(it.key());
            if (!syncChunk.chunkHighUSN() || *syncChunk.chunkHighUSN() < usn) {
                syncChunk.setChunkHighUSN(usn);
                QNDEBUG(
                    "tests::synchronization::FakeNoteStoreBackend",
                    "Sync chunk high USN updated to "
                        << *syncChunk.chunkHighUSN());
            }
        }
    }

    return std::make_pair(std::move(syncChunk), nullptr);
}

note_store::NotesByUSN::const_iterator
    FakeNoteStoreBackend::nextNoteByUsnIterator(
        note_store::NotesByUSN::const_iterator it,
        const std::optional<qevercloud::Guid> & targetLinkedNotebookGuid) const
{
    const auto & noteUsnIndex = m_notes.get<note_store::NoteByUSNTag>();
    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    while (it != noteUsnIndex.end()) {
        const qevercloud::Guid & notebookGuid = it->notebookGuid().value();
        auto noteNotebookIt = notebookGuidIndex.find(notebookGuid);
        if (Q_UNLIKELY(noteNotebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests::synchronization::FakeNoteStoreBackend",
                "Found note which notebook guid doesn't correspond to any "
                    << "existing notebook: " << *it);
            ++it;
            continue;
        }

        const auto & notebook = *noteNotebookIt;
        if (notebook.linkedNotebookGuid() != targetLinkedNotebookGuid) {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

note_store::ResourcesByUSN::const_iterator
    FakeNoteStoreBackend::nextResourceByUsnIterator(
        note_store::ResourcesByUSN::const_iterator it,
        const std::optional<qevercloud::Guid> & targetLinkedNotebookGuid) const
{
    const auto & resourceUsnIndex =
        m_resources.get<note_store::ResourceByUSNTag>();

    const auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    while (it != resourceUsnIndex.end()) {
        const qevercloud::Guid & noteGuid = it->noteGuid().value();

        auto resourceNoteIt = noteGuidIndex.find(noteGuid);
        if (Q_UNLIKELY(resourceNoteIt == noteGuidIndex.end())) {
            QNWARNING(
                "tests::synchronization::FakeNoteStoreBackend",
                "Found resource which note guid doesn't correspond to any "
                    << "existing note: " << *it);
            ++it;
            continue;
        }

        const auto & note = *resourceNoteIt;
        const qevercloud::Guid & notebookGuid = note.notebookGuid().value();
        auto noteNotebookIt = notebookGuidIndex.find(notebookGuid);
        if (Q_UNLIKELY(noteNotebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests::synchronization::FakeNoteStoreBackend",
                "Found note which notebook guid doesn't correspond to any "
                    << "existing notebook: " << note);
            ++it;
            continue;
        }

        const auto & notebook = *noteNotebookIt;
        if (notebook.linkedNotebookGuid() != targetLinkedNotebookGuid) {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

} // namespace quentier::synchronization::tests
