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

#include "utils/HttpUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/services/NoteStoreServer.h>

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest/QtTest>

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

} // namespace

NoteStoreServer::NoteStoreServer(
    QString authenticationToken, QList<QNetworkCookie> cookies,
    QHash<qevercloud::Guid, QString> linkedNotebookAuthTokensByGuid,
    QObject * parent) :
    QObject(parent),
    m_authenticationToken{std::move(authenticationToken)}, m_cookies{std::move(
                                                               cookies)},
    m_linkedNotebookAuthTokensByGuid{std::move(linkedNotebookAuthTokensByGuid)}
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

    if (!search.updateSequenceNum()) {
        qint32 maxUsn = currentUserOwnMaxUsn();
        ++maxUsn;
        search.setUpdateSequenceNum(maxUsn);
        result.usn = maxUsn;
    }

    removeExpungedSavedSearchGuid(*search.guid());

    auto & savedSearchGuidIndex =
        m_savedSearches.get<note_store::SavedSearchByGuidTag>();

    const auto it = savedSearchGuidIndex.find(*search.guid());
    if (it == savedSearchGuidIndex.end()) {
        m_savedSearches.insert(search);
    }
    else {
        savedSearchGuidIndex.replace(it, search);
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

    if (!tag.updateSequenceNum()) {
        std::optional<qint32> maxUsn = tag.linkedNotebookGuid()
            ? currentLinkedNotebookMaxUsn(*tag.linkedNotebookGuid())
            : std::make_optional(currentUserOwnMaxUsn());

        if (Q_UNLIKELY(!maxUsn)) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "Failed to find max USN on attempt to put tag")}};
        }

        ++(*maxUsn);
        tag.setUpdateSequenceNum(maxUsn);
        result.usn = *maxUsn;
    }

    if (!tag.linkedNotebookGuid()) {
        removeExpungedTagGuid(*tag.guid());
    }

    auto & tagGuidIndex = m_tags.get<note_store::TagByGuidTag>();
    const auto tagIt = tagGuidIndex.find(*tag.guid());
    if (tagIt == tagGuidIndex.end()) {
        m_tags.insert(tag);
    }
    else {
        tagGuidIndex.replace(tagIt, tag);
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

void NoteStoreServer::putExpungedTagGuid(const qevercloud::Guid & guid)
{
    removeTag(guid);
    m_expungedTagGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedTagGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedTagGuids.contains(guid);
}

void NoteStoreServer::removeExpungedTagGuid(const qevercloud::Guid & guid)
{
    m_expungedTagGuids.remove(guid);
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

    if (!notebook.updateSequenceNum()) {
        std::optional<qint32> maxUsn = notebook.linkedNotebookGuid()
            ? currentLinkedNotebookMaxUsn(*notebook.linkedNotebookGuid())
            : std::make_optional(currentUserOwnMaxUsn());

        if (Q_UNLIKELY(!maxUsn)) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "Failed to find max USN on attempt to put notebook")}};
        }

        ++(*maxUsn);
        notebook.setUpdateSequenceNum(maxUsn);
        result.usn = *maxUsn;
    }

    if (!notebook.linkedNotebookGuid()) {
        removeExpungedNotebookGuid(*notebook.guid());
    }

    auto & notebookGuidIndex = m_notebooks.get<note_store::NotebookByGuidTag>();
    const auto notebookIt = notebookGuidIndex.find(*notebook.guid());
    if (notebookIt == notebookGuidIndex.end()) {
        m_notebooks.insert(notebook);
    }
    else {
        notebookGuidIndex.replace(notebookIt, notebook);
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

void NoteStoreServer::putExpungedNotebookGuid(const qevercloud::Guid & guid)
{
    removeNotebook(guid);
    m_expungedNotebookGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedNotebookGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedNotebookGuids.contains(guid);
}

void NoteStoreServer::removeExpungedNotebookGuid(const qevercloud::Guid & guid)
{
    m_expungedNotebookGuids.remove(guid);
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
    ItemData result;

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

    if (!note.guid()) {
        result.guid = UidGenerator::Generate();
        note.setGuid(result.guid);
    }

    if (!note.updateSequenceNum()) {
        std::optional<qint32> maxUsn = notebookIt->linkedNotebookGuid()
            ? currentLinkedNotebookMaxUsn(*notebookIt->linkedNotebookGuid())
            : std::make_optional(currentUserOwnMaxUsn());

        if (Q_UNLIKELY(!maxUsn)) {
            throw InvalidArgument{ErrorString{QStringLiteral(
                "Failed to find max USN on attempt to put note")}};
        }

        ++(*maxUsn);
        note.setUpdateSequenceNum(maxUsn);
        result.usn = *maxUsn;
    }

    if (!notebookIt->linkedNotebookGuid()) {
        removeExpungedNoteGuid(*note.guid());
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

void NoteStoreServer::putExpungedNoteGuid(const qevercloud::Guid & guid)
{
    removeNote(guid);
    m_expungedNoteGuids.insert(guid);
}

bool NoteStoreServer::containsExpungedNoteGuid(
    const qevercloud::Guid & guid) const
{
    return m_expungedNoteGuids.contains(guid);
}

void NoteStoreServer::removeExpungedNoteGuid(const qevercloud::Guid & guid)
{
    m_expungedNoteGuids.remove(guid);
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
        this, &NoteStoreServer::getLinkedNotebookSyncStateReady, m_server,
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

} // namespace quentier::synchronization::tests
