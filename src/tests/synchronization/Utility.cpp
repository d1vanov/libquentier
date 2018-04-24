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

#include "Utility.h"
#include <quentier/types/ErrorString.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>

namespace quentier {

bool listSavedSearchesFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                                       const qint32 afterUSN,
                                       QHash<QString, qevercloud::SavedSearch> & savedSearches,
                                       ErrorString & errorDescription)
{
    savedSearches.clear();

    const LocalStorageManager * pLocalStorageManager = localStorageManagerAsync.localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        errorDescription.setBase(QStringLiteral("Local storage manager is null"));
        return false;
    }

    errorDescription.clear();
    QList<SavedSearch> searches = pLocalStorageManager->listSavedSearches(LocalStorageManager::ListAll, errorDescription);
    if (searches.isEmpty() && !errorDescription.isEmpty()) {
        return false;
    }

    savedSearches.reserve(searches.size());
    for(auto it = searches.constBegin(), end = searches.constEnd(); it != end; ++it)
    {
        const SavedSearch & search = *it;
        if (Q_UNLIKELY(!search.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!search.hasUpdateSequenceNumber() || (search.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        savedSearches[search.guid()] = search.qevercloudSavedSearch();
    }

    return true;
}

bool listTagsFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                              const qint32 afterUSN, const QString & linkedNotebookGuid,
                              QHash<QString, qevercloud::Tag> & tags,
                              ErrorString & errorDescription)
{
    tags.clear();

    const LocalStorageManager * pLocalStorageManager = localStorageManagerAsync.localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        errorDescription.setBase(QStringLiteral("Local storage manager is null"));
        return false;
    }

    QString localLinkedNotebookGuid = QStringLiteral("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    errorDescription.clear();
    QList<Tag> localTags = pLocalStorageManager->listTags(LocalStorageManager::ListAll,
                                                          errorDescription, 0, 0, LocalStorageManager::ListTagsOrder::NoOrder,
                                                          LocalStorageManager::OrderDirection::Ascending,
                                                          localLinkedNotebookGuid);
    if (localTags.isEmpty() && !errorDescription.isEmpty()) {
        return false;
    }

    tags.reserve(localTags.size());
    for(auto it = localTags.constBegin(), end = localTags.constEnd(); it != end; ++it)
    {
        const Tag & tag = *it;
        if (Q_UNLIKELY(!tag.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!tag.hasUpdateSequenceNumber() || (tag.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        tags[tag.guid()] = tag.qevercloudTag();
    }

    return true;
}

bool listNotebooksFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                                   const qint32 afterUSN, const QString & linkedNotebookGuid,
                                   QHash<QString, qevercloud::Notebook> & notebooks,
                                   ErrorString & errorDescription)
{
    notebooks.clear();

    const LocalStorageManager * pLocalStorageManager = localStorageManagerAsync.localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        errorDescription.setBase(QStringLiteral("Local storage manager is null"));
        return false;
    }

    QString localLinkedNotebookGuid = QStringLiteral("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    errorDescription.clear();
    QList<Notebook> localNotebooks = pLocalStorageManager->listNotebooks(LocalStorageManager::ListAll,
                                                                         errorDescription, 0, 0, LocalStorageManager::ListNotebooksOrder::NoOrder,
                                                                         LocalStorageManager::OrderDirection::Ascending,
                                                                         localLinkedNotebookGuid);
    if (localNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        return false;
    }

    notebooks.reserve(localNotebooks.size());
    for(auto it = localNotebooks.constBegin(), end = localNotebooks.constEnd(); it != end; ++it)
    {
        const Notebook & notebook = *it;
        if (Q_UNLIKELY(!notebook.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!notebook.hasUpdateSequenceNumber() || (notebook.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        notebooks[notebook.guid()] = notebook.qevercloudNotebook();
    }

    return true;
}

bool listNotesFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                               const qint32 afterUSN, const QString & linkedNotebookGuid,
                               QHash<QString, qevercloud::Note> & notes,
                               ErrorString & errorDescription)
{
    notes.clear();

    const LocalStorageManager * pLocalStorageManager = localStorageManagerAsync.localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        errorDescription.setBase(QStringLiteral("Local storage manager is null"));
        return false;
    }

    QString localLinkedNotebookGuid = QStringLiteral("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    errorDescription.clear();
    QList<Note> localNotes = pLocalStorageManager->listNotes(LocalStorageManager::ListAll,
                                                             errorDescription, true, 0, 0, LocalStorageManager::ListNotesOrder::NoOrder,
                                                             LocalStorageManager::OrderDirection::Ascending,
                                                             localLinkedNotebookGuid);
    if (localNotes.isEmpty() && !errorDescription.isEmpty()) {
        return false;
    }

    notes.reserve(localNotes.size());
    for(auto it = localNotes.constBegin(), end = localNotes.constEnd(); it != end; ++it)
    {
        const Note & note = *it;
        if (Q_UNLIKELY(!note.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!note.hasUpdateSequenceNumber() || (note.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        notes[note.guid()] = note.qevercloudNote();
    }

    return true;
}

bool listLinkedNotebooksFromLocalStorage(const LocalStorageManagerAsync & localStorageManagerAsync,
                                         const qint32 afterUSN, QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks,
                                         ErrorString & errorDescription)
{
    linkedNotebooks.clear();

    const LocalStorageManager * pLocalStorageManager = localStorageManagerAsync.localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        errorDescription.setBase(QStringLiteral("Local storage manager is null"));
        return false;
    }

    errorDescription.clear();
    QList<LinkedNotebook> localLinkedNotebooks = pLocalStorageManager->listLinkedNotebooks(LocalStorageManager::ListAll,
                                                                                           errorDescription, 0, 0, LocalStorageManager::ListLinkedNotebooksOrder::NoOrder,
                                                                                           LocalStorageManager::OrderDirection::Ascending);
    if (localLinkedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        return false;
    }

    linkedNotebooks.reserve(localLinkedNotebooks.size());
    for(auto it = localLinkedNotebooks.constBegin(), end = localLinkedNotebooks.constEnd(); it != end; ++it)
    {
        const LinkedNotebook & linkedNotebook = *it;
        if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!linkedNotebook.hasUpdateSequenceNumber() || (linkedNotebook.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        linkedNotebooks[linkedNotebook.guid()] = linkedNotebook.qevercloudLinkedNotebook();
    }

    return true;
}

} // namespace quentier
