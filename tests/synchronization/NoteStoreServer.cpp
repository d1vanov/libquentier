/*
 * Copyright 2023 Dmitry Ivanov
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

#include "NoteStoreServer.h"

#include "note_store/Checks.h"
#include "note_store/Compat.h"
#include "utils/ExceptionUtils.h"
#include "utils/HttpUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/exceptions/builders/EDAMNotFoundExceptionBuilder.h>
#include <qevercloud/exceptions/builders/EDAMSystemExceptionBuilder.h>
#include <qevercloud/exceptions/builders/EDAMUserExceptionBuilder.h>
#include <qevercloud/services/NoteStoreServer.h>

#include <QDateTime>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

#include <algorithm>

namespace quentier::synchronization::tests {

namespace {

[[nodiscard]] QString nextName(const QString & name)
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

template <class ConstIterator, class UsnIndex>
[[nodiscard]] ConstIterator advanceIterator(
    ConstIterator it, const UsnIndex & index,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid) noexcept
{
    while (it != index.end()) {
        if (!linkedNotebookGuid && it->linkedNotebookGuid()) {
            ++it;
            continue;
        }

        if (linkedNotebookGuid && !it->linkedNotebookGuid()) {
            ++it;
            continue;
        }

        if (linkedNotebookGuid && it->linkedNotebookGuid() &&
            (linkedNotebookGuid != *it->linkedNotebookGuid()))
        {
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

NoteStoreServer::NoteStoreServer(
    QString authenticationToken, QList<QNetworkCookie> cookies,
    QObject * parent) :
    QObject(parent),
    // clang-format off
    m_authenticationToken{std::move(authenticationToken)},
    m_cookies{std::move(cookies)}
// clang-format on
{
    bool res = m_tcpServer->listen(QHostAddress::LocalHost);
    if (Q_UNLIKELY(!res)) {
        throw RuntimeError{
            ErrorString{QString::fromUtf8("Failed to set up a TCP server for "
                                          "NoteStore on localhost: (%1) "
                                          "%2")
                            .arg(m_tcpServer->serverError())
                            .arg(m_tcpServer->errorString())}};
    }

    QObject::connect(m_tcpServer, &QTcpServer::newConnection, this, [this] {
        m_tcpSocket = m_tcpServer->nextPendingConnection();
        Q_ASSERT(m_tcpSocket);

        QObject::connect(
            m_tcpSocket, &QAbstractSocket::disconnected, m_tcpSocket,
            &QAbstractSocket::deleteLater);
        if (!m_tcpSocket->waitForConnected()) {
            QFAIL("Failed to establish connection");
        }

        QByteArray requestData = utils::readRequestBodyFromSocket(*m_tcpSocket);

        m_server->onRequest(std::move(requestData));
    });

    connectToQEverCloudServer();
}

NoteStoreServer::~NoteStoreServer() = default;

quint16 NoteStoreServer::port() const noexcept
{
    return m_tcpServer->serverPort();
}

QHash<qevercloud::Guid, qevercloud::SavedSearch>
    NoteStoreServer::savedSearches() const
{
    QHash<qevercloud::Guid, qevercloud::SavedSearch> result;
    result.reserve(static_cast<int>(m_savedSearches.size()));

    for (const auto & savedSearch: m_savedSearches) {
        Q_ASSERT(savedSearch.guid());
        result[*savedSearch.guid()] = savedSearch;
    }

    return result;
}

NoteStoreServer::ItemData NoteStoreServer::putSavedSearch(
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

std::optional<qevercloud::SavedSearch> NoteStoreServer::findSavedSearch(
    const qevercloud::Guid & guid) const
{
    const auto & index =
        m_savedSearches.get<note_store::SavedSearchByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void NoteStoreServer::removeSavedSearch(const qevercloud::Guid & guid)
{
    auto & index = m_savedSearches.get<note_store::SavedSearchByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        index.erase(it);
    }
}

void NoteStoreServer::putExpungedSavedSearchGuid(const qevercloud::Guid & guid)
{
    removeSavedSearch(guid);
    m_expungedSavedSearchGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedSavedSearchGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedSavedSearchGuids.contains(guid);
}

void NoteStoreServer::removeExpungedSavedSearchGuid(
    const qevercloud::Guid & guid)
{
    m_expungedSavedSearchGuids.remove(guid);
}

QHash<qevercloud::Guid, qevercloud::Tag> NoteStoreServer::tags() const
{
    QHash<qevercloud::Guid, qevercloud::Tag> result;
    result.reserve(static_cast<int>(m_tags.size()));

    for (const auto & tag: m_tags) {
        Q_ASSERT(tag.guid());
        result[*tag.guid()] = tag;
    }

    return result;
}

NoteStoreServer::ItemData NoteStoreServer::putTag(qevercloud::Tag tag)
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

    if (Q_UNLIKELY(!maxUsn)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Failed to find max USN on attempt to put tag")}};
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

std::optional<qevercloud::Tag> NoteStoreServer::findTag(
    const QString & guid) const
{
    const auto & index = m_tags.get<note_store::TagByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void NoteStoreServer::removeTag(const qevercloud::Guid & guid)
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

    for (const auto & childTagGuid: qAsConst(childTagGuids)) {
        removeTag(childTagGuid);
    }

    // NOTE: have to once again evaluate the iterator if we deleted any child
    // tags since the deletion of child tags could cause the invalidation of
    // the previously found iterator
    if (!childTagGuids.isEmpty()) {
        tagIt = index.find(guid);
        if (Q_UNLIKELY(tagIt == index.end())) {
            QNWARNING(
                "tests::synchronization",
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
        int tagGuidIndex = tagGuids.indexOf(guid);
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

void NoteStoreServer::putExpungedUserOwnTagGuid(const qevercloud::Guid & guid)
{
    removeTag(guid);
    m_expungedUserOwnTagGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedUserOwnTagGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedUserOwnTagGuids.contains(guid);
}

void NoteStoreServer::removeExpungedUserOwnTagGuid(
    const qevercloud::Guid & guid)
{
    m_expungedUserOwnTagGuids.remove(guid);
}

void NoteStoreServer::putExpungedLinkedNotebookTagGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & tagGuid)
{
    removeTag(tagGuid);
    m_expungedLinkedNotebookTagGuids[linkedNotebookGuid].insert(tagGuid);
}

bool NoteStoreServer::containsExpungedLinkedNotebookTagGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & tagGuid) const
{
    const auto it =
        m_expungedLinkedNotebookTagGuids.constFind(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookTagGuids.constEnd()) {
        return false;
    }

    return it->contains(tagGuid);
}

void NoteStoreServer::removeExpungedLinkedNotebookTagGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & tagGuid)
{
    const auto it = m_expungedLinkedNotebookTagGuids.find(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookTagGuids.end()) {
        return;
    }

    if (!it->remove(tagGuid)) {
        return;
    }

    if (it->isEmpty()) {
        m_expungedLinkedNotebookTagGuids.erase(it);
    }
}

QHash<qevercloud::Guid, qevercloud::Notebook> NoteStoreServer::notebooks() const
{
    QHash<qevercloud::Guid, qevercloud::Notebook> result;
    result.reserve(static_cast<int>(m_notebooks.size()));

    for (const auto & notebook: m_notebooks) {
        Q_ASSERT(notebook.guid());
        result[*notebook.guid()] = notebook;
    }

    return result;
}

NoteStoreServer::ItemData NoteStoreServer::putNotebook(
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

    if (Q_UNLIKELY(!maxUsn)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Failed to find max USN on attempt to put notebook")}};
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

std::optional<qevercloud::Notebook> NoteStoreServer::findNotebook(
    const qevercloud::Guid & guid) const
{
    const auto & index = m_notebooks.get<note_store::NotebookByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void NoteStoreServer::removeNotebook(const qevercloud::Guid & guid)
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

    for (const auto & noteGuid: qAsConst(noteGuids)) {
        removeNote(noteGuid);
    }

    index.erase(notebookIt);
}

QList<qevercloud::Notebook> NoteStoreServer::findNotebooksForLinkedNotebookGuid(
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

void NoteStoreServer::putExpungedUserOwnNotebookGuid(
    const qevercloud::Guid & guid)
{
    removeNotebook(guid);
    m_expungedUserOwnNotebookGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedUserOwnNotebookGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedUserOwnNotebookGuids.contains(guid);
}

void NoteStoreServer::removeExpungedUserOwnNotebookGuid(
    const qevercloud::Guid & guid)
{
    m_expungedUserOwnNotebookGuids.remove(guid);
}

void NoteStoreServer::putExpungedLinkedNotebookNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & notebookGuid)
{
    removeNotebook(notebookGuid);
    m_expungedLinkedNotebookNotebookGuids[linkedNotebookGuid].insert(
        notebookGuid);
}

bool NoteStoreServer::containsExpungedLinkedNotebookNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & notebookGuid) const
{
    const auto it =
        m_expungedLinkedNotebookNotebookGuids.constFind(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNotebookGuids.constEnd()) {
        return false;
    }

    return it->contains(notebookGuid);
}

void NoteStoreServer::removeExpungedLinkedNotebookNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & notebookGuid)
{
    const auto it =
        m_expungedLinkedNotebookNotebookGuids.find(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNotebookGuids.end()) {
        return;
    }

    if (!it->remove(notebookGuid)) {
        return;
    }

    if (it->isEmpty()) {
        m_expungedLinkedNotebookNotebookGuids.erase(it);
    }
}

QHash<qevercloud::Guid, qevercloud::Note> NoteStoreServer::notes() const
{
    QHash<qevercloud::Guid, qevercloud::Note> result;
    result.reserve(static_cast<int>(m_notes.size()));

    for (const auto & note: m_notes) {
        Q_ASSERT(note.guid());
        result[*note.guid()] = note;
    }

    return result;
}

NoteStoreServer::ItemData NoteStoreServer::putNote(qevercloud::Note note)
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

    if (Q_UNLIKELY(!maxUsn)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("Failed to find max USN on attempt to put note")}};
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
    noteGuidIndex.replace(noteIt, note);
    note.setResources(originalResources);
    return result;
}

std::optional<qevercloud::Note> NoteStoreServer::findNote(
    const qevercloud::Guid & guid) const
{
    const auto & index = m_notes.get<note_store::NoteByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void NoteStoreServer::removeNote(const qevercloud::Guid & guid)
{
    auto & index = m_notes.get<note_store::NoteByGuidTag>();
    const auto it = index.find(guid);
    if (it == index.end()) {
        return;
    }

    const auto & note = *it;
    if (note.resources() && !note.resources()->isEmpty()) {
        const auto resources = *note.resources();
        for (const auto & resource: qAsConst(resources)) {
            removeResource(resource.guid().value());
        }
    }

    index.erase(it);
}

QList<qevercloud::Note> NoteStoreServer::getNotesByConflictSourceNoteGuid(
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

void NoteStoreServer::putExpungedUserOwnNoteGuid(const qevercloud::Guid & guid)
{
    removeNote(guid);
    m_expungedUserOwnNoteGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedUserOwnNoteGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedUserOwnNoteGuids.contains(guid);
}

void NoteStoreServer::removeExpungedUserOwnNoteGuid(
    const qevercloud::Guid & guid)
{
    m_expungedUserOwnNoteGuids.remove(guid);
}

void NoteStoreServer::putExpungedLinkedNotebookNoteGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & noteGuid)
{
    removeNote(noteGuid);
    m_expungedLinkedNotebookNoteGuids[linkedNotebookGuid].insert(noteGuid);
}

bool NoteStoreServer::containsExpungedLinkedNotebookNoteGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & noteGuid) const
{
    const auto it =
        m_expungedLinkedNotebookNoteGuids.constFind(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNoteGuids.constEnd()) {
        return false;
    }

    return it->contains(noteGuid);
}

void NoteStoreServer::removeExpungedLinkedNotebookNoteGuid(
    const qevercloud::Guid & linkedNotebookGuid,
    const qevercloud::Guid & noteGuid)
{
    const auto it = m_expungedLinkedNotebookNoteGuids.find(linkedNotebookGuid);
    if (it == m_expungedLinkedNotebookNoteGuids.end()) {
        return;
    }

    if (!it->remove(noteGuid)) {
        return;
    }

    if (it->isEmpty()) {
        m_expungedLinkedNotebookNoteGuids.erase(it);
    }
}

QHash<qevercloud::Guid, qevercloud::Resource> NoteStoreServer::resources() const
{
    QHash<qevercloud::Guid, qevercloud::Resource> result;
    result.reserve(static_cast<int>(m_resources.size()));

    for (const auto & resource: m_resources) {
        Q_ASSERT(resource.guid());
        result[*resource.guid()] = resource;
    }

    return result;
}

NoteStoreServer::ItemData NoteStoreServer::putResource(
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

    if (Q_UNLIKELY(!maxUsn)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "Failed to find max USN on attempt to put resource")}};
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

std::optional<qevercloud::Resource> NoteStoreServer::findResource(
    const qevercloud::Guid & guid) const
{
    const auto & index = m_resources.get<note_store::ResourceByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void NoteStoreServer::removeResource(const qevercloud::Guid & guid)
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
            "tests::synchronization",
            "Found no note corresponding to the removed resource: " << *it);
    }

    index.erase(it);
}

QHash<qevercloud::Guid, qevercloud::LinkedNotebook>
    NoteStoreServer::linkedNotebooks() const
{
    QHash<qevercloud::Guid, qevercloud::LinkedNotebook> result;
    result.reserve(static_cast<int>(m_linkedNotebooks.size()));

    for (const auto & linkedNotebook: m_linkedNotebooks) {
        Q_ASSERT(linkedNotebook.guid());
        result[*linkedNotebook.guid()] = linkedNotebook;
    }

    return result;
}

NoteStoreServer::ItemData NoteStoreServer::putLinkedNotebook(
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

std::optional<qevercloud::LinkedNotebook> NoteStoreServer::findLinkedNotebook(
    const qevercloud::Guid & guid) const
{
    const auto & index =
        m_linkedNotebooks.get<note_store::LinkedNotebookByGuidTag>();

    if (const auto it = index.find(guid); it != index.end()) {
        return *it;
    }

    return std::nullopt;
}

void NoteStoreServer::removeLinkedNotebook(const qevercloud::Guid & guid)
{
    auto & index = m_linkedNotebooks.get<note_store::LinkedNotebookByGuidTag>();
    if (const auto it = index.find(guid); it != index.end()) {
        index.erase(it);
    }
}

void NoteStoreServer::putExpungedLinkedNotebookGuid(
    const qevercloud::Guid & guid)
{
    removeLinkedNotebook(guid);
    m_expungedLinkedNotebookGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedLinkedNotebookGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedLinkedNotebookGuids.contains(guid);
}

void NoteStoreServer::removeExpungedLinkedNotebookGuid(
    const qevercloud::Guid & guid)
{
    m_expungedLinkedNotebookGuids.remove(guid);
}

qevercloud::SyncState NoteStoreServer::userOwnSyncState() const
{
    return m_userOwnSyncState;
}

void NoteStoreServer::putUserOwnSyncState(qevercloud::SyncState syncState)
{
    m_userOwnSyncState = std::move(syncState);
}

QHash<qevercloud::Guid, qevercloud::SyncState>
    NoteStoreServer::linkedNotebookSyncStates() const
{
    return m_linkedNotebookSyncStates;
}

void NoteStoreServer::putLinkedNotebookSyncState(
    const qevercloud::Guid & linkedNotebookGuid,
    qevercloud::SyncState syncState)
{
    m_linkedNotebookSyncStates[linkedNotebookGuid] = std::move(syncState);
}

std::optional<qevercloud::SyncState>
    NoteStoreServer::findLinkedNotebookSyncState(
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

void NoteStoreServer::removeLinkedNotebookSyncState(
    const qevercloud::Guid & linkedNotebookGuid)
{
    m_linkedNotebookSyncStates.remove(linkedNotebookGuid);
}

void NoteStoreServer::clearLinkedNotebookSyncStates()
{
    m_linkedNotebookSyncStates.clear();
}

qint32 NoteStoreServer::currentUserOwnMaxUsn() const noexcept
{
    return m_userOwnMaxUsn;
}

std::optional<qint32> NoteStoreServer::currentLinkedNotebookMaxUsn(
    const qevercloud::Guid & linkedNotebookGuid) const noexcept
{
    if (const auto it = m_linkedNotebookMaxUsns.constFind(linkedNotebookGuid);
        it != m_linkedNotebookMaxUsns.constEnd())
    {
        return it.value();
    }

    return std::nullopt;
}

std::optional<std::pair<
    NoteStoreServer::StopSynchronizationErrorTrigger, StopSynchronizationError>>
    NoteStoreServer::stopSynchronizationError() const
{
    if (!m_stopSynchronizationErrorData) {
        return std::nullopt;
    }

    return std::make_pair(
        m_stopSynchronizationErrorData->trigger,
        m_stopSynchronizationErrorData->error);
}

void NoteStoreServer::setStopSynchronizationError(
    const StopSynchronizationErrorTrigger trigger,
    const StopSynchronizationError error)
{
    m_stopSynchronizationErrorData =
        StopSynchronizationErrorData{trigger, error};
}

void NoteStoreServer::clearStopSynchronizationError()
{
    m_stopSynchronizationErrorData.reset();
}

quint32 NoteStoreServer::maxNumSavedSearches() const noexcept
{
    return m_maxNumSavedSearches;
}

void NoteStoreServer::setMaxNumSavedSearches(
    const quint32 maxNumSavedSearches) noexcept
{
    m_maxNumSavedSearches = maxNumSavedSearches;
}

quint32 NoteStoreServer::maxNumTags() const noexcept
{
    return m_maxNumTags;
}

void NoteStoreServer::setMaxNumTags(const quint32 maxNumTags) noexcept
{
    m_maxNumTags = maxNumTags;
}

quint32 NoteStoreServer::maxNumNotebooks() const noexcept
{
    return m_maxNumNotebooks;
}

void NoteStoreServer::setMaxNumNotebooks(const quint32 maxNumNotebooks) noexcept
{
    m_maxNumNotebooks = maxNumNotebooks;
}

quint32 NoteStoreServer::maxNumNotes() const noexcept
{
    return m_maxNumNotes;
}

void NoteStoreServer::setMaxNumNotes(const quint32 maxNumNotes) noexcept
{
    m_maxNumNotes = maxNumNotes;
}

quint64 NoteStoreServer::maxNoteSize() const noexcept
{
    return m_maxNoteSize;
}

void NoteStoreServer::setMaxNoteSize(quint64 maxNoteSize) noexcept
{
    m_maxNoteSize = maxNoteSize;
}

quint32 NoteStoreServer::maxNumResourcesPerNote() const noexcept
{
    return m_maxNumResourcesPerNote;
}

void NoteStoreServer::setMaxNumResourcesPerNote(
    const quint32 maxNumResourcesPerNote) noexcept
{
    m_maxNumResourcesPerNote = maxNumResourcesPerNote;
}

quint32 NoteStoreServer::maxNumTagsPerNote() const noexcept
{
    return m_maxNumTagsPerNote;
}

void NoteStoreServer::setMaxNumTagsPerNote(
    const quint32 maxNumTagsPerNote) noexcept
{
    m_maxNumTagsPerNote = maxNumTagsPerNote;
}

quint64 NoteStoreServer::maxResourceSize() const noexcept
{
    return m_maxResourceSize;
}

void NoteStoreServer::setMaxResourceSize(const quint64 maxResourceSize) noexcept
{
    m_maxResourceSize = maxResourceSize;
}

QHash<qevercloud::Guid, QString>
    NoteStoreServer::linkedNotebookAuthTokensByGuid() const
{
    return m_linkedNotebookAuthTokensByGuid;
}

void NoteStoreServer::setLinkedNotebookAuthTokensByGuid(
    QHash<qevercloud::Guid, QString> tokens)
{
    m_linkedNotebookAuthTokensByGuid = std::move(tokens);
}

void NoteStoreServer::onRequestReady(const QByteArray & responseData)
{
    if (Q_UNLIKELY(!m_tcpSocket)) {
        QFAIL("NoteStoreServer: no socket on ready request");
        return;
    }

    QByteArray buffer;
    buffer.append("HTTP/1.1 200 OK\r\n");
    buffer.append("Content-Length: ");
    buffer.append(QString::number(responseData.size()).toUtf8());
    buffer.append("\r\n");
    buffer.append("Content-Type: application/x-thrift\r\n\r\n");
    buffer.append(responseData);

    if (!utils::writeBufferToSocket(buffer, *m_tcpSocket)) {
        QFAIL("Failed to write response to socket");
    }
}

void NoteStoreServer::onCreateNotebookRequest(
    qevercloud::Notebook notebook, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateNotebook)
    {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (m_notebooks.size() + 1 > m_maxNumNotebooks) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("Notebook"))
                    .build()));
        return;
    }

    if (auto exc = note_store::checkNotebook(notebook)) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    if (notebook.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebook.linkedNotebookGuid(), ctx))
        {
            Q_EMIT createNotebookRequestReady(
                qevercloud::Notebook{},
                std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    if (notebook.linkedNotebookGuid() &&
        notebook.defaultNotebook().value_or(false)) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::PERMISSION_DENIED,
                QStringLiteral("Notebook.defaultNotebook"))));
        return;
    }

    auto & nameIndex = m_notebooks.get<note_store::NotebookByNameUpperTag>();
    const auto nameIt = nameIndex.find(notebook.name()->toUpper());
    if (nameIt != nameIndex.end()) {
        Q_EMIT createNotebookRequestReady(
            qevercloud::Notebook{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_CONFLICT,
                QStringLiteral("Notebook.name"))));
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
                QStringLiteral("Notebook"))));
        return;
    }

    ++(*maxUsn);
    notebook.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    m_notebooks.insert(notebook);
    Q_EMIT createNotebookRequestReady(std::move(notebook), nullptr);
}

void NoteStoreServer::onUpdateNotebookRequest(
    qevercloud::Notebook notebook, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateNotebook)
    {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (!notebook.guid()) {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Notebook.guid"))));
        return;
    }

    if (auto exc = note_store::checkNotebook(notebook)) {
        Q_EMIT updateNotebookRequestReady(
            0, std::make_exception_ptr(std::move(exc)));
        return;
    }

    if (notebook.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebook.linkedNotebookGuid(), ctx))
        {
            Q_EMIT updateNotebookRequestReady(
                0, std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateNotebookRequestReady(
            0, std::make_exception_ptr(std::move(exc)));
        return;
    }

    if (notebook.linkedNotebookGuid() &&
        notebook.defaultNotebook().value_or(false)) {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::PERMISSION_DENIED,
                QStringLiteral("Notebook.defaultNotebook"))));
        return;
    }

    auto & index = m_notebooks.get<note_store::NotebookByGuidTag>();
    const auto it = index.find(*notebook.guid());
    if (it == index.end()) {
        Q_EMIT updateNotebookRequestReady(
            0,
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Notebook.guid"), *notebook.guid())));
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
                QStringLiteral("Notebook"))));
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
                    QStringLiteral("Notebook.name"))));
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
                QStringLiteral("Notebook"))));
        return;
    }

    ++(*maxUsn);
    notebook.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    index.replace(it, std::move(notebook));
    Q_EMIT updateNotebookRequestReady(*maxUsn, nullptr);
}

void NoteStoreServer::onCreateNoteRequest(
    qevercloud::Note note, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateNote)
    {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (m_notes.size() + 1 > m_maxNumNotes) {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("Note"))
                    .build()));
        return;
    }

    if (Q_UNLIKELY(!note.notebookGuid())) {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.notebookGuid"))
                    .build()));
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
                    .build()));
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
                    .build()));
        return;
    }

    if (auto exc = note_store::checkNote(
            note, m_maxNumResourcesPerNote, m_maxNumTagsPerNote))
    {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    if (notebook.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebook.linkedNotebookGuid(), ctx))
        {
            Q_EMIT createNoteRequestReady(
                qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createNoteRequestReady(
            qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    note.setGuid(UidGenerator::Generate());

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
                QStringLiteral("Note"))));
        return;
    }

    ++(*maxUsn);
    note.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    m_notes.insert(note);
    Q_EMIT createNoteRequestReady(std::move(note), nullptr);
}

void NoteStoreServer::onUpdateNoteRequest(
    qevercloud::Note note, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateNote)
    {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (Q_UNLIKELY(!note.guid())) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.guid"))
                    .build()));
        return;
    }

    const auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto noteIt = noteGuidIndex.find(*note.guid());
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.guid"))
                    .setKey(*note.guid())
                    .build()));
    }

    if (Q_UNLIKELY(!note.notebookGuid())) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Note.notebookGuid"))
                    .build()));
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
                    .build()));
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
                    .build()));
        return;
    }

    if (auto exc = note_store::checkNote(
            note, m_maxNumResourcesPerNote, m_maxNumTagsPerNote))
    {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    if (notebook.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebook.linkedNotebookGuid(), ctx))
        {
            Q_EMIT updateNoteRequestReady(
                qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateNoteRequestReady(
            qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
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
                QStringLiteral("Note"))));
        return;
    }

    ++(*maxUsn);
    note.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, notebook.linkedNotebookGuid());

    m_notes.insert(note);
    Q_EMIT updateNoteRequestReady(std::move(note), nullptr);
}

void NoteStoreServer::onCreateTagRequest(
    qevercloud::Tag tag, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateTag)
    {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (m_tags.size() + 1 > m_maxNumTags) {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("Tag"))
                    .build()));
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
                        .build()));
            return;
        }
    }

    if (auto exc = note_store::checkTag(tag)) {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{}, std::make_exception_ptr(std::move(exc)));
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
                    .build()));
        return;
    }

    if (tag.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *tag.linkedNotebookGuid(), ctx))
        {
            Q_EMIT createTagRequestReady(
                qevercloud::Tag{}, std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createTagRequestReady(
            qevercloud::Tag{}, std::make_exception_ptr(std::move(exc)));
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
                QStringLiteral("Tag"))));
        return;
    }

    ++(*maxUsn);
    tag.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, tag.linkedNotebookGuid());

    m_tags.insert(tag);
    Q_EMIT createTagRequestReady(std::move(tag), nullptr);
}

void NoteStoreServer::onUpdateTagRequest(
    qevercloud::Tag tag, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateTag)
    {
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (Q_UNLIKELY(!tag.guid())) {
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Tag.guid"))
                    .build()));
        return;
    }

    const auto & tagGuidIndex = m_tags.get<note_store::TagByGuidTag>();
    const auto tagIt = tagGuidIndex.find(*tag.guid());
    if (Q_UNLIKELY(tagIt == tagGuidIndex.end())) {
        Q_EMIT updateTagRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("Tag.guid"))
                    .setKey(*tag.guid())
                    .build()));
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
                        .build()));
            return;
        }
    }

    if (auto exc = note_store::checkTag(tag)) {
        Q_EMIT updateTagRequestReady(
            0, std::make_exception_ptr(std::move(exc)));
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
                    .build()));
        return;
    }

    if (tag.linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *tag.linkedNotebookGuid(), ctx))
        {
            Q_EMIT updateTagRequestReady(
                0, std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateTagRequestReady(
            0, std::make_exception_ptr(std::move(exc)));
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
                QStringLiteral("Tag"))));
        return;
    }

    ++(*maxUsn);
    tag.setUpdateSequenceNum(maxUsn);
    setMaxUsn(*maxUsn, tag.linkedNotebookGuid());

    m_tags.insert(tag);
    Q_EMIT updateTagRequestReady(*maxUsn, nullptr);
}

void NoteStoreServer::onCreateSavedSearchRequest(
    qevercloud::SavedSearch search, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnCreateSavedSearch)
    {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (m_savedSearches.size() + 1 > m_maxNumSavedSearches) {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{},
            std::make_exception_ptr(
                qevercloud::EDAMUserExceptionBuilder{}
                    .setErrorCode(qevercloud::EDAMErrorCode::LIMIT_REACHED)
                    .setParameter(QStringLiteral("SavedSearch"))
                    .build()));
        return;
    }

    if (auto exc = note_store::checkSavedSearch(search)) {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{}, std::make_exception_ptr(std::move(exc)));
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
                    .build()));
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT createSavedSearchRequestReady(
            qevercloud::SavedSearch{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    search.setGuid(UidGenerator::Generate());

    auto maxUsn = currentUserOwnMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNum(maxUsn);
    m_userOwnMaxUsn = maxUsn;

    m_savedSearches.insert(search);
    Q_EMIT createSavedSearchRequestReady(std::move(search), nullptr);
}

void NoteStoreServer::onUpdateSavedSearchRequest(
    qevercloud::SavedSearch search, const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnUpdateSavedSearch)
    {
        Q_EMIT updateSavedSearchRequestReady(
            0,
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (Q_UNLIKELY(!search.guid())) {
        Q_EMIT updateSavedSearchRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("SavedSearch.guid"))
                    .build()));
        return;
    }

    const auto & savedSearchGuidIndex =
        m_savedSearches.get<note_store::SavedSearchByGuidTag>();

    const auto savedSearchIt = savedSearchGuidIndex.find(*search.guid());
    if (Q_UNLIKELY(savedSearchIt == savedSearchGuidIndex.end())) {
        Q_EMIT updateSavedSearchRequestReady(
            0,
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("SavedSearch.guid"))
                    .setKey(*search.guid())
                    .build()));
        return;
    }

    if (auto exc = note_store::checkSavedSearch(search)) {
        Q_EMIT updateSavedSearchRequestReady(
            0, std::make_exception_ptr(std::move(exc)));
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
                    .build()));
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT updateSavedSearchRequestReady(
            0, std::make_exception_ptr(std::move(exc)));
        return;
    }

    auto maxUsn = currentUserOwnMaxUsn();
    ++maxUsn;
    search.setUpdateSequenceNum(maxUsn);
    m_userOwnMaxUsn = maxUsn;

    m_savedSearches.insert(search);
    Q_EMIT updateSavedSearchRequestReady(maxUsn, nullptr);
}

void NoteStoreServer::onGetSyncStateRequest(
    const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetUserOwnSyncState)
    {
        Q_EMIT getSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getSyncStateRequestReady(
            qevercloud::SyncState{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    Q_EMIT getSyncStateRequestReady(m_userOwnSyncState, nullptr);
}

void NoteStoreServer::onGetLinkedNotebookSyncStateRequest(
    const qevercloud::LinkedNotebook & linkedNotebook,
    const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetLinkedNotebookSyncState)
    {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    if (!linkedNotebook.username()) {
        Q_EMIT getLinkedNotebookSyncStateRequestReady(
            qevercloud::SyncState{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::DATA_REQUIRED,
                QStringLiteral("LinkedNotebook.username"))));
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
                    .build()));
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
                    .build()));
        return;
    }

    Q_EMIT getLinkedNotebookSyncStateRequestReady(it.value(), nullptr);
}

void NoteStoreServer::onGetFilteredSyncChunkRequest(
    const qint32 afterUSN, const qint32 maxEntries,
    const qevercloud::SyncChunkFilter & filter,
    const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetUserOwnSyncChunk)
    {
        Q_EMIT getFilteredSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    auto result = getSyncChunkImpl(
        afterUSN, maxEntries, (afterUSN == 0), std::nullopt, filter, ctx);

    Q_EMIT getFilteredSyncChunkRequestReady(
        std::move(result.first), std::move(result.second));
}

void NoteStoreServer::onGetLinkedNotebookSyncChunkRequest(
    const qevercloud::LinkedNotebook & linkedNotebook, const qint32 afterUSN,
    const qint32 maxEntries, const bool fullSyncOnly,
    const qevercloud::IRequestContextPtr & ctx)
{
    m_onceGetLinkedNotebookSyncChunkCalled = true;

    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnGetLinkedNotebookSyncChunk)
    {
        Q_EMIT getLinkedNotebookSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (!linkedNotebook.username()) {
        Q_EMIT getLinkedNotebookSyncChunkRequestReady(
            qevercloud::SyncChunk{},
            std::make_exception_ptr(
                qevercloud::EDAMNotFoundExceptionBuilder{}
                    .setIdentifier(QStringLiteral("LinkedNotebook"))
                    .build()));
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
                    .build()));
        return;
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
        afterUSN, maxEntries, (afterUSN == 0), std::nullopt, filter, ctx);

    Q_EMIT getLinkedNotebookSyncChunkRequestReady(
        std::move(result.first), std::move(result.second));
}

void NoteStoreServer::onGetNoteWithResultSpecRequest(
    const qevercloud::Guid & guid,
    const qevercloud::NoteResultSpec & resultSpec,
    const qevercloud::IRequestContextPtr & ctx)
{
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
                        m_stopSynchronizationErrorData->error)));
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
                    m_stopSynchronizationErrorData->error)));
            return;
        }
    }

    if (guid.isEmpty()) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Note.guid"))));
        return;
    }

    const auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto noteIt = noteGuidIndex.find(guid);
    if (Q_UNLIKELY(noteIt == noteGuidIndex.end())) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{},
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Note.guid"), guid)));
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
                    .build()));
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
                    .build()));
        return;
    }

    if (notebookIt->linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebookIt->linkedNotebookGuid(), ctx))
        {
            Q_EMIT getNoteWithResultSpecRequestReady(
                qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getNoteWithResultSpecRequestReady(
            qevercloud::Note{}, std::make_exception_ptr(std::move(exc)));
        return;
    }

    auto note = *noteIt;

    note.setLocalId(UidGenerator::Generate());
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
                resource.data()) {
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

    Q_EMIT getNoteWithResultSpecRequestReady(std::move(note), nullptr);
}

void NoteStoreServer::onGetResourceRequest(
    const qevercloud::Guid & guid, bool withData, bool withRecognition,
    bool withAttributes, bool withAlternateData,
    const qevercloud::IRequestContextPtr & ctx)
{
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
                        m_stopSynchronizationErrorData->error)));
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
                    m_stopSynchronizationErrorData->error)));
            return;
        }
    }

    if (guid.isEmpty()) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Resource.guid"))));
        return;
    }

    const auto & resourceGuidIndex =
        m_resources.get<note_store::ResourceByGuidTag>();

    const auto resourceIt = resourceGuidIndex.find(guid);
    if (Q_UNLIKELY(resourceIt == resourceGuidIndex.end())) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{},
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("Resource.guid"), guid)));
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
                    .build()));
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
                    .build()));
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
                    .build()));
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
                    .build()));
        return;
    }

    if (notebookIt->linkedNotebookGuid()) {
        if (auto exc = checkLinkedNotebookAuthentication(
                *notebookIt->linkedNotebookGuid(), ctx))
        {
            Q_EMIT getResourceRequestReady(
                qevercloud::Resource{},
                std::make_exception_ptr(std::move(exc)));
            return;
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT getResourceRequestReady(
            qevercloud::Resource{}, std::make_exception_ptr(std::move(exc)));
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

    Q_EMIT getResourceRequestReady(std::move(resource), nullptr);
}

void NoteStoreServer::onAuthenticateToSharedNotebookRequest(
    const QString & shareKeyOrGlobalId,
    const qevercloud::IRequestContextPtr & ctx)
{
    if (m_stopSynchronizationErrorData &&
        m_stopSynchronizationErrorData->trigger ==
            StopSynchronizationErrorTrigger::OnAuthenticateToSharedNotebook)
    {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{},
            std::make_exception_ptr(utils::createStopSyncException(
                m_stopSynchronizationErrorData->error)));
        return;
    }

    if (auto exc = checkAuthentication(ctx)) {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{},
            std::make_exception_ptr(std::move(exc)));
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
                    .build()));
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
                    .build()));
        return;
    }

    auto authTokenIt =
        m_linkedNotebookAuthTokensByGuid.find(*linkedNotebook.guid());

    if (Q_UNLIKELY(authTokenIt == m_linkedNotebookAuthTokensByGuid.end())) {
        Q_EMIT authenticateToSharedNotebookRequestReady(
            qevercloud::AuthenticationResult{},
            std::make_exception_ptr(utils::createNotFoundException(
                QStringLiteral("SharedNotebook.id"))));
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
        std::move(authResult), nullptr);
}

void NoteStoreServer::connectToQEverCloudServer()
{
    // 1. Connect QEverCloud server's request ready signals to local slot
    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNotebookRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNotebookRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNoteRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNoteRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createTagRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateTagRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createSearchRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateSearchRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getSyncStateRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncStateRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getFilteredSyncChunkRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncChunkRequestReady,
        this, &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getNoteWithResultSpecRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getResourceRequestReady, this,
        &NoteStoreServer::onRequestReady);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::authenticateToSharedNotebookRequestReady,
        this, &NoteStoreServer::onRequestReady);

    // 2. Connect incoming request signals to local slots
    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNotebookRequest, this,
        &NoteStoreServer::onCreateNotebookRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNotebookRequest, this,
        &NoteStoreServer::onUpdateNotebookRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createNoteRequest, this,
        &NoteStoreServer::onCreateNoteRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateNoteRequest, this,
        &NoteStoreServer::onUpdateNoteRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createTagRequest, this,
        &NoteStoreServer::onCreateTagRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateTagRequest, this,
        &NoteStoreServer::onUpdateTagRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::createSearchRequest, this,
        &NoteStoreServer::onCreateSavedSearchRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::updateSearchRequest, this,
        &NoteStoreServer::onUpdateSavedSearchRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getSyncStateRequest, this,
        &NoteStoreServer::onGetSyncStateRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncStateRequest, this,
        &NoteStoreServer::onGetLinkedNotebookSyncStateRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getFilteredSyncChunkRequest,
        this, &NoteStoreServer::onGetFilteredSyncChunkRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::getLinkedNotebookSyncChunkRequest, this,
        &NoteStoreServer::onGetLinkedNotebookSyncChunkRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getNoteWithResultSpecRequest,
        this, &NoteStoreServer::onGetNoteWithResultSpecRequest);

    QObject::connect(
        m_server, &qevercloud::NoteStoreServer::getResourceRequest, this,
        &NoteStoreServer::onGetResourceRequest);

    QObject::connect(
        m_server,
        &qevercloud::NoteStoreServer::authenticateToSharedNotebookRequest, this,
        &NoteStoreServer::onAuthenticateToSharedNotebookRequest);

    // 3. Connect local ready signals to QEverCloud server's slots
    QObject::connect(
        this, &NoteStoreServer::createNotebookRequestReady, m_server,
        &qevercloud::NoteStoreServer::onCreateNotebookRequestReady);

    QObject::connect(
        this, &NoteStoreServer::updateNotebookRequestReady, m_server,
        &qevercloud::NoteStoreServer::onUpdateNotebookRequestReady);

    QObject::connect(
        this, &NoteStoreServer::createNoteRequestReady, m_server,
        &qevercloud::NoteStoreServer::onCreateNoteRequestReady);

    QObject::connect(
        this, &NoteStoreServer::updateNoteRequestReady, m_server,
        &qevercloud::NoteStoreServer::onUpdateNoteRequestReady);

    QObject::connect(
        this, &NoteStoreServer::createTagRequestReady, m_server,
        &qevercloud::NoteStoreServer::onCreateTagRequestReady);

    QObject::connect(
        this, &NoteStoreServer::updateTagRequestReady, m_server,
        &qevercloud::NoteStoreServer::onUpdateTagRequestReady);

    QObject::connect(
        this, &NoteStoreServer::createSavedSearchRequestReady, m_server,
        &qevercloud::NoteStoreServer::onCreateSearchRequestReady);

    QObject::connect(
        this, &NoteStoreServer::updateSavedSearchRequestReady, m_server,
        &qevercloud::NoteStoreServer::onUpdateSearchRequestReady);

    QObject::connect(
        this, &NoteStoreServer::getSyncStateRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetSyncStateRequestReady);

    QObject::connect(
        this, &NoteStoreServer::getLinkedNotebookSyncStateRequestReady,
        m_server,
        &qevercloud::NoteStoreServer::onGetLinkedNotebookSyncStateRequestReady);

    QObject::connect(
        this, &NoteStoreServer::getFilteredSyncChunkRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetFilteredSyncChunkRequestReady);

    QObject::connect(
        this, &NoteStoreServer::getLinkedNotebookSyncChunkRequestReady,
        m_server,
        &qevercloud::NoteStoreServer::onGetLinkedNotebookSyncChunkRequestReady);

    QObject::connect(
        this, &NoteStoreServer::getNoteWithResultSpecRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetNoteWithResultSpecRequestReady);

    QObject::connect(
        this, &NoteStoreServer::getResourceRequestReady, m_server,
        &qevercloud::NoteStoreServer::onGetResourceRequestReady);

    QObject::connect(
        this, &NoteStoreServer::authenticateToSharedNotebookRequestReady,
        m_server,
        &qevercloud::NoteStoreServer::
            onAuthenticateToSharedNotebookRequestReady);
}

void NoteStoreServer::setMaxUsn(
    const qint32 maxUsn,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid)
{
    if (!linkedNotebookGuid) {
        m_userOwnMaxUsn = maxUsn;
        return;
    }

    m_linkedNotebookMaxUsns[*linkedNotebookGuid] = maxUsn;
}

std::pair<qevercloud::SyncChunk, std::exception_ptr>
    NoteStoreServer::getSyncChunkImpl(
        const qint32 afterUsn, const qint32 maxEntries, const bool fullSyncOnly,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        const qevercloud::SyncChunkFilter & filter,
        const qevercloud::IRequestContextPtr & ctx) const
{
    if (linkedNotebookGuid) {
        if (auto exc =
                checkLinkedNotebookAuthentication(*linkedNotebookGuid, ctx)) {
            return std::make_pair(
                qevercloud::SyncChunk{},
                std::make_exception_ptr(std::move(exc)));
        }
    }
    else if (auto exc = checkAuthentication(ctx)) {
        return std::make_pair(
            qevercloud::SyncChunk{}, std::make_exception_ptr(std::move(exc)));
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

    std::optional<qint32> maxUsn = linkedNotebookGuid
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

    auto savedSearchIt = savedSearchUsnIndex.end();
    if (!linkedNotebookGuid && filter.includeSearches() &&
        *filter.includeSearches())
    {
        savedSearchIt = std::upper_bound(
            savedSearchUsnIndex.begin(), savedSearchUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::SavedSearch>());
    }

    auto tagIt = tagUsnIndex.end();
    if (filter.includeTags() && *filter.includeTags()) {
        tagIt = std::upper_bound(
            tagUsnIndex.begin(), tagUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Tag>());

        tagIt = advanceIterator(tagIt, tagUsnIndex, linkedNotebookGuid);
    }

    auto notebookIt = notebookUsnIndex.end();
    if (filter.includeNotebooks() && *filter.includeNotebooks()) {
        notebookIt = std::upper_bound(
            notebookUsnIndex.begin(), notebookUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Notebook>());

        notebookIt =
            advanceIterator(notebookIt, notebookUsnIndex, linkedNotebookGuid);
    }

    auto noteIt = noteUsnIndex.end();
    if (filter.includeNotes() && *filter.includeNotes()) {
        noteIt = std::upper_bound(
            noteUsnIndex.begin(), noteUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Note>());

        noteIt = nextNoteByUsnIterator(noteIt, linkedNotebookGuid);
    }

    auto resourceIt = resourceUsnIndex.end();
    if (!fullSyncOnly && filter.includeResources() &&
        *filter.includeResources()) {
        resourceIt = std::upper_bound(
            resourceUsnIndex.begin(), resourceUsnIndex.end(), afterUsn,
            CompareByUSN<qevercloud::Resource>());

        resourceIt = nextResourceByUsnIterator(resourceIt, linkedNotebookGuid);
    }

    auto linkedNotebookIt = linkedNotebookUsnIndex.end();
    if (!linkedNotebookGuid && filter.includeLinkedNotebooks() &&
        *filter.includeLinkedNotebooks())
    {
        linkedNotebookIt = std::upper_bound(
            linkedNotebookUsnIndex.begin(), linkedNotebookUsnIndex.end(),
            afterUsn, CompareByUSN<qevercloud::LinkedNotebook>());
    }

    while (true) {
        auto nextItemType = NextItemType::None;
        qint32 lastItemUsn = std::numeric_limits<qint32>::max();

        if (savedSearchIt != savedSearchUsnIndex.end()) {
            const auto & nextSearch = *savedSearchIt;
            QNDEBUG(
                "tests::synchronization",
                "Checking saved search for the possibility to include it into "
                    << "the sync chunk: " << nextSearch);

            qint32 usn = nextSearch.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::SavedSearch;
                QNDEBUG(
                    "tests::synchronization",
                    "Will include saved search into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (tagIt != tagUsnIndex.end())) {
            const auto & nextTag = *tagIt;
            QNDEBUG(
                "tests::synchronization",
                "Checking tag for the possibility to include it into the sync "
                    << "chunk: " << nextTag);

            qint32 usn = nextTag.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Tag;
                QNDEBUG(
                    "tests::synchronization",
                    "Will include tag into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (notebookIt != notebookUsnIndex.end()))
        {
            const auto & nextNotebook = *notebookIt;
            QNDEBUG(
                "tests::synchronization",
                "Checking notebook for the possibility to include it into "
                    << "the sync chunk: " << nextNotebook);

            qint32 usn = nextNotebook.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Notebook;
                QNDEBUG(
                    "tests::synchronization",
                    "Will include notebook into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (noteIt != noteUsnIndex.end())) {
            const auto & nextNote = *noteIt;
            QNDEBUG(
                "tests::synchronization",
                "Checking note for the possibility to include it into the sync "
                    << "chunk: " << nextNote);

            qint32 usn = nextNote.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Note;
                QNDEBUG(
                    "tests::synchronization",
                    "Will include note into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (resourceIt != resourceUsnIndex.end()))
        {
            const auto & nextResource = *resourceIt;
            QNDEBUG(
                "tests::synchronization",
                "Checking resource for the possibility to include it into "
                    << "the sync chunk: " << nextResource);

            qint32 usn = nextResource.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                lastItemUsn = usn;
                nextItemType = NextItemType::Resource;
                QNDEBUG(
                    "tests::synchronization",
                    "Will include resource into the sync chunk");
            }
        }

        if ((nextItemType == NextItemType::None) &&
            (linkedNotebookIt != linkedNotebookUsnIndex.end()))
        {
            const auto & nextLinkedNotebook = *linkedNotebookIt;
            QNDEBUG(
                "tests::synchronization",
                "Checking linked notebook for the possibility to include it "
                    << "into the sync chunk: " << nextLinkedNotebook);

            qint32 usn = nextLinkedNotebook.updateSequenceNum().value();
            if (usn < lastItemUsn) {
                nextItemType = NextItemType::LinkedNotebook;
                QNDEBUG(
                    "tests::synchronization",
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

            qevercloud::SavedSearch search = *savedSearchIt;
            search.setLocalId(UidGenerator::Generate());
            search.setLocalData({});
            search.setLocalOnly(false);
            search.setLocallyModified(false);
            search.setLocallyFavorited(false);

            syncChunk.mutableSearches()->append(search);

            syncChunk.setChunkHighUSN(
                savedSearchIt->updateSequenceNum().value());

            QNDEBUG(
                "tests::synchronization",
                "Added saved search to sync chunk: "
                    << *savedSearchIt << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

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
            syncChunk.setChunkHighUSN(tagIt->updateSequenceNum().value());
            QNDEBUG(
                "tests::synchronization",
                "Added tag to sync chunk: "
                    << *tagIt << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

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
            syncChunk.setChunkHighUSN(notebookIt->updateSequenceNum().value());

            QNDEBUG(
                "tests::synchronization",
                "Added notebook to sync chunk: "
                    << *notebookIt << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

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
            qecNote.setLocalId(UidGenerator::Generate());
            qecNote.setLocalData({});
            qecNote.setLocalOnly(false);
            qecNote.setLocallyModified(false);
            qecNote.setLocallyFavorited(false);
            qecNote.setTagLocalIds(QStringList{});
            qecNote.setNotebookLocalId(QString{});

            if (!filter.includeNoteResources() ||
                !*filter.includeNoteResources()) {
                qecNote.setResources(std::nullopt);
            }

            if (!filter.includeNoteAttributes() ||
                !*filter.includeNoteAttributes()) {
                qecNote.setAttributes(std::nullopt);
            }
            else {
                if ((!filter.includeNoteApplicationDataFullMap() ||
                     !*filter.includeNoteApplicationDataFullMap()) &&
                    qecNote.attributes() &&
                    qecNote.attributes()->applicationData())
                {
                    qecNote.mutableAttributes()
                        ->mutableApplicationData()
                        ->setFullMap(std::nullopt);
                }

                if ((!filter.includeNoteResourceApplicationDataFullMap() ||
                     !*filter.includeNoteResourceApplicationDataFullMap()) &&
                    qecNote.resources())
                {
                    for (auto & resource: *qecNote.mutableResources()) {
                        if (resource.attributes() &&
                            resource.attributes()->applicationData()) {
                            resource.mutableAttributes()
                                ->mutableApplicationData()
                                ->setFullMap(std::nullopt);
                        }
                    }
                }
            }

            if (!filter.includeSharedNotes() || !*filter.includeSharedNotes()) {
                qecNote.setSharedNotes(std::nullopt);
            }

            // Notes within the sync chunks should include only note
            // metadata bt no content, resource content, resource
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
            syncChunk.setChunkHighUSN(noteIt->updateSequenceNum().value());

            QNDEBUG(
                "tests::synchronization",
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
            syncChunk.setChunkHighUSN(resourceIt->updateSequenceNum().value());

            QNDEBUG(
                "tests::synchronization",
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
                "tests::synchronization",
                "Added linked notebook to sync chunk: "
                    << *linkedNotebookIt << "\nSync chunk high USN updated to "
                    << *syncChunk.chunkHighUSN());

            ++linkedNotebookIt;
        } break;
        default:
            QNWARNING(
                "tests::synchronization",
                "Unexpected next item type: " << nextItemType);
            break;
        }
    }

    if (!syncChunk.chunkHighUSN()) {
        syncChunk.setChunkHighUSN(syncChunk.updateCount());
        QNDEBUG(
            "tests::synchronization",
            "Sync chunk's high USN was still not set, set it to the update "
                << "count: " << syncChunk.updateCount());
    }

    if (fullSyncOnly) {
        // No need to insert the information about expunged data items
        // when doing full sync
        return std::make_pair(std::move(syncChunk), nullptr);
    }

    if (!linkedNotebookGuid && !m_expungedSavedSearchGuids.isEmpty()) {
        if (!syncChunk.expungedSearches()) {
            syncChunk.setExpungedSearches(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedSearches()->reserve(
            m_expungedSavedSearchGuids.size());

        for (const auto & guid: qAsConst(m_expungedSavedSearchGuids)) {
            syncChunk.mutableExpungedSearches()->append(guid);
        }
    }

    if (!linkedNotebookGuid && !m_expungedUserOwnTagGuids.isEmpty()) {
        if (!syncChunk.expungedTags()) {
            syncChunk.setExpungedTags(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedTags()->reserve(
            m_expungedUserOwnTagGuids.size());

        for (const auto & guid: qAsConst(m_expungedUserOwnTagGuids)) {
            syncChunk.mutableExpungedTags()->append(guid);
        }
    }
    else if (linkedNotebookGuid) {
        const auto it =
            m_expungedLinkedNotebookTagGuids.constFind(*linkedNotebookGuid);
        if (it != m_expungedLinkedNotebookTagGuids.constEnd()) {
            const auto & expungedTagGuids = it.value();

            if (!syncChunk.expungedTags()) {
                syncChunk.setExpungedTags(QList<qevercloud::Guid>{});
            }

            syncChunk.mutableExpungedTags()->reserve(expungedTagGuids.size());

            for (const auto & guid: qAsConst(expungedTagGuids)) {
                syncChunk.mutableExpungedTags()->append(guid);
            }
        }
    }

    if (!linkedNotebookGuid && !m_expungedUserOwnNotebookGuids.isEmpty()) {
        if (!syncChunk.expungedNotebooks()) {
            syncChunk.setExpungedNotebooks(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedNotebooks()->reserve(
            m_expungedUserOwnNotebookGuids.size());

        for (const auto & guid: qAsConst(m_expungedUserOwnNotebookGuids)) {
            syncChunk.mutableExpungedNotebooks()->append(guid);
        }
    }
    else if (linkedNotebookGuid) {
        const auto it = m_expungedLinkedNotebookNotebookGuids.constFind(
            *linkedNotebookGuid);
        if (it != m_expungedLinkedNotebookNotebookGuids.constEnd()) {
            const auto & expungedNotebookGuids = it.value();

            if (!syncChunk.expungedNotebooks()) {
                syncChunk.setExpungedNotebooks(QList<qevercloud::Guid>{});
            }

            syncChunk.mutableExpungedNotebooks()->reserve(
                expungedNotebookGuids.size());

            for (const auto & guid: qAsConst(expungedNotebookGuids)) {
                syncChunk.mutableExpungedNotebooks()->append(guid);
            }
        }
    }

    if (!linkedNotebookGuid && !m_expungedUserOwnNoteGuids.isEmpty()) {
        if (!syncChunk.expungedNotes()) {
            syncChunk.setExpungedNotes(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedNotes()->reserve(
            m_expungedUserOwnNoteGuids.size());

        for (const auto & guid: qAsConst(m_expungedUserOwnNoteGuids)) {
            syncChunk.mutableExpungedNotes()->append(guid);
        }
    }
    else if (linkedNotebookGuid) {
        const auto it =
            m_expungedLinkedNotebookNoteGuids.constFind(*linkedNotebookGuid);
        if (it != m_expungedLinkedNotebookNoteGuids.constEnd()) {
            const auto & expungedNoteGuids = it.value();

            if (!syncChunk.expungedNotes()) {
                syncChunk.setExpungedNotes(QList<qevercloud::Guid>{});
            }

            syncChunk.mutableExpungedNotes()->reserve(expungedNoteGuids.size());

            for (const auto & guid: qAsConst(expungedNoteGuids)) {
                syncChunk.mutableExpungedNotes()->append(guid);
            }
        }
    }

    if (!linkedNotebookGuid && !m_expungedLinkedNotebookGuids.isEmpty()) {
        if (!syncChunk.expungedLinkedNotebooks()) {
            syncChunk.setExpungedLinkedNotebooks(QList<qevercloud::Guid>{});
        }

        syncChunk.mutableExpungedLinkedNotebooks()->reserve(
            m_expungedLinkedNotebookGuids.size());

        for (auto it = m_expungedLinkedNotebookGuids.constBegin(),
                  end = m_expungedLinkedNotebookGuids.constEnd();
             it != end; ++it)
        {
            syncChunk.mutableExpungedLinkedNotebooks()->append(*it);
        }
    }

    return std::make_pair(std::move(syncChunk), nullptr);
}

note_store::NotesByUSN::const_iterator NoteStoreServer::nextNoteByUsnIterator(
    note_store::NotesByUSN::const_iterator it,
    const std::optional<qevercloud::Guid> & targetLinkedNotebookGuid) const
{
    const auto & noteUsnIndex = m_notes.get<note_store::NoteByUSNTag>();
    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    while (it != noteUsnIndex.end()) {
        const QString & notebookGuid = it->notebookGuid().value();
        auto noteNotebookIt = notebookGuidIndex.find(notebookGuid);
        if (Q_UNLIKELY(noteNotebookIt == notebookGuidIndex.end())) {
            QNWARNING(
                "tests::synchronization",
                "Found note which notebook guid "
                    << "doesn't correspond to any existing notebook: " << *it);
            ++it;
            continue;
        }

        const auto & notebook = *noteNotebookIt;
        if (notebook.linkedNotebookGuid() &&
            (!targetLinkedNotebookGuid ||
             (*notebook.linkedNotebookGuid() != *targetLinkedNotebookGuid)))
        {
            ++it;
            continue;
        }

        if (!notebook.linkedNotebookGuid() && targetLinkedNotebookGuid) {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

note_store::ResourcesByUSN::const_iterator
    NoteStoreServer::nextResourceByUsnIterator(
        note_store::ResourcesByUSN::const_iterator it,
        const std::optional<qevercloud::Guid> & targetLinkedNotebookGuid) const
{
    const auto & resourceUsnIndex =
        m_resources.get<note_store::ResourceByUSNTag>();

    const auto & noteGuidIndex = m_notes.get<note_store::NoteByGuidTag>();
    const auto & notebookGuidIndex =
        m_notebooks.get<note_store::NotebookByGuidTag>();

    while (it != resourceUsnIndex.end()) {
        const QString & noteGuid = it->noteGuid().value();

        auto resourceNoteIt = noteGuidIndex.find(noteGuid);
        if (Q_UNLIKELY(resourceNoteIt == noteGuidIndex.end())) {
            QNWARNING(
                "tests::synchronization",
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
                "tests::synchronization",
                "Found note which notebook guid "
                    << "doesn't correspond to any existing notebook: " << note);
            ++it;
            continue;
        }

        const auto & notebook = *noteNotebookIt;
        if (notebook.linkedNotebookGuid() &&
            (!targetLinkedNotebookGuid ||
             (notebook.linkedNotebookGuid() != *targetLinkedNotebookGuid)))
        {
            ++it;
            continue;
        }

        if (!notebook.linkedNotebookGuid() && targetLinkedNotebookGuid) {
            ++it;
            continue;
        }

        break;
    }

    return it;
}

} // namespace quentier::synchronization::tests
