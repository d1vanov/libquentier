/*
 * Copyright 2021 Dmitry Ivanov
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

#include "ILinkedNotebooksHandler.h"
#include "INotebooksHandler.h"
#include "INotesHandler.h"
#include "IResourcesHandler.h"
#include "ISavedSearchesHandler.h"
#include "ISynchronizationInfoHandler.h"
#include "ITagsHandler.h"
#include "IUsersHandler.h"
#include "IVersionHandler.h"
#include "LocalStorage.h"

#include <quentier/exception/InvalidArgument.h>

namespace quentier::local_storage::sql {

LocalStorage::LocalStorage(
    ILinkedNotebooksHandlerPtr linkedNotebooksHandler,
    INotebooksHandlerPtr notebooksHandler, INotesHandlerPtr notesHandler,
    IResourcesHandlerPtr resourcesHandler,
    ISavedSearchesHandlerPtr savedSearchesHandler,
    ISynchronizationInfoHandlerPtr synchronizationInfoHandler,
    ITagsHandlerPtr tagsHandler, IVersionHandlerPtr versionHandler,
    IUsersHandlerPtr usersHandler, ILocalStorageNotifier * notifier) :
    m_linkedNotebooksHandler{std::move(linkedNotebooksHandler)},
    m_notebooksHandler{std::move(notebooksHandler)},
    m_notesHandler{std::move(notesHandler)}, m_resourcesHandler{std::move(
                                                 resourcesHandler)},
    m_savedSearchesHandler{std::move(savedSearchesHandler)},
    m_synchronizationInfoHandler{std::move(synchronizationInfoHandler)},
    m_tagsHandler{std::move(tagsHandler)}, m_versionHandler{std::move(
                                               versionHandler)},
    m_usersHandler{std::move(usersHandler)}, m_notifier{notifier}
{
    if (Q_UNLIKELY(!m_linkedNotebooksHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: linked notebooks handler is null")}};
    }

    if (Q_UNLIKELY(!m_notebooksHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: notebooks handler is null")}};
    }

    if (Q_UNLIKELY(!m_notesHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: notes handler is null")}};
    }

    if (Q_UNLIKELY(!m_resourcesHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: resources handler is null")}};
    }

    if (Q_UNLIKELY(!m_savedSearchesHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: saved searhes handler is null")}};
    }

    if (Q_UNLIKELY(!m_synchronizationInfoHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: synchronization info handler is null")}};
    }

    if (Q_UNLIKELY(!m_tagsHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: tags handler is null")}};
    }

    if (Q_UNLIKELY(!m_versionHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: version handler is null")}};
    }

    if (Q_UNLIKELY(!m_usersHandler)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: users handler is null")}};
    }

    if (Q_UNLIKELY(!m_notifier)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::LocalStorage",
            "LocalStorage ctor: notifier is null")}};
    }
}

QFuture<bool> LocalStorage::isVersionTooHigh() const
{
    return m_versionHandler->isVersionTooHigh();
}

QFuture<bool> LocalStorage::requiresUpgrade() const
{
    return m_versionHandler->requiresUpgrade();
}

QFuture<QList<IPatchPtr>> LocalStorage::requiredPatches() const
{
    return m_versionHandler->requiredPatches();
}

QFuture<qint32> LocalStorage::version() const
{
    return m_versionHandler->version();
}

QFuture<qint32> LocalStorage::highestSupportedVersion() const
{
    return m_versionHandler->highestSupportedVersion();
}

QFuture<quint32> LocalStorage::userCount() const
{
    return m_usersHandler->userCount();
}

QFuture<void> LocalStorage::putUser(qevercloud::User user)
{
    return m_usersHandler->putUser(std::move(user));
}

QFuture<qevercloud::User> LocalStorage::findUserById(
    qevercloud::UserID userId) const
{
    return m_usersHandler->findUserById(userId);
}

QFuture<void> LocalStorage::expungeUserById(qevercloud::UserID userId)
{
    return m_usersHandler->expungeUserById(userId);
}

QFuture<quint32> LocalStorage::notebookCount() const
{
    return m_notebooksHandler->notebookCount();
}

QFuture<void> LocalStorage::putNotebook(qevercloud::Notebook notebook)
{
    return m_notebooksHandler->putNotebook(std::move(notebook));
}

QFuture<std::optional<qevercloud::Notebook>>
    LocalStorage::findNotebookByLocalId(QString notebookLocalId) const
{
    return m_notebooksHandler->findNotebookByLocalId(
        std::move(notebookLocalId));
}

QFuture<std::optional<qevercloud::Notebook>> LocalStorage::findNotebookByGuid(
    qevercloud::Guid guid) const
{
    return m_notebooksHandler->findNotebookByGuid(std::move(guid));
}

QFuture<std::optional<qevercloud::Notebook>> LocalStorage::findNotebookByName(
    QString notebookName,
    std::optional<qevercloud::Guid> linkedNotebookGuid) const
{
    return m_notebooksHandler->findNotebookByName(
        std::move(notebookName), std::move(linkedNotebookGuid));
}

QFuture<std::optional<qevercloud::Notebook>> LocalStorage::findDefaultNotebook()
    const
{
    return m_notebooksHandler->findDefaultNotebook();
}

QFuture<void> LocalStorage::expungeNotebookByLocalId(QString notebookLocalId)
{
    return m_notebooksHandler->expungeNotebookByLocalId(
        std::move(notebookLocalId));
}

QFuture<void> LocalStorage::expungeNotebookByGuid(qevercloud::Guid notebookGuid)
{
    return m_notebooksHandler->expungeNotebookByGuid(std::move(notebookGuid));
}

QFuture<void> LocalStorage::expungeNotebookByName(
    QString name, std::optional<qevercloud::Guid> linkedNotebookGuid)
{
    return m_notebooksHandler->expungeNotebookByName(
        std::move(name), std::move(linkedNotebookGuid));
}

QFuture<QList<qevercloud::Notebook>> LocalStorage::listNotebooks(
    ListNotebooksOptions options) const
{
    return m_notebooksHandler->listNotebooks(std::move(options));
}

QFuture<QList<qevercloud::SharedNotebook>> LocalStorage::listSharedNotebooks(
    qevercloud::Guid notebookGuid) const
{
    return m_notebooksHandler->listSharedNotebooks(std::move(notebookGuid));
}

QFuture<quint32> LocalStorage::linkedNotebookCount() const
{
    return m_linkedNotebooksHandler->linkedNotebookCount();
}

QFuture<void> LocalStorage::putLinkedNotebook(
    qevercloud::LinkedNotebook linkedNotebook)
{
    return m_linkedNotebooksHandler->putLinkedNotebook(
        std::move(linkedNotebook));
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LocalStorage::findLinkedNotebookByGuid(qevercloud::Guid guid) const
{
    return m_linkedNotebooksHandler->findLinkedNotebookByGuid(std::move(guid));
}

QFuture<void> LocalStorage::expungeLinkedNotebookByGuid(qevercloud::Guid guid)
{
    return m_linkedNotebooksHandler->expungeLinkedNotebookByGuid(
        std::move(guid));
}

QFuture<QList<qevercloud::LinkedNotebook>> LocalStorage::listLinkedNotebooks(
    ListLinkedNotebooksOptions options) const
{
    return m_linkedNotebooksHandler->listLinkedNotebooks(options);
}

QFuture<quint32> LocalStorage::noteCount(NoteCountOptions options) const
{
    return m_notesHandler->noteCount(options);
}

QFuture<quint32> LocalStorage::noteCountPerNotebookLocalId(
    QString notebookLocalId, NoteCountOptions options) const
{
    return m_notesHandler->noteCountPerNotebookLocalId(
        std::move(notebookLocalId), options);
}

QFuture<quint32> LocalStorage::noteCountPerTagLocalId(
    QString tagLocalId, NoteCountOptions options) const
{
    return m_notesHandler->noteCountPerTagLocalId(
        std::move(tagLocalId), options);
}

QFuture<QHash<QString, quint32>> LocalStorage::noteCountsPerTags(
    ListTagsOptions listTagsOptions, NoteCountOptions options) const
{
    return m_notesHandler->noteCountsPerTags(
        std::move(listTagsOptions), options);
}

QFuture<quint32> LocalStorage::noteCountPerNotebookAndTagLocalIds(
    QStringList notebookLocalIds, QStringList tagLocalIds,
    NoteCountOptions options) const
{
    return m_notesHandler->noteCountPerNotebookAndTagLocalIds(
        std::move(notebookLocalIds), std::move(tagLocalIds), options);
}

QFuture<void> LocalStorage::putNote(qevercloud::Note note)
{
    return m_notesHandler->putNote(std::move(note));
}

QFuture<void> LocalStorage::updateNote(
    qevercloud::Note note, UpdateNoteOptions options)
{
    return m_notesHandler->updateNote(std::move(note), options);
}

QFuture<std::optional<qevercloud::Note>> LocalStorage::findNoteByLocalId(
    QString noteLocalId, FetchNoteOptions options) const
{
    return m_notesHandler->findNoteByLocalId(std::move(noteLocalId), options);
}

QFuture<std::optional<qevercloud::Note>> LocalStorage::findNoteByGuid(
    qevercloud::Guid noteGuid, FetchNoteOptions options) const
{
    return m_notesHandler->findNoteByGuid(std::move(noteGuid), options);
}

QFuture<QList<qevercloud::Note>> LocalStorage::listNotes(
    FetchNoteOptions fetchOptions, ListNotesOptions listOptions) const
{
    return m_notesHandler->listNotes(fetchOptions, listOptions);
}

QFuture<QList<qevercloud::Note>> LocalStorage::listNotesPerNotebookLocalId(
    QString notebookLocalId, FetchNoteOptions fetchOptions,
    ListNotesOptions listOptions) const
{
    return m_notesHandler->listNotesPerNotebookLocalId(
        std::move(notebookLocalId), fetchOptions, listOptions);
}

QFuture<QList<qevercloud::Note>> LocalStorage::listNotesPerTagLocalId(
    QString tagLocalId, FetchNoteOptions fetchOptions,
    ListNotesOptions listOptions) const
{
    return m_notesHandler->listNotesPerTagLocalId(
        std::move(tagLocalId), fetchOptions, listOptions);
}

QFuture<QList<qevercloud::Note>>
    LocalStorage::listNotesPerNotebookAndTagLocalIds(
        QStringList notebookLocalIds, QStringList tagLocalIds,
        FetchNoteOptions fetchOptions, ListNotesOptions listOptions) const
{
    return m_notesHandler->listNotesPerNotebookAndTagLocalIds(
        std::move(notebookLocalIds), std::move(tagLocalIds), fetchOptions,
        listOptions);
}

QFuture<QList<qevercloud::Note>> LocalStorage::listNotesByLocalIds(
    QStringList noteLocalIds, FetchNoteOptions fetchOptions,
    ListNotesOptions listOptions) const
{
    return m_notesHandler->listNotesByLocalIds(
        std::move(noteLocalIds), fetchOptions, listOptions);
}

QFuture<QList<qevercloud::Note>> LocalStorage::queryNotes(
    NoteSearchQuery query, FetchNoteOptions fetchOptions) const
{
    return m_notesHandler->queryNotes(std::move(query), fetchOptions);
}

QFuture<QStringList> LocalStorage::queryNoteLocalIds(
    NoteSearchQuery query) const
{
    return m_notesHandler->queryNoteLocalIds(std::move(query));
}

QFuture<void> LocalStorage::expungeNoteByLocalId(QString noteLocalId)
{
    return m_notesHandler->expungeNoteByLocalId(std::move(noteLocalId));
}

QFuture<void> LocalStorage::expungeNoteByGuid(qevercloud::Guid noteGuid)
{
    return m_notesHandler->expungeNoteByGuid(std::move(noteGuid));
}

QFuture<quint32> LocalStorage::tagCount() const
{
    return m_tagsHandler->tagCount();
}

QFuture<void> LocalStorage::putTag(qevercloud::Tag tag)
{
    return m_tagsHandler->putTag(std::move(tag));
}

QFuture<qevercloud::Tag> LocalStorage::findTagByLocalId(
    QString tagLocalId) const
{
    return m_tagsHandler->findTagByLocalId(std::move(tagLocalId));
}

QFuture<qevercloud::Tag> LocalStorage::findTagByGuid(
    qevercloud::Guid tagGuid) const
{
    return m_tagsHandler->findTagByGuid(std::move(tagGuid));
}

QFuture<qevercloud::Tag> LocalStorage::findTagByName(
    QString tagName, std::optional<qevercloud::Guid> linkedNotebookGuid) const
{
    return m_tagsHandler->findTagByName(
        std::move(tagName), std::move(linkedNotebookGuid));
}

QFuture<QList<qevercloud::Tag>> LocalStorage::listTags(
    ListTagsOptions options) const
{
    return m_tagsHandler->listTags(options);
}

QFuture<QList<qevercloud::Tag>> LocalStorage::listTagsPerNoteLocalId(
    QString noteLocalId, ListTagsOptions options) const
{
    return m_tagsHandler->listTagsPerNoteLocalId(
        std::move(noteLocalId), options);
}

QFuture<void> LocalStorage::expungeTagByLocalId(QString tagLocalId)
{
    return m_tagsHandler->expungeTagByLocalId(std::move(tagLocalId));
}

QFuture<void> LocalStorage::expungeTagByGuid(qevercloud::Guid tagGuid)
{
    return m_tagsHandler->expungeTagByGuid(std::move(tagGuid));
}

QFuture<void> LocalStorage::expungeTagByName(
    QString name, std::optional<qevercloud::Guid> linkedNotebookGuid)
{
    return m_tagsHandler->expungeTagByName(
        std::move(name), std::move(linkedNotebookGuid));
}

QFuture<quint32> LocalStorage::resourceCount(NoteCountOptions options) const
{
    return m_resourcesHandler->resourceCount(options);
}

QFuture<quint32> LocalStorage::resourceCountPerNoteLocalId(
    QString noteLocalId) const
{
    return m_resourcesHandler->resourceCountPerNoteLocalId(
        std::move(noteLocalId));
}

QFuture<void> LocalStorage::putResource(
    qevercloud::Resource resource, int indexInNote)
{
    return m_resourcesHandler->putResource(std::move(resource), indexInNote);
}

QFuture<std::optional<qevercloud::Resource>>
    LocalStorage::findResourceByLocalId(
        QString resourceLocalId, FetchResourceOptions options) const
{
    return m_resourcesHandler->findResourceByLocalId(
        std::move(resourceLocalId), options);
}

QFuture<std::optional<qevercloud::Resource>> LocalStorage::findResourceByGuid(
    qevercloud::Guid resourceGuid, FetchResourceOptions options) const
{
    return m_resourcesHandler->findResourceByGuid(
        std::move(resourceGuid), options);
}

QFuture<void> LocalStorage::expungeResourceByLocalId(QString resourceLocalId)
{
    return m_resourcesHandler->expungeResourceByLocalId(
        std::move(resourceLocalId));
}

QFuture<void> LocalStorage::expungeResourceByGuid(qevercloud::Guid resourceGuid)
{
    return m_resourcesHandler->expungeResourceByGuid(std::move(resourceGuid));
}

QFuture<quint32> LocalStorage::savedSearchCount() const
{
    return m_savedSearchesHandler->savedSearchCount();
}

QFuture<void> LocalStorage::putSavedSearch(qevercloud::SavedSearch search)
{
    return m_savedSearchesHandler->putSavedSearch(std::move(search));
}

QFuture<std::optional<qevercloud::SavedSearch>>
    LocalStorage::findSavedSearchByLocalId(QString savedSearchLocalId) const
{
    return m_savedSearchesHandler->findSavedSearchByLocalId(
        std::move(savedSearchLocalId));
}

QFuture<std::optional<qevercloud::SavedSearch>>
    LocalStorage::findSavedSearchByGuid(qevercloud::Guid guid) const
{
    return m_savedSearchesHandler->findSavedSearchByGuid(std::move(guid));
}

QFuture<std::optional<qevercloud::SavedSearch>>
    LocalStorage::findSavedSearchByName(QString name) const
{
    return m_savedSearchesHandler->findSavedSearchByName(std::move(name));
}

QFuture<QList<qevercloud::SavedSearch>> LocalStorage::listSavedSearches(
    ListSavedSearchesOptions options) const
{
    return m_savedSearchesHandler->listSavedSearches(options);
}

QFuture<void> LocalStorage::expungeSavedSearchByLocalId(
    QString savedSearchLocalId)
{
    return m_savedSearchesHandler->expungeSavedSearchByLocalId(
        std::move(savedSearchLocalId));
}

QFuture<void> LocalStorage::expungeSavedSearchByGuid(qevercloud::Guid guid)
{
    return m_savedSearchesHandler->expungeSavedSearchByGuid(std::move(guid));
}

QFuture<qint32> LocalStorage::highestUpdateSequenceNumber(
    HighestUsnOption option) const
{
    return m_synchronizationInfoHandler->highestUpdateSequenceNumber(option);
}

QFuture<qint32> LocalStorage::highestUpdateSequenceNumber(
    qevercloud::Guid linkedNotebookGuid) const
{
    return m_synchronizationInfoHandler->highestUpdateSequenceNumber(
        std::move(linkedNotebookGuid));
}

ILocalStorageNotifier * LocalStorage::notifier() const noexcept
{
    return m_notifier;
}

} // namespace quentier::local_storage::sql
