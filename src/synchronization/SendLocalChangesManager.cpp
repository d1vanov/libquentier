/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#include "SendLocalChangesManager.h"
#include <quentier/utility/Macros.h>
#include <quentier/utility/Utility.h>
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <QTimerEvent>

#define APPEND_NOTE_DETAILS(errorDescription, note)                            \
   if (note.hasTitle())                                                        \
   {                                                                           \
       errorDescription.details() = note.title();                              \
   }                                                                           \
   else if (note.hasContent())                                                 \
   {                                                                           \
       QString previewText = note.plainText();                                 \
       if (!previewText.isEmpty()) {                                           \
           previewText.truncate(30);                                           \
           errorDescription.details() = previewText;                           \
       }                                                                       \
   }                                                                           \
// APPEND_NOTE_DETAILS

namespace quentier {

SendLocalChangesManager::SendLocalChangesManager(
        IManager & manager, QObject * parent) :
    QObject(parent),
    m_manager(manager),
    m_lastUpdateCount(0),
    m_lastUpdateCountByLinkedNotebookGuid(),
    m_shouldRepeatIncrementalSync(false),
    m_active(false),
    m_connectedToLocalStorage(false),
    m_receivedDirtyLocalStorageObjectsFromUsersAccount(false),
    m_receivedAllDirtyLocalStorageObjects(false),
    m_listDirtyTagsRequestId(),
    m_listDirtySavedSearchesRequestId(),
    m_listDirtyNotebooksRequestId(),
    m_listDirtyNotesRequestId(),
    m_listLinkedNotebooksRequestId(),
    m_listDirtyTagsFromLinkedNotebooksRequestIds(),
    m_listDirtyNotebooksFromLinkedNotebooksRequestIds(),
    m_listDirtyNotesFromLinkedNotebooksRequestIds(),
    m_tags(),
    m_savedSearches(),
    m_notebooks(),
    m_notes(),
    m_linkedNotebookGuidsForWhichStuffWasRequestedFromLocalStorage(),
    m_linkedNotebookAuthData(),
    m_authenticationTokensAndShardIdsByLinkedNotebookGuid(),
    m_authenticationTokenExpirationTimesByLinkedNotebookGuid(),
    m_pendingAuthenticationTokensForLinkedNotebooks(false),
    m_updateTagRequestIds(),
    m_updateSavedSearchRequestIds(),
    m_updateNotebookRequestIds(),
    m_updateNoteRequestIds(),
    m_findNotebookRequestIds(),
    m_notebooksByGuidsCache(),
    m_sendTagsPostponeTimerId(0),
    m_sendSavedSearchesPostponeTimerId(0),
    m_sendNotebooksPostponeTimerId(0),
    m_sendNotesPostponeTimerId(0)
{}

bool SendLocalChangesManager::active() const
{
    return m_active;
}

void SendLocalChangesManager::start(
    const qint32 updateCount,
    QHash<QString,qint32> updateCountByLinkedNotebookGuid)
{
    QNDEBUG("SendLocalChangesManager::start: update count = "
            << updateCount << ", update count by linked notebook guid = "
            << updateCountByLinkedNotebookGuid);

    clear();
    m_active = true;
    m_lastUpdateCount = updateCount;
    m_lastUpdateCountByLinkedNotebookGuid = updateCountByLinkedNotebookGuid;

    requestStuffFromLocalStorage();
}

void SendLocalChangesManager::stop()
{
    QNDEBUG("SendLocalChangesManager::stop");

    if (!m_active) {
        QNDEBUG("Already stopped");
        return;
    }

    clear();

    m_active = false;
    Q_EMIT stopped();
}

void SendLocalChangesManager::onAuthenticationTokensForLinkedNotebooksReceived(
    QHash<QString,QPair<QString,QString> > authTokensByLinkedNotebookGuid,
    QHash<QString,qevercloud::Timestamp> authTokenExpirationByLinkedNotebookGuid)
{
    QNDEBUG("SendLocalChangesManager::"
            "onAuthenticationTokensForLinkedNotebooksReceived");

    if (!m_pendingAuthenticationTokensForLinkedNotebooks) {
        QNDEBUG("Authentication tokens for linked notebooks were "
                "not requested by this object, won't do anything");
        return;
    }

    m_pendingAuthenticationTokensForLinkedNotebooks = false;

    m_authenticationTokensAndShardIdsByLinkedNotebookGuid =
        authTokensByLinkedNotebookGuid;

    m_authenticationTokenExpirationTimesByLinkedNotebookGuid =
        authTokenExpirationByLinkedNotebookGuid;

    sendLocalChanges();
}

void SendLocalChangesManager::onListDirtyTagsCompleted(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListTagsOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid, QList<Tag> tags, QUuid requestId)
{
    bool userTagsListCompleted = (requestId == m_listDirtyTagsRequestId);
    auto it = m_listDirtyTagsFromLinkedNotebooksRequestIds.end();
    if (!userTagsListCompleted) {
        it = m_listDirtyTagsFromLinkedNotebooksRequestIds.find(requestId);
    }

    if (userTagsListCompleted ||
        (it != m_listDirtyTagsFromLinkedNotebooksRequestIds.end()))
    {
        QNDEBUG("SendLocalChangesManager::onListDirtyTagsCompleted: "
                << "flag = " << flag << ", limit = " << limit << ", offset = "
                << offset << ", order = " << order << ", orderDirection = "
                << orderDirection << ", linked notebook guid = "
                << linkedNotebookGuid << ", requestId = " << requestId
                << ", " << tags.size() << " tags listed");

        m_tags << tags;

        if (userTagsListCompleted) {
            QNTRACE("User's tags list is completed: " << m_tags.size()
                    << " tags");
            m_listDirtyTagsRequestId = QUuid();
        }
        else {
            QNTRACE("Tags list is completed for one of linked notebooks");
            Q_UNUSED(m_listDirtyTagsFromLinkedNotebooksRequestIds.erase(it));
        }

        checkListLocalStorageObjectsCompletion();
    }
}

void SendLocalChangesManager::onListDirtyTagsFailed(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListTagsOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    bool userTagsListCompleted = (requestId == m_listDirtyTagsRequestId);
    auto it = m_listDirtyTagsFromLinkedNotebooksRequestIds.end();
    if (!userTagsListCompleted) {
        it = m_listDirtyTagsFromLinkedNotebooksRequestIds.find(requestId);
    }

    if (userTagsListCompleted ||
        (it != m_listDirtyTagsFromLinkedNotebooksRequestIds.end()))
    {
        QNWARNING("SendLocalChangesManager::onListDirtyTagsFailed: "
                  << "flag = " << flag << ", limit = " << limit << ", offset = "
                  << offset << ", order = " << order << ", orderDirection = "
                  << orderDirection << ", linked notebook guid = "
                  << linkedNotebookGuid << ", error description = "
                  << errorDescription << ", requestId = " << requestId);

        if (userTagsListCompleted) {
            m_listDirtyTagsRequestId = QUuid();
        }
        else {
            Q_UNUSED(m_listDirtyTagsFromLinkedNotebooksRequestIds.erase(it));
        }

        ErrorString error(QT_TR_NOOP("Error listing dirty tags from "
                                     "the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
    }
}

void SendLocalChangesManager::onListDirtySavedSearchesCompleted(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QList<SavedSearch> savedSearches, QUuid requestId)
{
    if (requestId != m_listDirtySavedSearchesRequestId) {
        return;
    }

    QNDEBUG("SendLocalChangesManager::onListDirtySavedSearchesCompleted: "
            << "flag = " << flag << ", limit = " << limit << ", offset = "
            << offset << ", order = " << order << ", orderDirection = "
            << orderDirection << ", requestId = " << requestId << ", "
            << savedSearches.size() << " saved searches listed");

    m_savedSearches << savedSearches;
    QNTRACE("Total " << m_savedSearches.size() << " dirty saved searches");

    m_listDirtySavedSearchesRequestId = QUuid();

    checkListLocalStorageObjectsCompletion();
}

void SendLocalChangesManager::onListDirtySavedSearchesFailed(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListSavedSearchesOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    ErrorString errorDescription, QUuid requestId)
{
    QNTRACE("SendLocalChangesManager::onListDirtySavedSearchesFailed: "
            << "request id = " << requestId << ", error: " << errorDescription);

    if (requestId != m_listDirtySavedSearchesRequestId) {
        return;
    }

    QNWARNING("SendLocalChangesManager::onListDirtySavedSearchesFailed: "
              << "flag = " << flag << ", limit = " << limit << ", offset = "
              << offset << ", order = " << order << ", orderDirection = "
              << orderDirection << ", errorDescription = " << errorDescription
              << ", requestId = " << requestId);

    m_listDirtySavedSearchesRequestId = QUuid();

    ErrorString error(QT_TR_NOOP("Error listing dirty saved searches from "
                                 "the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void SendLocalChangesManager::onListDirtyNotebooksCompleted(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListNotebooksOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid, QList<Notebook> notebooks, QUuid requestId)
{
    bool userNotebooksListCompleted = (requestId == m_listDirtyNotebooksRequestId);
    auto it = m_listDirtyNotebooksFromLinkedNotebooksRequestIds.end();
    if (!userNotebooksListCompleted) {
        it = m_listDirtyNotebooksFromLinkedNotebooksRequestIds.find(requestId);
    }

    if (userNotebooksListCompleted ||
        (it != m_listDirtyNotebooksFromLinkedNotebooksRequestIds.end()))
    {
        QNDEBUG("SendLocalChangesManager::onListDirtyNotebooksCompleted: "
                << "flag = " << flag << ", limit = " << limit << ", offset = "
                << offset << ", order = " << order << ", orderDirection = "
                << orderDirection << ", linkedNotebookGuid = "
                << linkedNotebookGuid << ", requestId = " << requestId << ", "
                << notebooks.size() << " notebooks listed");

        m_notebooks << notebooks;

        if (userNotebooksListCompleted) {
            QNTRACE("User's notebooks list is completed: "
                    << m_notebooks.size() << " notebooks");
            m_listDirtyNotebooksRequestId = QUuid();
        }
        else {
            Q_UNUSED(m_listDirtyNotebooksFromLinkedNotebooksRequestIds.erase(it));
        }

        checkListLocalStorageObjectsCompletion();
    }
}

void SendLocalChangesManager::onListDirtyNotebooksFailed(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListNotebooksOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    bool userNotebooksListCompleted = (requestId == m_listDirtyNotebooksRequestId);
    auto it = m_listDirtyNotebooksFromLinkedNotebooksRequestIds.end();
    if (!userNotebooksListCompleted) {
        it = m_listDirtyNotebooksFromLinkedNotebooksRequestIds.find(requestId);
    }

    if (userNotebooksListCompleted ||
        (it != m_listDirtyNotebooksFromLinkedNotebooksRequestIds.end()))
    {
        QNWARNING("SendLocalChangesManager::onListDirtyNotebooksFailed: "
                  << "flag = " << flag << ", limit = " << limit << ", offset = "
                  << offset << ", order = " << order << ", orderDirection = "
                  << orderDirection << ", linkedNotebookGuid = "
                  << linkedNotebookGuid << ", errorDescription = "
                  << errorDescription);

        if (userNotebooksListCompleted) {
            m_listDirtyNotebooksRequestId = QUuid();
        }
        else {
            Q_UNUSED(m_listDirtyNotebooksFromLinkedNotebooksRequestIds.erase(it));
        }

        ErrorString error(QT_TR_NOOP("Error listing dirty notebooks from "
                                     "the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
    }
}

void SendLocalChangesManager::onListDirtyNotesCompleted(
    LocalStorageManager::ListObjectsOptions flag,
    LocalStorageManager::GetNoteOptions options,
    size_t limit, size_t offset, LocalStorageManager::ListNotesOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid, QList<Note> notes, QUuid requestId)
{
    bool userNotesListCompleted = (requestId == m_listDirtyNotesRequestId);
    auto it = m_listDirtyNotesFromLinkedNotebooksRequestIds.end();
    if (!userNotesListCompleted) {
        it = m_listDirtyNotesFromLinkedNotebooksRequestIds.find(requestId);
    }

    if (userNotesListCompleted ||
        (it != m_listDirtyNotesFromLinkedNotebooksRequestIds.end()))
    {
        QNDEBUG("SendLocalChangesManager::onListDirtyNotesCompleted: "
                << "flag = " << flag << ", with resource metadata = "
                << ((options & LocalStorageManager::GetNoteOption::WithResourceMetadata)
                    ? "true"
                    : "false")
                << ", with resource binary data = "
                << ((options & LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                    ? "true"
                    : "false")
                << ", limit = " << limit << ", offset = " << offset
                << ", order = " << order << ", orderDirection = "
                << orderDirection << ", linked notebook guid = "
                << linkedNotebookGuid << ", requestId = " << requestId << ", "
                << notes.size() << " notes listed");

        m_notes << notes;

        if (userNotesListCompleted) {
            m_listDirtyNotesRequestId = QUuid();
        }
        else {
            Q_UNUSED(m_listDirtyNotesFromLinkedNotebooksRequestIds.erase(it));
        }

        checkListLocalStorageObjectsCompletion();
    }
}

void SendLocalChangesManager::onListDirtyNotesFailed(
    LocalStorageManager::ListObjectsOptions flag,
    LocalStorageManager::GetNoteOptions options,
    size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QString linkedNotebookGuid, ErrorString errorDescription, QUuid requestId)
{
    bool userNotesListCompleted = (requestId == m_listDirtyNotesRequestId);
    auto it = m_listDirtyNotesFromLinkedNotebooksRequestIds.end();
    if (!userNotesListCompleted) {
        it = m_listDirtyNotesFromLinkedNotebooksRequestIds.find(requestId);
    }

    if (userNotesListCompleted ||
        (it != m_listDirtyNotesFromLinkedNotebooksRequestIds.end()))
    {
        QNWARNING("SendLocalChangesManager::onListDirtyNotesFailed: "
                  << "flag = " << flag << ", with resource metadata = "
                  << ((options & LocalStorageManager::GetNoteOption::WithResourceMetadata)
                      ? "true"
                      : "false")
                  << ", with resource binary data = "
                  << ((options & LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                      ? "true"
                      : "false")
                  << ", limit = " << limit << ", offset = " << offset
                  << ", order = " << order << ", orderDirection = "
                  << orderDirection << ", linked notebook guid = "
                  << linkedNotebookGuid << ", requestId = " << requestId);

        if (userNotesListCompleted) {
            m_listDirtyNotesRequestId = QUuid();
        }
        else {
            Q_UNUSED(m_listDirtyNotesFromLinkedNotebooksRequestIds.erase(it));
        }

        ErrorString error(QT_TR_NOOP("Error listing dirty notes from "
                                     "the local storage"));
        error.additionalBases().append(errorDescription.base());
        error.additionalBases().append(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        Q_EMIT failure(error);
    }
}

void SendLocalChangesManager::onListLinkedNotebooksCompleted(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListLinkedNotebooksOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    QList<LinkedNotebook> linkedNotebooks, QUuid requestId)
{
    if (requestId != m_listLinkedNotebooksRequestId) {
        return;
    }

    QNDEBUG("SendLocalChangesManager::onListLinkedNotebooksCompleted: "
            << "flag = " << flag << ", limit = " << limit << ", offset = "
            << offset << ", order = " << order << ", orderDirection = "
            << orderDirection << ", requestId = " << requestId << ", "
            << " linked notebooks listed");

    const int numLinkedNotebooks = linkedNotebooks.size();
    m_linkedNotebookAuthData.reserve(std::max(numLinkedNotebooks, 0));

    QString shardId;
    QString sharedNotebookGlobalId;
    QString uri;
    QString noteStoreUrl;
    for(int i = 0; i < numLinkedNotebooks; ++i)
    {
        const LinkedNotebook & linkedNotebook = linkedNotebooks[i];
        if (!linkedNotebook.hasGuid())
        {
            ErrorString error(QT_TR_NOOP("Internal error: found a linked "
                                         "notebook without guid"));
            if (linkedNotebook.hasUsername()) {
                error.details() = linkedNotebook.username();
            }

            Q_EMIT failure(error);
            QNWARNING(error << ", linked notebook: " << linkedNotebook);
            return;
        }

        shardId.resize(0);
        if (linkedNotebook.hasShardId()) {
            shardId = linkedNotebook.shardId();
        }

        sharedNotebookGlobalId.resize(0);
        if (linkedNotebook.hasSharedNotebookGlobalId()) {
            sharedNotebookGlobalId = linkedNotebook.sharedNotebookGlobalId();
        }

        uri.resize(0);
        if (linkedNotebook.hasUri()) {
            uri = linkedNotebook.uri();
        }

        noteStoreUrl.resize(0);
        if (linkedNotebook.hasNoteStoreUrl()) {
            noteStoreUrl = linkedNotebook.noteStoreUrl();
        }

        m_linkedNotebookAuthData
            << LinkedNotebookAuthData(linkedNotebook.guid(), shardId,
                                      sharedNotebookGlobalId, uri, noteStoreUrl);
    }

    m_listLinkedNotebooksRequestId = QUuid();
    checkListLocalStorageObjectsCompletion();
}

void SendLocalChangesManager::onListLinkedNotebooksFailed(
    LocalStorageManager::ListObjectsOptions flag,
    size_t limit, size_t offset,
    LocalStorageManager::ListLinkedNotebooksOrder::type order,
    LocalStorageManager::OrderDirection::type orderDirection,
    ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_listLinkedNotebooksRequestId) {
        return;
    }

    QNWARNING("SendLocalChangesManager::onListLinkedNotebooksFailed: "
              <<  "flag = " << flag << ", limit = " << limit << ", offset = "
              << offset << ", order = " << order << ", orderDirection = "
              << orderDirection << ", errorDescription = " << errorDescription
              << ", requestId = " << requestId);

    m_listLinkedNotebooksRequestId = QUuid();

    ErrorString error(QT_TR_NOOP("Error listing linked notebooks from "
                                 "the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void SendLocalChangesManager::onUpdateTagCompleted(Tag tag, QUuid requestId)
{
    auto it = m_updateTagRequestIds.find(requestId);
    if (it == m_updateTagRequestIds.end()) {
        return;
    }

    QNDEBUG("SendLocalChangesManager::onUpdateTagCompleted: tag = "
            << tag << "\nRequest id = " << requestId);
    Q_UNUSED(m_updateTagRequestIds.erase(it));

    if (m_tags.isEmpty() && m_updateTagRequestIds.isEmpty()) {
        checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::onUpdateTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateTagRequestIds.find(requestId);
    if (it == m_updateTagRequestIds.end()) {
        return;
    }

    QNWARNING("SendLocalChangesManager::onUpdateTagFailed: tag = "
              << tag << "\nRequest id = " << requestId
              << ", error description = " << errorDescription);

    Q_UNUSED(m_updateTagRequestIds.erase(it));

    ErrorString error(QT_TR_NOOP("Failed to update a tag in the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void SendLocalChangesManager::onUpdateSavedSearchCompleted(
    SavedSearch savedSearch, QUuid requestId)
{
    auto it = m_updateSavedSearchRequestIds.find(requestId);
    if (it == m_updateSavedSearchRequestIds.end()) {
        return;
    }

    QNDEBUG("SendLocalChangesManager::onUpdateSavedSearchCompleted: "
            << "search = " << savedSearch << "\nRequest id = " << requestId);
    Q_UNUSED(m_updateSavedSearchRequestIds.erase(it));

    if (m_savedSearches.isEmpty() && m_updateSavedSearchRequestIds.isEmpty()) {
        checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::onUpdateSavedSearchFailed(
    SavedSearch savedSearch, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateSavedSearchRequestIds.find(requestId);
    if (it == m_updateSavedSearchRequestIds.end()) {
        return;
    }

    QNWARNING("SendLocalChangesManager::onUpdateSavedSearchFailed: "
              << "saved search = " << savedSearch
              << "\nRequest id = " << requestId
              << ", error description = " << errorDescription);

    Q_UNUSED(m_updateSavedSearchRequestIds.erase(it));

    ErrorString error(QT_TR_NOOP("Failed to update a saved search "
                                 "in the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void SendLocalChangesManager::onUpdateNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    auto it = m_updateNotebookRequestIds.find(requestId);
    if (it == m_updateNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG("SendLocalChangesManager::onUpdateNotebookCompleted: "
            << "notebook = " << notebook
            << "\nRequest id = " << requestId);
    Q_UNUSED(m_updateNotebookRequestIds.erase(it));

    if (m_notebooks.isEmpty() && m_updateNotebookRequestIds.isEmpty()) {
        checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::onUpdateNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_updateNotebookRequestIds.find(requestId);
    if (it == m_updateNotebookRequestIds.end()) {
        return;
    }

    QNWARNING("SendLocalChangesManager::onUpdateNotebookFailed: "
              << "notebook = " << notebook
              << "\nRequest id = " << requestId
              << ", error description = " << errorDescription);

    Q_UNUSED(m_updateNotebookRequestIds.erase(it));

    ErrorString error(QT_TR_NOOP("Failed to update a notebook in the local "
                                 "storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void SendLocalChangesManager::onUpdateNoteCompleted(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    Q_UNUSED(options)

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    QNDEBUG("SendLocalChangesManager::onUpdateNoteCompleted: note = "
            << note << "\nRequest id = " << requestId);
    Q_UNUSED(m_updateNoteRequestIds.erase(it));

    if (m_notes.isEmpty() && m_updateNoteRequestIds.isEmpty()) {
        checkDirtyFlagRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::onUpdateNoteFailed(
    Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(options)

    auto it = m_updateNoteRequestIds.find(requestId);
    if (it == m_updateNoteRequestIds.end()) {
        return;
    }

    QNWARNING("SendLocalChangesManager::onUpdateNoteFailed: note = "
              << note << "\nRequest id = " << requestId
              << ", error description = " << errorDescription);

    Q_UNUSED(m_updateNoteRequestIds.erase(it));

    ErrorString error(QT_TR_NOOP("Failed to update a note in the local storage"));
    error.additionalBases().append(errorDescription.base());
    error.additionalBases().append(errorDescription.additionalBases());
    error.details() = errorDescription.details();
    Q_EMIT failure(error);
}

void SendLocalChangesManager::onFindNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    auto it = m_findNotebookRequestIds.find(requestId);
    if (it == m_findNotebookRequestIds.end()) {
        return;
    }

    QNDEBUG("SendLocalChangesManager::onFindNotebookCompleted: "
            << "notebook = " << notebook << "\nRequest id = " << requestId);

    if (!notebook.hasGuid())
    {
        ErrorString errorDescription(QT_TR_NOOP("Found a notebook without guid "
                                                "within the notebooks requested "
                                                "from the local storage by guid"));
        if (notebook.hasName()) {
            errorDescription.details() = notebook.name();
        }

        QNWARNING(errorDescription << ", notebook: " << notebook);
        Q_EMIT failure(errorDescription);
        return;
    }

    m_notebooksByGuidsCache[notebook.guid()] = notebook;
    Q_UNUSED(m_findNotebookRequestIds.erase(it));

    if (m_findNotebookRequestIds.isEmpty()) {
        checkAndSendNotes();
    }
}

void SendLocalChangesManager::onFindNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    auto it = m_findNotebookRequestIds.find(requestId);
    if (it == m_findNotebookRequestIds.end()) {
        return;
    }

    Q_UNUSED(m_findNotebookRequestIds.erase(it));

    QNWARNING(errorDescription << "; notebook: " << notebook);
    Q_EMIT failure(errorDescription);
}

void SendLocalChangesManager::timerEvent(QTimerEvent * pEvent)
{
    QNDEBUG("SendLocalChangesManager::timerEvent");

    if (Q_UNLIKELY(!pEvent)) {
        ErrorString errorDescription(QT_TR_NOOP("Qt error: detected null pointer "
                                                "to QTimerEvent"));
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription);
        return;
    }

    int timerId = pEvent->timerId();
    killTimer(timerId);
    QNDEBUG("Killed timer with id " << timerId);

    if (timerId == m_sendTagsPostponeTimerId)
    {
        m_sendTagsPostponeTimerId = 0;
        sendTags();
    }
    else if (timerId == m_sendSavedSearchesPostponeTimerId)
    {
        m_sendSavedSearchesPostponeTimerId = 0;
        sendSavedSearches();
    }
    else if (timerId == m_sendNotebooksPostponeTimerId)
    {
        m_sendNotebooksPostponeTimerId = 0;
        sendNotebooks();
    }
    else if (timerId == m_sendNotesPostponeTimerId)
    {
        m_sendNotesPostponeTimerId = 0;
        checkAndSendNotes();
    }
}

void SendLocalChangesManager::connectToLocalStorage()
{
    QNDEBUG("SendLocalChangesManager::connectToLocalStorage");

    if (m_connectedToLocalStorage) {
        QNDEBUG("Already connected to local storage");
        return;
    }

    LocalStorageManagerAsync & localStorageManagerAsync =
        m_manager.localStorageManagerAsync();

    // Connect local signals with localStorageManagerAsync's slots
    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,requestLocalUnsynchronizedTags,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListTagsRequest,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,
                              requestLocalUnsynchronizedSavedSearches,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListSavedSearchesOrder::type,
                              LocalStorageManager::OrderDirection::type,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListSavedSearchesRequest,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListSavedSearchesOrder::type,
                            LocalStorageManager::OrderDirection::type,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,
                              requestLocalUnsynchronizedNotebooks,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListNotebooksRequest,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,requestLocalUnsynchronizedNotes,
                              LocalStorageManager::ListObjectsOptions,
                              LocalStorageManager::GetNoteOptions,size_t,size_t,
                              LocalStorageManager::ListNotesOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListNotesRequest,
                            LocalStorageManager::ListObjectsOptions,
                            LocalStorageManager::GetNoteOptions,size_t,size_t,
                            LocalStorageManager::ListNotesOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,requestLinkedNotebooksList,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onListLinkedNotebooksRequest,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,updateTag,Tag,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,updateSavedSearch,
                              SavedSearch,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,
                            SavedSearch,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,updateNotebook,Notebook,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,
                            Notebook,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,updateNote,
                              Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,
                            Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(this,
                     QNSIGNAL(SendLocalChangesManager,findNotebook,Notebook,QUuid),
                     &localStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,
                            Notebook,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    // Connect localStorageManagerAsync's signals to local slots
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listTagsComplete,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QList<Tag>,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtyTagsCompleted,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QList<Tag>,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listTagsFailed,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListTagsOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtyTagsFailed,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListTagsOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listSavedSearchesComplete,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListSavedSearchesOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QList<SavedSearch>,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtySavedSearchesCompleted,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListSavedSearchesOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QList<SavedSearch>,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listSavedSearchesFailed,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListSavedSearchesOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtySavedSearchesFailed,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListSavedSearchesOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listNotebooksComplete,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QList<Notebook>,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtyNotebooksCompleted,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QList<Notebook>,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listNotebooksFailed,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtyNotebooksFailed,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listNotesComplete,
                              LocalStorageManager::ListObjectsOptions,
                              LocalStorageManager::GetNoteOptions,size_t,size_t,
                              LocalStorageManager::ListNotesOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,QList<Note>,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtyNotesCompleted,
                            LocalStorageManager::ListObjectsOptions,
                            LocalStorageManager::GetNoteOptions,size_t,size_t,
                            LocalStorageManager::ListNotesOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,QList<Note>,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listNotesFailed,
                              LocalStorageManager::ListObjectsOptions,
                              LocalStorageManager::GetNoteOptions,size_t,size_t,
                              LocalStorageManager::ListNotesOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QString,ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListDirtyNotesFailed,
                            LocalStorageManager::ListObjectsOptions,
                            LocalStorageManager::GetNoteOptions,size_t,size_t,
                            LocalStorageManager::ListNotesOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QString,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listLinkedNotebooksComplete,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              QList<LinkedNotebook>,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListLinkedNotebooksCompleted,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            QList<LinkedNotebook>,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,listLinkedNotebooksFailed,
                              LocalStorageManager::ListObjectsOptions,size_t,size_t,
                              LocalStorageManager::ListLinkedNotebooksOrder::type,
                              LocalStorageManager::OrderDirection::type,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onListLinkedNotebooksFailed,
                            LocalStorageManager::ListObjectsOptions,size_t,size_t,
                            LocalStorageManager::ListLinkedNotebooksOrder::type,
                            LocalStorageManager::OrderDirection::type,
                            ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,Tag,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateTagCompleted,Tag,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,
                              Tag,ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateTagFailed,
                            Tag,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,
                              SavedSearch,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateSavedSearchCompleted,
                            SavedSearch,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,
                              SavedSearch,ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateSavedSearchFailed,
                            SavedSearch,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateNotebookCompleted,
                            Notebook,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateNotebookFailed,
                            Notebook,ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,
                              Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateNoteCompleted,
                            Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,
                              Note,LocalStorageManager::UpdateNoteOptions,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onUpdateNoteFailed,
                            Note,LocalStorageManager::UpdateNoteOptions,
                            ErrorString,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(&localStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(SendLocalChangesManager,onFindNotebookCompleted,
                            Notebook,QUuid),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    m_connectedToLocalStorage = true;
}

void SendLocalChangesManager::disconnectFromLocalStorage()
{
    QNDEBUG("SendLocalChangesManager::disconnectFromLocalStorage");

    if (!m_connectedToLocalStorage) {
        QNDEBUG("Not connected to local storage at the moment");
        return;
    }

    LocalStorageManagerAsync & localStorageManagerAsync =
        m_manager.localStorageManagerAsync();

    // Disconnect local signals from localStorageManagerAsync's slots
    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,
                                 requestLocalUnsynchronizedTags,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListTagsOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListTagsRequest,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,
                                 requestLocalUnsynchronizedSavedSearches,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListSavedSearchesOrder::type,
                                 LocalStorageManager::OrderDirection::type,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListSavedSearchesRequest,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListSavedSearchesOrder::type,
                               LocalStorageManager::OrderDirection::type,QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,
                                 requestLocalUnsynchronizedNotebooks,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListNotebooksRequest,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,
                                 requestLocalUnsynchronizedNotes,
                                 LocalStorageManager::ListObjectsOptions,
                                 LocalStorageManager::GetNoteOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListNotesOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onListNotesRequest,
                               LocalStorageManager::ListObjectsOptions,
                               LocalStorageManager::GetNoteOptions,size_t,size_t,
                               LocalStorageManager::ListNotesOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,
                                 requestLinkedNotebooksList,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,
                               onListLinkedNotebooksRequest,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,updateTag,Tag,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateTagRequest,
                               Tag,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,updateSavedSearch,
                                 SavedSearch,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateSavedSearchRequest,
                               SavedSearch,QUuid));
    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,updateNotebook,
                                 Notebook,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateNotebookRequest,
                               Notebook,QUuid));
    QObject::disconnect(this, QNSIGNAL(SendLocalChangesManager,updateNote,Note,
                                       LocalStorageManager::UpdateNoteOptions,
                                       QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,
                               Note,LocalStorageManager::UpdateNoteOptions,
                               QUuid));

    QObject::disconnect(this,
                        QNSIGNAL(SendLocalChangesManager,findNotebook,
                                 Notebook,QUuid),
                        &localStorageManagerAsync,
                        QNSLOT(LocalStorageManagerAsync,onFindNotebookRequest,
                               Notebook,QUuid));

    // Disconnect localStorageManagerAsync's signals from local slots
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listTagsComplete,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListTagsOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QList<Tag>,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onListDirtyTagsCompleted,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QList<Tag>,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listTagsFailed,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListTagsOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onListDirtyTagsFailed,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListTagsOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,
                                 listSavedSearchesComplete,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListSavedSearchesOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QList<SavedSearch>,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,
                               onListDirtySavedSearchesCompleted,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListSavedSearchesOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QList<SavedSearch>,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,
                                 listSavedSearchesFailed,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListSavedSearchesOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onListDirtySavedSearchesFailed,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListSavedSearchesOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listNotebooksComplete,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QList<Notebook>,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onListDirtyNotebooksCompleted,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QList<Notebook>,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listNotebooksFailed,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onListDirtyNotebooksFailed,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listNotesComplete,
                                 LocalStorageManager::ListObjectsOptions,
                                 LocalStorageManager::GetNoteOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListNotesOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,QList<Note>,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onListDirtyNotesCompleted,
                               LocalStorageManager::ListObjectsOptions,
                               LocalStorageManager::GetNoteOptions,size_t,size_t,
                               LocalStorageManager::ListNotesOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,QList<Note>,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,listNotesFailed,
                                 LocalStorageManager::ListObjectsOptions,
                                 LocalStorageManager::GetNoteOptions,size_t,size_t,
                                 LocalStorageManager::ListNotesOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QString,ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onListDirtyNotesFailed,
                               LocalStorageManager::ListObjectsOptions,
                               LocalStorageManager::GetNoteOptions,size_t,size_t,
                               LocalStorageManager::ListNotesOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QString,ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,
                                 listLinkedNotebooksComplete,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 QList<LinkedNotebook>,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,
                               onListLinkedNotebooksCompleted,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               QList<LinkedNotebook>,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,
                                 listLinkedNotebooksFailed,
                                 LocalStorageManager::ListObjectsOptions,
                                 size_t,size_t,
                                 LocalStorageManager::ListLinkedNotebooksOrder::type,
                                 LocalStorageManager::OrderDirection::type,
                                 ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,
                               onListLinkedNotebooksFailed,
                               LocalStorageManager::ListObjectsOptions,
                               size_t,size_t,
                               LocalStorageManager::ListLinkedNotebooksOrder::type,
                               LocalStorageManager::OrderDirection::type,
                               ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateTagComplete,
                                 Tag,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateTagCompleted,
                               Tag,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateTagFailed,
                                 Tag,ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateTagFailed,
                               Tag,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchComplete,
                                 SavedSearch,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateSavedSearchCompleted,
                               SavedSearch,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateSavedSearchFailed,
                                 SavedSearch,ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateSavedSearchFailed,
                               SavedSearch,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNotebookComplete,
                                 Notebook,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateNotebookCompleted,
                               Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNotebookFailed,
                                 Notebook,ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateNotebookFailed,
                               Notebook,ErrorString,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,
                                 Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateNoteCompleted,
                               Note,LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,
                                 Note,LocalStorageManager::UpdateNoteOptions,
                                 ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onUpdateNoteFailed,
                               Note,LocalStorageManager::UpdateNoteOptions,
                               ErrorString,QUuid));

    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,findNotebookComplete,
                                 Notebook,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onFindNotebookCompleted,
                               Notebook,QUuid));
    QObject::disconnect(&localStorageManagerAsync,
                        QNSIGNAL(LocalStorageManagerAsync,findNotebookFailed,
                                 Notebook,ErrorString,QUuid),
                        this,
                        QNSLOT(SendLocalChangesManager,onFindNotebookFailed,
                               Notebook,ErrorString,QUuid));

    m_connectedToLocalStorage = false;
}

bool SendLocalChangesManager::requestStuffFromLocalStorage(
    const QString & linkedNotebookGuid)
{
    QNDEBUG("SendLocalChangesManager::requestStuffFromLocalStorage: "
            << "linked notebook guid = " << linkedNotebookGuid << " (empty = "
            << (linkedNotebookGuid.isEmpty()
                ? "true"
                : "false")
            << ", null = "
            << (linkedNotebookGuid.isNull()
                ? "true"
                : "false")
            << ")");

    bool emptyLinkedNotebookGuid = linkedNotebookGuid.isEmpty();
    if (!emptyLinkedNotebookGuid)
    {
        auto it =
            m_linkedNotebookGuidsForWhichStuffWasRequestedFromLocalStorage.find(
                linkedNotebookGuid);
        if (it != m_linkedNotebookGuidsForWhichStuffWasRequestedFromLocalStorage.end()) {
            QNDEBUG("The stuff has already been requested from "
                    << "the local storage for linked notebook guid "
                    << linkedNotebookGuid);
            return false;
        }
    }

    connectToLocalStorage();

    LocalStorageManager::ListObjectsOptions listDirtyObjectsFlag =
            LocalStorageManager::ListDirty | LocalStorageManager::ListNonLocal;

    size_t limit = 0, offset = 0;
    LocalStorageManager::OrderDirection::type orderDirection =
        LocalStorageManager::OrderDirection::Ascending;

    LocalStorageManager::ListTagsOrder::type tagsOrder =
        LocalStorageManager::ListTagsOrder::NoOrder;

    QUuid listDirtyTagsRequestId = QUuid::createUuid();
    if (emptyLinkedNotebookGuid) {
        m_listDirtyTagsRequestId = listDirtyTagsRequestId;
    }
    else {
        Q_UNUSED(m_listDirtyTagsFromLinkedNotebooksRequestIds.insert(
            listDirtyTagsRequestId));
    }
    QNTRACE("Emitting the request to fetch unsynchronized tags "
            << "from the local storage: request id = "
            << listDirtyTagsRequestId);
    Q_EMIT requestLocalUnsynchronizedTags(listDirtyObjectsFlag, limit, offset,
                                          tagsOrder, orderDirection,
                                          linkedNotebookGuid, listDirtyTagsRequestId);

    if (emptyLinkedNotebookGuid)
    {
        LocalStorageManager::ListSavedSearchesOrder::type savedSearchesOrder =
            LocalStorageManager::ListSavedSearchesOrder::NoOrder;

        m_listDirtySavedSearchesRequestId = QUuid::createUuid();
        QNTRACE("Emitting the request to fetch unsynchronized "
                << "saved searches from the local storage: request id = "
                << m_listDirtySavedSearchesRequestId);
        Q_EMIT requestLocalUnsynchronizedSavedSearches(
            listDirtyObjectsFlag,
            limit, offset,
            savedSearchesOrder,
            orderDirection,
            m_listDirtySavedSearchesRequestId);
    }

    LocalStorageManager::ListNotebooksOrder::type notebooksOrder =
        LocalStorageManager::ListNotebooksOrder::NoOrder;

    QUuid listDirtyNotebooksRequestId = QUuid::createUuid();
    if (emptyLinkedNotebookGuid) {
        m_listDirtyNotebooksRequestId = listDirtyNotebooksRequestId;
    }
    else {
        Q_UNUSED(m_listDirtyNotebooksFromLinkedNotebooksRequestIds.insert(
            listDirtyNotebooksRequestId));
    }

    QNTRACE("Emitting the request to fetch unsynchronized notebooks "
            << "from the local storage: request id = "
            << listDirtyNotebooksRequestId);
    Q_EMIT requestLocalUnsynchronizedNotebooks(
        listDirtyObjectsFlag, limit,
        offset, notebooksOrder,
        orderDirection, linkedNotebookGuid,
        listDirtyNotebooksRequestId);

    LocalStorageManager::ListNotesOrder::type notesOrder =
        LocalStorageManager::ListNotesOrder::NoOrder;

    QUuid listDirtyNotesRequestId = QUuid::createUuid();
    if (emptyLinkedNotebookGuid) {
        m_listDirtyNotesRequestId = listDirtyNotesRequestId;
    }
    else {
        Q_UNUSED(m_listDirtyNotesFromLinkedNotebooksRequestIds.insert(
            listDirtyNotesRequestId));
    }
    QNTRACE("Emitting the request to fetch unsynchronized notes "
            << "from the local storage: request id = "
            << listDirtyNotesRequestId);
    LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);
    Q_EMIT requestLocalUnsynchronizedNotes(
        listDirtyObjectsFlag, getNoteOptions,
        limit, offset, notesOrder, orderDirection,
        linkedNotebookGuid, listDirtyNotesRequestId);

    if (emptyLinkedNotebookGuid)
    {
        LocalStorageManager::ListObjectsOptions linkedNotebooksListOption =
            LocalStorageManager::ListAll;
        LocalStorageManager::ListLinkedNotebooksOrder::type linkedNotebooksOrder =
            LocalStorageManager::ListLinkedNotebooksOrder::NoOrder;
        m_listLinkedNotebooksRequestId = QUuid::createUuid();
        QNTRACE("Emitting the request to fetch unsynchronized "
                << "linked notebooks from the local storage: request id = "
                << m_listLinkedNotebooksRequestId);
        Q_EMIT requestLinkedNotebooksList(linkedNotebooksListOption, limit, offset,
                                          linkedNotebooksOrder, orderDirection,
                                          m_listLinkedNotebooksRequestId);
    }

    if (!emptyLinkedNotebookGuid) {
        Q_UNUSED(m_linkedNotebookGuidsForWhichStuffWasRequestedFromLocalStorage.insert(
            linkedNotebookGuid))
    }

    return true;
}

void SendLocalChangesManager::checkListLocalStorageObjectsCompletion()
{
    QNDEBUG("SendLocalChangesManager::"
            "checkListLocalStorageObjectsCompletion");

    if (!m_listDirtyTagsRequestId.isNull()) {
        QNTRACE("The last request for the list of new and dirty "
                "tags was not processed yet");
        return;
    }

    if (!m_listDirtySavedSearchesRequestId.isNull()) {
        QNTRACE("The last request for the list of new and dirty "
                "saved searches was not processed yet");
        return;
    }

    if (!m_listDirtyNotebooksRequestId.isNull()) {
        QNTRACE("The last request for the list of new and dirty "
                "notebooks was not processed yet");
        return;
    }

    if (!m_listDirtyNotesRequestId.isNull()) {
        QNTRACE("The last request for the list of new and dirty "
                "notes was not processed yet");
        return;
    }

    if (!m_listLinkedNotebooksRequestId.isNull()) {
        QNTRACE("The last request for the list of linked notebooks "
                "was not processed yet");
        return;
    }

    if (!m_receivedDirtyLocalStorageObjectsFromUsersAccount)
    {
        m_receivedDirtyLocalStorageObjectsFromUsersAccount = true;
        QNTRACE("Received all dirty objects from user's own account "
                << "from the local storage: " << m_tags.size()
                << " tags, " << m_savedSearches.size()
                << " saved searches, " << m_notebooks.size()
                << " notebooks and " << m_notes.size() << " notes");

        if (!m_tags.isEmpty() || !m_savedSearches.isEmpty() ||
            !m_notebooks.isEmpty() || !m_notes.isEmpty())
        {
            Q_EMIT receivedUserAccountDirtyObjects();
        }
    }

    if (!m_linkedNotebookAuthData.isEmpty())
    {
        QNTRACE("There are " << m_linkedNotebookAuthData.size()
                << " linked notebook guids, need to check if there "
                << "are those for which there is no pending request "
                << "to list stuff from the local storage yet");

        bool requestedStuffForSomeLinkedNotebook = false;

        for(auto it = m_linkedNotebookAuthData.constBegin(),
            end = m_linkedNotebookAuthData.constEnd(); it != end; ++it)
        {
            const LinkedNotebookAuthData & authData = *it;
            requestedStuffForSomeLinkedNotebook |=
                requestStuffFromLocalStorage(authData.m_guid);
        }

        if (requestedStuffForSomeLinkedNotebook) {
            QNDEBUG("Sent one or more list stuff from linked "
                    "notebooks from the local storage request ids");
            return;
        }

        if (!m_listDirtyTagsFromLinkedNotebooksRequestIds.isEmpty()) {
            QNTRACE("There are pending requests to list tags from "
                    << "linked notebooks from the local storage: "
                    << m_listDirtyTagsFromLinkedNotebooksRequestIds.size());
            return;
        }

        if (!m_listDirtyNotebooksFromLinkedNotebooksRequestIds.isEmpty()) {
            QNTRACE("There are pending requests to list notebooks "
                    << "from linked notebooks from the local storage: "
                    << m_listDirtyNotebooksFromLinkedNotebooksRequestIds.size());
            return;
        }

        if (!m_listDirtyNotesFromLinkedNotebooksRequestIds.isEmpty()) {
            QNTRACE("There are pending requests to list notes from "
                    << "linked notebooks from the local storage: "
                    << m_listDirtyNotesFromLinkedNotebooksRequestIds.size());
            return;
        }
    }

    m_receivedAllDirtyLocalStorageObjects = true;
    QNTRACE("All relevant objects from the local storage have been listed");

    if (!m_tags.isEmpty() || !m_savedSearches.isEmpty() ||
        !m_notebooks.isEmpty() || !m_notes.isEmpty())
    {
        if (!m_linkedNotebookAuthData.isEmpty()) {
            Q_EMIT receivedDirtyObjectsFromLinkedNotebooks();
        }

        sendLocalChanges();
    }
    else
    {
        QNINFO("No modified or new synchronizable objects were "
               "found in the local storage, nothing to send to "
               "Evernote service");
        finalize();
    }
}

void SendLocalChangesManager::sendLocalChanges()
{
    QNDEBUG("SendLocalChangesManager::sendLocalChanges");

    if (!checkAndRequestAuthenticationTokensForLinkedNotebooks()) {
        return;
    }

#define CHECK_RATE_LIMIT()                                                     \
    if (rateLimitIsActive()) {                                                 \
        return;                                                                \
    }                                                                          \
// CHECK_RATE_LIMIT

    if (!m_tags.isEmpty()) {
        sendTags();
        CHECK_RATE_LIMIT()
    }

    if (!m_savedSearches.isEmpty()) {
        sendSavedSearches();
        CHECK_RATE_LIMIT()
    }

    if (!m_notebooks.isEmpty()) {
        sendNotebooks();
        CHECK_RATE_LIMIT()
    }

    if (!m_notes.isEmpty()) {
        /**
         * NOTE: in case of API rate limits breaching this can be done multiple
         * times but it's safer to do overwork than not to do some important
         * missing piece so it's ok to repeatedly search for notebooks here
         */
        findNotebooksForNotes();
    }
}

void SendLocalChangesManager::sendTags()
{
    QNDEBUG("SendLocalChangesManager::sendTags");

    ErrorString errorDescription;
    bool res = sortTagsByParentChildRelations(m_tags, errorDescription);
    if (Q_UNLIKELY(!res)) {
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription);
        return;
    }

    QHash<QString, QString> tagGuidsByLocalUid;
    tagGuidsByLocalUid.reserve(m_tags.size());

    size_t numSentTags = 0;

    for(auto it = m_tags.begin(); it != m_tags.end(); )
    {
        Tag & tag = *it;

        errorDescription.clear();
        qint32 rateLimitSeconds = 0;
        qint32 errorCode = static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);

        QString linkedNotebookAuthToken;
        QString linkedNotebookShardId;
        QString linkedNotebookNoteStoreUrl;

        if (tag.hasLinkedNotebookGuid())
        {
            auto cit = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(
                tag.linkedNotebookGuid());
            if (cit != m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())
            {
                linkedNotebookAuthToken = cit.value().first;
                linkedNotebookShardId = cit.value().second;
            }
            else
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Couldn't find the auth token "
                               "for a linked notebook when attempting to "
                               "create or update a tag from it"));
                if (tag.hasName()) {
                    errorDescription.details() = tag.name();
                }

                QNWARNING(errorDescription << ", tag: " << tag);

                auto sit = std::find_if(m_linkedNotebookAuthData.begin(),
                                        m_linkedNotebookAuthData.end(),
                                        CompareLinkedNotebookAuthDataByGuid(
                                            tag.linkedNotebookGuid()));
                if (sit == m_linkedNotebookAuthData.end()) {
                    QNWARNING("The linked notebook the tag refers "
                              "to was not found within the list "
                              "of linked notebooks received "
                              "from the local storage!");
                }

                Q_EMIT failure(errorDescription);
                return;
            }

            auto sit = std::find_if(m_linkedNotebookAuthData.begin(),
                                    m_linkedNotebookAuthData.end(),
                                    CompareLinkedNotebookAuthDataByGuid(
                                        tag.linkedNotebookGuid()));
            if (sit != m_linkedNotebookAuthData.end())
            {
                linkedNotebookNoteStoreUrl = sit->m_noteStoreUrl;
            }
            else
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Couldn't find the note store "
                               "URL for a linked notebook when attempting to "
                               "create or update a tag from it"));
                if (tag.hasName()) {
                    errorDescription.details() = tag.name();
                }

                QNWARNING(errorDescription << ", tag: " << tag);

                Q_EMIT failure(errorDescription);
                return;
            }
        }

        INoteStore * pNoteStore = nullptr;
        if (tag.hasLinkedNotebookGuid())
        {
            LinkedNotebook linkedNotebook;
            linkedNotebook.setGuid(tag.linkedNotebookGuid());
            linkedNotebook.setShardId(linkedNotebookShardId);
            linkedNotebook.setNoteStoreUrl(linkedNotebookNoteStoreUrl);
            pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);

            if (Q_UNLIKELY(!pNoteStore))
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Can't send new or modified tag: can't find or "
                               "create a note store for the linked notebook"));
                QNWARNING(errorDescription << ", linked notebook guid = "
                          << tag.linkedNotebookGuid());
                Q_EMIT failure(errorDescription);
                return;
            }

            if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty()))
            {
                ErrorString errorDescription(
                    QT_TR_NOOP("Internal error: empty note store url for "
                               "the linked notebook's note store"));
                QNWARNING(errorDescription << ", linked notebook guid = "
                          << tag.linkedNotebookGuid());
                Q_EMIT failure(errorDescription);
                return;
            }
        }
        else
        {
            pNoteStore = &(m_manager.noteStore());
        }

        bool creatingTag = !tag.hasUpdateSequenceNumber();
        if (creatingTag) {
            QNTRACE("Sending new tag: " << tag);
            errorCode = pNoteStore->createTag(tag, errorDescription,
                                              rateLimitSeconds,
                                              linkedNotebookAuthToken);
        }
        else {
            QNTRACE("Sending modified tag: " << tag);
            errorCode = pNoteStore->updateTag(tag, errorDescription,
                                              rateLimitSeconds,
                                              linkedNotebookAuthToken);
        }

        if (errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
        {
            if (rateLimitSeconds < 0)
            {
                errorDescription.setBase(QT_TR_NOOP("Rate limit reached but "
                                                    "the number of seconds "
                                                    "to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0))
            {
                errorDescription.setBase(QT_TR_NOOP("Failed to start a timer to "
                                                    "postpone the Evernote API "
                                                    "call due to rate limit "
                                                    "exceeding"));
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_sendTagsPostponeTimerId = timerId;

            QNINFO("Encountered API rate limits exceeding during "
                   << "the attempt to send new or modified tag, "
                   << ", will need to wait for " << rateLimitSeconds
                   << " seconds");
            QNDEBUG("Send tags postpone timer id = " << timerId);

            Q_EMIT rateLimitExceeded(rateLimitSeconds);
            return;
        }
        else if (errorCode ==
                 static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
        {
            if (!tag.hasLinkedNotebookGuid())
            {
                handleAuthExpiration();
            }
            else
            {
                auto cit =
                    m_authenticationTokenExpirationTimesByLinkedNotebookGuid.find(
                        tag.linkedNotebookGuid());
                if (cit == m_authenticationTokenExpirationTimesByLinkedNotebookGuid.end())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Couldn't find the expiration time of "
                                   "a linked notebook's authentication token"));
                    QNWARNING(errorDescription << ", linked notebook guid = "
                              << tag.linkedNotebookGuid());
                    Q_EMIT failure(errorDescription);
                }
                else if (checkAndRequestAuthenticationTokensForLinkedNotebooks())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Unexpected AUTH_EXPIRED error: "
                                   "authentication tokens for all linked "
                                   "notebooks are still valid"));
                    QNWARNING(errorDescription << ", linked notebook guid = "
                              << tag.linkedNotebookGuid());
                    Q_EMIT failure(errorDescription);
                }
            }

            return;
        }
        else if (errorCode ==
                 static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT))
        {
            QNINFO("Encountered DATA_CONFLICT exception while trying to send "
                   "new and/or modified tags, it means the incremental sync "
                   "should be repeated before sending the changes to Evernote "
                   "service");
            Q_EMIT conflictDetected();
            stop();
            return;
        }
        else if (errorCode != 0) {
            ErrorString error(QT_TR_NOOP("Failed to send new and/or modified "
                                         "tags to Evernote service"));
            error.additionalBases().append(errorDescription.base());
            error.additionalBases().append(errorDescription.additionalBases());
            error.details() = errorDescription.details();
            QNWARNING(error);
            Q_EMIT failure(error);
            return;
        }

        QNDEBUG("Successfully sent the tag to Evernote");

        // Now the tag should have obtained guid, need to set this guid
        // as parent tag guid for child tags

        if (Q_UNLIKELY(!tag.hasGuid()))
        {
            ErrorString error(
                QT_TR_NOOP("The tag just sent to Evernote has no guid"));
            if (tag.hasName()) {
                error.details() = tag.name();
            }
            Q_EMIT failure(error);
            return;
        }

        auto nextTagIt = it;
        ++nextTagIt;
        for(auto nit = nextTagIt; nit != m_tags.end(); ++nit)
        {
            Tag & otherTag = *nit;
            if (otherTag.hasParentLocalUid() &&
                (otherTag.parentLocalUid() == tag.localUid()))
            {
                otherTag.setParentGuid(tag.guid());
            }
        }

        tagGuidsByLocalUid[tag.localUid()] = tag.guid();

        tag.setDirty(false);
        QUuid updateTagRequestId = QUuid::createUuid();
        Q_UNUSED(m_updateTagRequestIds.insert(updateTagRequestId))
        QNTRACE("Emitting the request to update tag "
                << "(remove dirty flag from it): request id = "
                << updateTagRequestId << ", tag: " << tag);
        Q_EMIT updateTag(tag, updateTagRequestId);

        if (!m_shouldRepeatIncrementalSync)
        {
            QNTRACE("Checking if we are still in sync with the remote service");

            if (!tag.hasUpdateSequenceNumber())
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Tag's update sequence number is not set after "
                               "it being sent to the service"));
                Q_EMIT failure(errorDescription);
                return;
            }

            int * pLastUpdateCount = nullptr;
            if (!tag.hasLinkedNotebookGuid())
            {
                pLastUpdateCount = &m_lastUpdateCount;
                QNTRACE("Current tag does not belong to linked notebook");
            }
            else
            {
                QNTRACE("Current tag belongs to linked notebook with guid "
                        << tag.linkedNotebookGuid());

                auto lit = m_lastUpdateCountByLinkedNotebookGuid.find(
                    tag.linkedNotebookGuid());
                if (lit == m_lastUpdateCountByLinkedNotebookGuid.end())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Can't find the update count per linked "
                                   "notebook guid on attempt to check the "
                                   "update count of tag sent to Evernote "
                                   "service"));
                    Q_EMIT failure(errorDescription);
                    return;
                }

                pLastUpdateCount = &lit.value();
            }

            if (tag.updateSequenceNumber() == *pLastUpdateCount + 1) {
                *pLastUpdateCount = tag.updateSequenceNumber();
                QNTRACE("The client is in sync with the service; "
                        << "updated corresponding last update count to "
                        << *pLastUpdateCount);
            }
            else {
                m_shouldRepeatIncrementalSync = true;
                Q_EMIT shouldRepeatIncrementalSync();
                QNTRACE("The client is not in sync with the service");
            }
        }

        it = m_tags.erase(it);
        ++numSentTags;
    }

    if (numSentTags != 0) {
        QNINFO("Sent " << numSentTags
               << " locally added/updated tags to Evernote");
    }
    else {
        QNINFO("Found no locally added/modified tags to send to Evernote");
    }

    // Need to set tag guids for all dirty notes which have the corresponding
    // tags local uids
    for(auto nit = m_notes.begin(), end = m_notes.end(); nit != end; ++nit)
    {
        Note & note = *nit;
        if (!note.hasTagLocalUids()) {
            continue;
        }

        QStringList noteTagGuids;
        if (note.hasTagGuids()) {
            noteTagGuids = note.tagGuids();
        }

        const QStringList & tagLocalUids = note.tagLocalUids();
        for(auto tit = tagLocalUids.constBegin(),
            tend = tagLocalUids.constEnd(); tit != tend; ++tit)
        {
            auto git = tagGuidsByLocalUid.find(*tit);
            if (git == tagGuidsByLocalUid.constEnd()) {
                continue;
            }

            const QString & tagGuid = git.value();
            if (noteTagGuids.contains(tagGuid)) {
                continue;
            }

            note.addTagGuid(tagGuid);
        }
    }

    /**
     * If we got here, m_tags should be empty; m_updateTagRequestIds would
     * unlikely be empty, it should only happen if updateNote signal-slot
     * connection executed synchronously which is unlikely but still
     * theoretically possible so accounting for this possibility
     */
    if (m_updateTagRequestIds.isEmpty()) {
        checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::sendSavedSearches()
{
    QNDEBUG("SendLocalChangesManager::sendSavedSearches");

    ErrorString errorDescription;
    INoteStore & noteStore = m_manager.noteStore();
    size_t numSentSavedSearches = 0;

    for(auto it = m_savedSearches.begin(); it != m_savedSearches.end(); )
    {
        SavedSearch & search = *it;

        errorDescription.clear();
        qint32 rateLimitSeconds = 0;
        qint32 errorCode = static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);

        bool creatingSearch = !search.hasUpdateSequenceNumber();
        if (creatingSearch) {
            QNTRACE("Sending new saved search: " << search);
            errorCode = noteStore.createSavedSearch(search, errorDescription,
                                                    rateLimitSeconds);
        }
        else {
            QNTRACE("Sending modified saved search: " << search);
            errorCode = noteStore.updateSavedSearch(search, errorDescription,
                                                    rateLimitSeconds);
        }

        if (errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
        {
            if (rateLimitSeconds < 0)
            {
                errorDescription.setBase(QT_TR_NOOP("Rate limit reached but "
                                                    "the number of seconds "
                                                    "to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0))
            {
                errorDescription.setBase(QT_TR_NOOP("Failed to start a timer "
                                                    "to postpone the Evernote "
                                                    "API call due to rate limit "
                                                    "exceeding"));
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_sendSavedSearchesPostponeTimerId = timerId;

            QNINFO("Encountered API rate limits exceeding during "
                   << "the attempt to send new or modified saved "
                   << "search, will need to wait for " << rateLimitSeconds
                   << " seconds");
            QNDEBUG("Send saved searches postpone timer id = " << timerId);

            Q_EMIT rateLimitExceeded(rateLimitSeconds);
            return;
        }
        else if (errorCode ==
                 static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
        {
            handleAuthExpiration();
            return;
        }
        else if (errorCode ==
                 static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT))
        {
            QNINFO("Encountered DATA_CONFLICT exception while "
                   "trying to send new and/or modified saved searches, "
                   "it means the incremental sync should be repeated "
                   "before sending the changes to the service");
            Q_EMIT conflictDetected();
            stop();
            return;
        }
        else if (errorCode != 0)
        {
            ErrorString error(QT_TR_NOOP("Failed to send new and/or modified "
                                         "saved searches to Evernote service"));
            error.additionalBases().append(errorDescription.base());
            error.additionalBases().append(errorDescription.additionalBases());
            error.details() = errorDescription.details();
            QNWARNING(error);
            Q_EMIT failure(error);
            return;
        }

        QNDEBUG("Successfully sent the saved search to Evernote");

        search.setDirty(false);
        QUuid updateSavedSearchRequestId = QUuid::createUuid();
        Q_UNUSED(m_updateSavedSearchRequestIds.insert(updateSavedSearchRequestId))
        QNTRACE("Emitting the request to update saved search "
                << "(remove the dirty flag from it): request id = "
                << updateSavedSearchRequestId << ", saved search: "
                << search);
        Q_EMIT updateSavedSearch(search, updateSavedSearchRequestId);

        if (!m_shouldRepeatIncrementalSync)
        {
            QNTRACE("Checking if we are still in sync with the remote service");

            if (!search.hasUpdateSequenceNumber())
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Internal error: saved search's update sequence "
                               "number is not set after sending it to Evernote "
                               "service"));
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            if (search.updateSequenceNumber() == m_lastUpdateCount + 1) {
                m_lastUpdateCount = search.updateSequenceNumber();
                QNDEBUG("The client is in sync with the service; "
                        << "updated last update count to " << m_lastUpdateCount);
            }
            else {
                m_shouldRepeatIncrementalSync = true;
                QNDEBUG("The client is not in sync with the service");
                Q_EMIT shouldRepeatIncrementalSync();
            }
        }

        it = m_savedSearches.erase(it);
        ++numSentSavedSearches;
    }

    if (numSentSavedSearches != 0) {
        QNINFO("Sent " << numSentSavedSearches
               << " locally added/updated saved searches to Evernote");
    }
    else {
        QNINFO("Found no locally added/modified saved searches "
               "to send to Evernote");
    }

    /**
     * If we got here, m_savedSearches should be empty;
     * m_updateSavedSearchRequestIds would unlikely be empty, it should only
     * happen if updateSavedSearch signal-slot connection executed synchronously
     * which is unlikely but still theoretically possible so accounting for
     * this possibility
     */
    if (m_updateSavedSearchRequestIds.isEmpty()) {
        checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::sendNotebooks()
{
    QNDEBUG("SendLocalChangesManager::sendNotebooks");

    ErrorString errorDescription;

    QHash<QString, QString> notebookGuidsByLocalUid;
    notebookGuidsByLocalUid.reserve(m_notebooks.size());

    size_t numSentNotebooks = 0;

    for(auto it = m_notebooks.begin(); it != m_notebooks.end(); )
    {
        Notebook & notebook = *it;

        errorDescription.clear();
        qint32 rateLimitSeconds = 0;
        qint32 errorCode = static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);

        QString linkedNotebookAuthToken;
        QString linkedNotebookShardId;
        QString linkedNotebookNoteStoreUrl;

        if (notebook.hasLinkedNotebookGuid())
        {
            auto cit = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(
                notebook.linkedNotebookGuid());
            if (cit != m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())
            {
                linkedNotebookAuthToken = cit.value().first;
                linkedNotebookShardId = cit.value().second;
            }
            else
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Couldn't find the auth token for a linked "
                               "notebook when attempting to create or update "
                               "a notebook"));
                if (notebook.hasName()) {
                    errorDescription.details() = notebook.name();
                }

                QNWARNING(errorDescription << ", notebook: " << notebook);

                auto sit = std::find_if(m_linkedNotebookAuthData.begin(),
                                        m_linkedNotebookAuthData.end(),
                                        CompareLinkedNotebookAuthDataByGuid(
                                            notebook.linkedNotebookGuid()));
                if (sit == m_linkedNotebookAuthData.end()) {
                    QNWARNING("The linked notebook the notebook refers to was "
                              "not found within the list of linked notebooks "
                              "received from the local storage");
                }

                Q_EMIT failure(errorDescription);
                return;
            }

            auto sit = std::find_if(m_linkedNotebookAuthData.begin(),
                                    m_linkedNotebookAuthData.end(),
                                    CompareLinkedNotebookAuthDataByGuid(
                                        notebook.linkedNotebookGuid()));
            if (sit != m_linkedNotebookAuthData.end())
            {
                linkedNotebookNoteStoreUrl = sit->m_noteStoreUrl;
            }
            else
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Couldn't find the note store URL for a linked "
                               "notebook when attempting to create or update "
                               "a notebook from it"));
                if (notebook.hasName()) {
                    errorDescription.details() = notebook.name();
                }

                QNWARNING(errorDescription << ", notebook: " << notebook);
                Q_EMIT failure(errorDescription);
                return;
            }
        }

        INoteStore * pNoteStore = nullptr;
        if (notebook.hasLinkedNotebookGuid())
        {
            LinkedNotebook linkedNotebook;
            linkedNotebook.setGuid(notebook.linkedNotebookGuid());
            linkedNotebook.setShardId(linkedNotebookShardId);
            linkedNotebook.setNoteStoreUrl(linkedNotebookNoteStoreUrl);
            pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);
            if (Q_UNLIKELY(!pNoteStore))
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Can't send new or modified notebook: can't "
                               "find or create a note store for the linked "
                               "notebook"));
                QNWARNING(errorDescription << ", linked notebook guid = "
                          << notebook.linkedNotebookGuid());
                Q_EMIT failure(errorDescription);
                return;
            }

            if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty()))
            {
                ErrorString errorDescription(
                    QT_TR_NOOP("Internal error: empty note store url for "
                               "the linked notebook's note store"));
                QNWARNING(errorDescription << ", linked notebook guid = "
                          << notebook.linkedNotebookGuid());
                Q_EMIT failure(errorDescription);
                return;
            }
        }
        else
        {
            pNoteStore = &(m_manager.noteStore());
        }

        bool creatingNotebook = !notebook.hasUpdateSequenceNumber();
        if (creatingNotebook) {
            QNTRACE("Sending new notebook: " << notebook);
            errorCode = pNoteStore->createNotebook(notebook, errorDescription,
                                                   rateLimitSeconds,
                                                   linkedNotebookAuthToken);
        }
        else {
            QNTRACE("Sending modified notebook: " << notebook);
            errorCode = pNoteStore->updateNotebook(notebook, errorDescription,
                                                   rateLimitSeconds,
                                                   linkedNotebookAuthToken);
        }

        if (errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
        {
            if (rateLimitSeconds < 0)
            {
                errorDescription.setBase(QT_TR_NOOP("Rate limit reached but "
                                                    "the number of seconds "
                                                    "to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (Q_UNLIKELY(timerId == 0))
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Failed to start a timer to postpone the "
                               "Evernote API call due to rate limit exceeding"));
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_sendNotebooksPostponeTimerId = timerId;

            QNINFO("Encountered API rate limits exceeding during "
                   << "the attempt to send new or modified notebook, "
                   << ", will need to wait for " << rateLimitSeconds
                   << " seconds");
            QNDEBUG("Send notebooks postpone timer id = " << timerId);

            Q_EMIT rateLimitExceeded(rateLimitSeconds);
            return;
        }
        else if (errorCode ==
                 static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
        {
            if (!notebook.hasLinkedNotebookGuid()) {
                handleAuthExpiration();
            }
            else
            {
                auto cit =
                    m_authenticationTokenExpirationTimesByLinkedNotebookGuid.find(
                        notebook.linkedNotebookGuid());
                if (cit ==
                    m_authenticationTokenExpirationTimesByLinkedNotebookGuid.end())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Couldn't find the linked notebook auth "
                                   "token's expiration time"));
                    QNWARNING(errorDescription << ", linked notebook guid = "
                              << notebook.linkedNotebookGuid());
                    Q_EMIT failure(errorDescription);
                }
                else if (checkAndRequestAuthenticationTokensForLinkedNotebooks())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Unexpected AUTH_EXPIRED error: "
                                   "authentication tokens for all linked "
                                   "notebooks are still valid"));
                    QNWARNING(errorDescription << ", linked notebook guid = "
                              << notebook.linkedNotebookGuid());
                    Q_EMIT failure(errorDescription);
                }
            }

            return;
        }
        else if (errorCode ==
                 static_cast<qint32>(qevercloud::EDAMErrorCode::DATA_CONFLICT))
        {
            QNINFO("Encountered DATA_CONFLICT exception while "
                   "trying to send new and/or modified notebooks, "
                   "it means the incremental sync should be repeated "
                   "before sending the changes to the service");
            Q_EMIT conflictDetected();
            stop();
            return;
        }
        else if (errorCode != 0)
        {
            ErrorString error(QT_TR_NOOP("Failed to send new and/or mofidied "
                                         "notebooks to Evernote service"));
            error.additionalBases().append(errorDescription.base());
            error.additionalBases().append(errorDescription.additionalBases());
            error.details() = errorDescription.details();
            QNWARNING(error);
            Q_EMIT failure(error);
            return;
        }

        QNDEBUG("Successfully sent the notebook to Evernote");

        if (Q_UNLIKELY(!notebook.hasGuid()))
        {
            ErrorString error(QT_TR_NOOP("The notebook just sent to Evernote "
                                         "has no guid"));
            if (notebook.hasName()) {
                error.details() = notebook.name();
            }
            Q_EMIT failure(error);
            return;
        }

        notebookGuidsByLocalUid[notebook.localUid()] = notebook.guid();

        notebook.setDirty(false);
        QUuid updateNotebookRequestId = QUuid::createUuid();
        Q_UNUSED(m_updateNotebookRequestIds.insert(updateNotebookRequestId))
        QNTRACE("Emitting the request to update notebook "
                << "(remove dirty flag from it): request id = "
                << updateNotebookRequestId << ", notebook: " << notebook);
        Q_EMIT updateNotebook(notebook, updateNotebookRequestId);

        if (!m_shouldRepeatIncrementalSync)
        {
            QNTRACE("Checking if we are still in sync with the remote service");

            if (!notebook.hasUpdateSequenceNumber())
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Notebook's update sequence number is not set "
                               "after it was sent to Evernote service"));
                if (notebook.hasName()) {
                    errorDescription.details() = notebook.name();
                }

                Q_EMIT failure(errorDescription);
                return;
            }

            int * pLastUpdateCount = nullptr;
            if (!notebook.hasLinkedNotebookGuid())
            {
                pLastUpdateCount = &m_lastUpdateCount;
                QNTRACE("Current notebook does not belong to linked notebook");
            }
            else
            {
                QNTRACE("Current notebook belongs to linked notebook "
                        << "with guid " << notebook.linkedNotebookGuid());

                auto lit = m_lastUpdateCountByLinkedNotebookGuid.find(
                    notebook.linkedNotebookGuid());
                if (lit == m_lastUpdateCountByLinkedNotebookGuid.end())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Can't find the update count per linked "
                                   "notebook guid on attempt to check the "
                                   "update count of a notebook sent to "
                                   "Evernote service"));
                    Q_EMIT failure(errorDescription);
                    return;
                }

                pLastUpdateCount = &lit.value();
            }

            if (notebook.updateSequenceNumber() == *pLastUpdateCount + 1)
            {
                *pLastUpdateCount = notebook.updateSequenceNumber();
                QNTRACE("The client is in sync with the service; "
                        << "updated last update count to " << *pLastUpdateCount);
            }
            else
            {
                m_shouldRepeatIncrementalSync = true;
                Q_EMIT shouldRepeatIncrementalSync();
                QNTRACE("The client is not in sync with the service");
            }
        }

        it = m_notebooks.erase(it);
        ++numSentNotebooks;
    }

    if (numSentNotebooks != 0) {
        QNINFO("Sent " << numSentNotebooks
               << " locally added/updated notebooks to Evernote");
    }
    else {
        QNINFO("Found no locally added/modified notebooks to send to Evernote");
    }

    // Need to set notebook guids for all dirty notes which have the
    // corresponding notebook local uids
    for(auto nit = m_notes.begin(), end = m_notes.end(); nit != end; ++nit)
    {
        Note & note = *nit;
        if (note.hasNotebookGuid()) {
            QNDEBUG("Dirty note with local uid " << note.localUid()
                    << " already has notebook guid: " << note.notebookGuid());
            continue;
        }

        if (Q_UNLIKELY(!note.hasNotebookLocalUid()))
        {
            ErrorString error(
                QT_TR_NOOP("Detected note which doesn't have neither "
                           "notebook guid not notebook local uid"));
            APPEND_NOTE_DETAILS(error, note);
            QNWARNING(errorDescription << ", note: " << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        auto git = notebookGuidsByLocalUid.find(note.notebookLocalUid());
        if (Q_UNLIKELY(git == notebookGuidsByLocalUid.end()))
        {
            ErrorString error(
                QT_TR_NOOP("Can't find the notebook guid for one of notes"));
            APPEND_NOTE_DETAILS(error, note);
            QNWARNING(errorDescription << ", note: " << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        note.setNotebookGuid(git.value());
    }

    /**
     * If we got here, m_notebooks should be empty; m_updateNotebookRequestIds
     * would unlikely be empty, it should only happen if updateNotebook
     * signal-slot connection executed synchronously which is unlikely but still
     * theoretically possible so accounting for this possibility
     */
    if (m_updateNotebookRequestIds.isEmpty()) {
        checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::checkAndSendNotes()
{
    QNDEBUG("SendLocalChangesManager::checkAndSendNotes");

    if (m_tags.isEmpty() && m_notebooks.isEmpty() &&
        m_findNotebookRequestIds.isEmpty())
    {
        sendNotes();
    }
}

void SendLocalChangesManager::sendNotes()
{
    QNDEBUG("SendLocalChangesManager::sendNotes");

    ErrorString errorDescription;
    size_t numSentNotes = 0;

    for(auto it = m_notes.begin(); it != m_notes.end(); )
    {
        Note & note = *it;

        errorDescription.clear();
        qint32 rateLimitSeconds = 0;
        qint32 errorCode = static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);

        if (!note.hasNotebookGuid())
        {
            errorDescription.setBase(
                QT_TR_NOOP("Found a note without notebook guid"));
            APPEND_NOTE_DETAILS(errorDescription, note)
            QNWARNING(errorDescription << ", note: " << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        auto nit = m_notebooksByGuidsCache.find(note.notebookGuid());
        if (nit == m_notebooksByGuidsCache.end())
        {
            errorDescription.setBase(
                QT_TR_NOOP("Can't find the notebook for one of notes about to "
                           "be sent to Evernote service"));
            APPEND_NOTE_DETAILS(errorDescription, note)

            QNWARNING(errorDescription << ", note: " << note);
            Q_EMIT failure(errorDescription);
            return;
        }

        const Notebook & notebook = nit.value();

        QString linkedNotebookAuthToken;
        QString linkedNotebookShardId;
        QString linkedNotebookNoteStoreUrl;

        if (notebook.hasLinkedNotebookGuid())
        {
            auto cit = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(
                notebook.linkedNotebookGuid());
            if (cit != m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())
            {
                linkedNotebookAuthToken = cit.value().first;
                linkedNotebookShardId = cit.value().second;
            }
            else
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Couldn't find the auth token for a linked "
                               "notebook when attempting to create or "
                               "update a note from that notebook"));
                QNWARNING(errorDescription << ", notebook: " << notebook);

                auto sit = std::find_if(m_linkedNotebookAuthData.begin(),
                                        m_linkedNotebookAuthData.end(),
                                        CompareLinkedNotebookAuthDataByGuid(
                                            notebook.linkedNotebookGuid()));
                if (sit == m_linkedNotebookAuthData.end()) {
                    QNWARNING("The linked notebook the notebook refers to was "
                              "not found within the list of linked notebooks "
                              "received from the local storage");
                }

                Q_EMIT failure(errorDescription);
                return;
            }

            auto sit = std::find_if(m_linkedNotebookAuthData.begin(),
                                    m_linkedNotebookAuthData.end(),
                                    CompareLinkedNotebookAuthDataByGuid(
                                        notebook.linkedNotebookGuid()));
            if (sit != m_linkedNotebookAuthData.end())
            {
                linkedNotebookNoteStoreUrl = sit->m_noteStoreUrl;
            }
            else
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Couldn't find the note store URL for a linked "
                               "notebook when attempting to create or update "
                               "a note from it"));
                if (notebook.hasName()) {
                    errorDescription.details() = notebook.name();
                }

                QNWARNING(errorDescription << ", notebook: " << notebook);
                Q_EMIT failure(errorDescription);
                return;
            }
        }

        INoteStore * pNoteStore = nullptr;
        if (notebook.hasLinkedNotebookGuid())
        {
            LinkedNotebook linkedNotebook;
            linkedNotebook.setGuid(notebook.linkedNotebookGuid());
            linkedNotebook.setShardId(linkedNotebookShardId);
            linkedNotebook.setNoteStoreUrl(linkedNotebookNoteStoreUrl);
            pNoteStore = m_manager.noteStoreForLinkedNotebook(linkedNotebook);

            if (Q_UNLIKELY(!pNoteStore))
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Can't send new or modified note: can't find or "
                               "create a note store for the linked notebook"));
                QNWARNING(errorDescription << ", linked notebook guid = "
                          << notebook.linkedNotebookGuid());
                Q_EMIT failure(errorDescription);
                return;
            }

            if (Q_UNLIKELY(pNoteStore->noteStoreUrl().isEmpty()))
            {
                ErrorString errorDescription(
                    QT_TR_NOOP("Internal error: empty note store url for "
                               "the linked notebook's note store"));
                QNWARNING(errorDescription << ", linked notebook guid = "
                          << notebook.linkedNotebookGuid());
                Q_EMIT failure(errorDescription);
                return;
            }
        }
        else
        {
            pNoteStore = &(m_manager.noteStore());
        }

        /**
         * Per Evernote API documentation, clients MUST set note title quality
         * attribute to one of the following values when the corresponding note's
         * title was not manually entered by the user:
         * 1. EDAM_NOTE_TITLE_QUALITY_UNTITLED
         * 2. EDAM_NOTE_TITLE_QUALITY_LOW
         * 3. EDAM_NOTE_TITLE_QUALITY_MEDIUM
         * 4. EDAM_NOTE_TITLE_QUALITY_HIGH
         * When a user edits a note's title, clients MUST unset this value.
         *
         * It also seems that Evernote no longer accepts notes without a title,
         * so need to create some note title if it's not set
         */
        if (!note.hasTitle())
        {
            qevercloud::NoteAttributes & noteAttributes = note.noteAttributes();
            QString title;

            if (note.hasContent())
            {
                title = note.plainText();
                if (!title.isEmpty()) {
                    title.truncate(qevercloud::EDAM_NOTE_TITLE_LEN_MAX-4);
                    title = title.simplified();
                    title += QStringLiteral("...");
                }
            }

            if (title.isEmpty())
            {
                title = tr("Untitled note");
                noteAttributes.noteTitleQuality =
                    qevercloud::EDAM_NOTE_TITLE_QUALITY_UNTITLED;
            }
            else
            {
                noteAttributes.noteTitleQuality =
                    qevercloud::EDAM_NOTE_TITLE_QUALITY_LOW;
            }

            note.setTitle(title);
        }
        else if (note.hasNoteAttributes())
        {
            qevercloud::NoteAttributes & noteAttributes = note.noteAttributes();
            if (noteAttributes.noteTitleQuality.isSet() &&
                (noteAttributes.noteTitleQuality.ref() ==
                 qevercloud::EDAM_NOTE_TITLE_QUALITY_UNTITLED))
            {
                noteAttributes.noteTitleQuality.clear();
            }
        }

        // NOTE: need to ensure the note's "active" property is set to false if
        // it has deletion timestamp, otherwise Evernote would reject such note
        if (note.hasDeletionTimestamp()) {
            note.setActive(false);
        }

        bool creatingNote = !note.hasUpdateSequenceNumber();
        if (creatingNote) {
            QNTRACE("Sending new note: " << note);
            errorCode = pNoteStore->createNote(note, errorDescription,
                                               rateLimitSeconds,
                                               linkedNotebookAuthToken);
        }
        else {
            QNTRACE("Sending modified note: " << note);
            errorCode = pNoteStore->updateNote(note, errorDescription,
                                               rateLimitSeconds,
                                               linkedNotebookAuthToken);
        }

        if (errorCode ==
            static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
        {
            if (rateLimitSeconds < 0)
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Rate limit reached but the number of seconds "
                               "to wait is incorrect"));
                errorDescription.details() = QString::number(rateLimitSeconds);
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            int timerId = startTimer(SEC_TO_MSEC(rateLimitSeconds));
            if (timerId == 0)
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Failed to start a timer to postpone the "
                               "Evernote API call due to rate limit exceeding"));
                QNWARNING(errorDescription);
                Q_EMIT failure(errorDescription);
                return;
            }

            m_sendNotesPostponeTimerId = timerId;

            QNINFO("Encountered API rate limits exceeding during "
                   << "the attempt to send new or modified note, "
                   << ", will need to wait for " << rateLimitSeconds
                   << " seconds");
            QNDEBUG("Send notes postpone timer id = " << timerId);

            Q_EMIT rateLimitExceeded(rateLimitSeconds);
            return;
        }
        else if (errorCode ==
                 static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
        {
            if (!notebook.hasLinkedNotebookGuid()) {
                handleAuthExpiration();
            }
            else
            {
                auto cit = m_authenticationTokenExpirationTimesByLinkedNotebookGuid.find(
                    notebook.linkedNotebookGuid());
                if (cit == m_authenticationTokenExpirationTimesByLinkedNotebookGuid.end())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Couldn't find the linked notebook auth "
                                   "token's expiration time"));
                    QNWARNING(errorDescription << ", linked notebook guid = "
                              << notebook.linkedNotebookGuid());
                    Q_EMIT failure(errorDescription);
                }
                else if (checkAndRequestAuthenticationTokensForLinkedNotebooks())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Unexpected AUTH_EXPIRED error: "
                                   "authentication tokens for all linked "
                                   "notebooks are still valid"));
                    QNWARNING(errorDescription << ", linked notebook guid = "
                              << notebook.linkedNotebookGuid());
                    Q_EMIT failure(errorDescription);
                }
            }

            return;
        }
        else if (errorCode != 0)
        {
            ErrorString error(QT_TR_NOOP("Failed to send new and/or mofidied "
                                         "notes to Evernote service"));
            error.additionalBases().append(errorDescription.base());
            error.additionalBases().append(errorDescription.additionalBases());
            error.details() = errorDescription.details();
            QNWARNING(error);
            Q_EMIT failure(error);
            return;
        }

        QNDEBUG("Successfully sent the note to Evernote");

        note.setDirty(false);
        QUuid updateNoteRequestId = QUuid::createUuid();
        Q_UNUSED(m_updateNoteRequestIds.insert(updateNoteRequestId))
        QNTRACE("Emitting the request to update note (remove "
                << "the dirty flag from it): request id = "
                << updateNoteRequestId << ", note: " << note);

        /**
         * NOTE: update of resources and tags is required here because otherwise
         * we might end up with note which has only tag/resource local uids but
         * no tag/resource guids (if the note's tags were local i.e. newly created
         * tags/resources before the sync was launched) or, in case of resources,
         * with the list of resources lacking USN values set
         */
        LocalStorageManager::UpdateNoteOptions updateNoteOptions(
            LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
            LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData |
            LocalStorageManager::UpdateNoteOption::UpdateTags);
        Q_EMIT updateNote(note, updateNoteOptions, updateNoteRequestId);

        if (!m_shouldRepeatIncrementalSync)
        {
            QNTRACE("Checking if we are still in sync with Evernote service");

            if (!note.hasUpdateSequenceNumber())
            {
                errorDescription.setBase(
                    QT_TR_NOOP("Note's update sequence number is not set after "
                               "it was sent to Evernote service"));
                Q_EMIT failure(errorDescription);
                return;
            }

            int * pLastUpdateCount = nullptr;
            if (!notebook.hasLinkedNotebookGuid())
            {
                pLastUpdateCount = &m_lastUpdateCount;
                QNTRACE("Current note does not belong to any linked notebook");
            }
            else
            {
                QNTRACE("Current note belongs to linked notebook with guid "
                        << notebook.linkedNotebookGuid());

                auto lit = m_lastUpdateCountByLinkedNotebookGuid.find(
                    notebook.linkedNotebookGuid());
                if (lit == m_lastUpdateCountByLinkedNotebookGuid.end())
                {
                    errorDescription.setBase(
                        QT_TR_NOOP("Failed to find the update count per linked "
                                   "notebook guid on attempt to check the "
                                   "update count of a notebook sent to "
                                   "Evernote service"));
                    Q_EMIT failure(errorDescription);
                    return;
                }

                pLastUpdateCount = &lit.value();
            }

            if (note.updateSequenceNumber() == *pLastUpdateCount + 1)
            {
                *pLastUpdateCount = note.updateSequenceNumber();
                QNTRACE("The client is in sync with the service; "
                        << "updated last update count to " << *pLastUpdateCount);
            }
            else
            {
                m_shouldRepeatIncrementalSync = true;
                Q_EMIT shouldRepeatIncrementalSync();
                QNTRACE("The client is not in sync with the service: "
                        << "last update count = " << *pLastUpdateCount
                        << ", note's update sequence number is "
                        << note.updateSequenceNumber()
                        << ", whole note: " << note);
            }
        }

        it = m_notes.erase(it);
        ++numSentNotes;
    }

    if (numSentNotes != 0) {
        QNINFO("Sent " << numSentNotes
               << " locally added/updated notes to Evernote");
    }
    else {
        QNINFO("Found no locally added/modified notes to send to Evernote");
    }

    /**
     * NOTE: as notes are sent the last, after sending them we must be done;
     * the only possibly still pending transactions are those removing dirty
     * flags from sent objects within the local storage
     */
    if (m_updateNoteRequestIds.isEmpty()) {
        checkDirtyFlagRemovingUpdatesAndFinalize();
    }
}

void SendLocalChangesManager::findNotebooksForNotes()
{
    QNDEBUG("SendLocalChangesManager::findNotebooksForNotes");

    m_findNotebookRequestIds.clear();
    QSet<QString> notebookGuids;

    for(auto it = m_notes.constBegin(),
        end = m_notes.constEnd(); it != end; ++it)
    {
        const Note & note = *it;
        if (!note.hasNotebookGuid()) {
            continue;
        }

        QHash<QString, Notebook>::const_iterator nit =
            m_notebooksByGuidsCache.find(note.notebookGuid());
        if (nit == m_notebooksByGuidsCache.constEnd()) {
            Q_UNUSED(notebookGuids.insert(note.notebookGuid()));
        }
    }

    if (!notebookGuids.isEmpty())
    {
        Notebook dummyNotebook;
        dummyNotebook.unsetLocalUid();

        for(auto it = notebookGuids.constBegin(),
            end = notebookGuids.constEnd(); it != end; ++it)
        {
            const QString & notebookGuid = *it;
            dummyNotebook.setGuid(notebookGuid);

            QUuid requestId = QUuid::createUuid();
            Q_EMIT findNotebook(dummyNotebook, requestId);
            Q_UNUSED(m_findNotebookRequestIds.insert(requestId));

            QNTRACE("Sent find notebook request for notebook guid "
                    << notebookGuid << ", request id = " << requestId);
        }
    }
    else
    {
        checkAndSendNotes();
    }
}

bool SendLocalChangesManager::rateLimitIsActive() const
{
    return ( (m_sendTagsPostponeTimerId > 0) ||
             (m_sendSavedSearchesPostponeTimerId > 0) ||
             (m_sendNotebooksPostponeTimerId > 0) ||
             (m_sendNotesPostponeTimerId > 0) );
}

void SendLocalChangesManager::checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize()
{
    QNDEBUG("SendLocalChangesManager::"
            "checkSendLocalChangesAndDirtyFlagsRemovingUpdatesAndFinalize");

    if (m_tags.isEmpty() && m_savedSearches.isEmpty() &&
        m_notebooks.isEmpty() && m_notes.isEmpty())
    {
        checkDirtyFlagRemovingUpdatesAndFinalize();
        return;
    }

    QNDEBUG("Still have " << m_tags.size()
            << " not yet sent tags, "
            << m_savedSearches.size() << " not yet sent saved searches, "
            << m_notebooks.size() << " not yet sent notebooks and "
            << m_notes.size() << " not yet sent notes");
    sendLocalChanges();
}

void SendLocalChangesManager::checkDirtyFlagRemovingUpdatesAndFinalize()
{
    QNDEBUG("SendLocalChangesManager::"
            "checkDirtyFlagRemovingUpdatesAndFinalize");

    if (!m_updateTagRequestIds.isEmpty()) {
        QNDEBUG("Still pending " << m_updateTagRequestIds.size()
                << " update tag requests");
        return;
    }

    if (!m_updateSavedSearchRequestIds.isEmpty()) {
        QNDEBUG("Still pending " << m_updateSavedSearchRequestIds.size()
                << " update saved search requests");
        return;
    }

    if (!m_updateNotebookRequestIds.isEmpty()) {
        QNDEBUG("Still pending " << m_updateNotebookRequestIds.size()
                << " update notebook requests");
        return;
    }

    if (!m_updateNoteRequestIds.isEmpty()) {
        QNDEBUG("Still pending " << m_updateNoteRequestIds.size()
                << " update note requests");
        return;
    }

    QNDEBUG("Found no pending update requests");
    finalize();
}

void SendLocalChangesManager::finalize()
{
    QNDEBUG("SendLocalChangesManager::finalize: last update count = "
            << m_lastUpdateCount
            << ", last update count by linked notebook guid = "
            << m_lastUpdateCountByLinkedNotebookGuid);

    Q_EMIT finished(m_lastUpdateCount, m_lastUpdateCountByLinkedNotebookGuid);
    clear();
    m_active = false;
}

void SendLocalChangesManager::clear()
{
    QNDEBUG("SendLocalChangesManager::clear");

    disconnectFromLocalStorage();

    m_lastUpdateCount = 0;
    m_lastUpdateCountByLinkedNotebookGuid.clear();

    m_shouldRepeatIncrementalSync = false;

    m_receivedDirtyLocalStorageObjectsFromUsersAccount = false;
    m_receivedAllDirtyLocalStorageObjects = false;

    QUuid emptyId;
    m_listDirtyTagsRequestId = emptyId;
    m_listDirtySavedSearchesRequestId = emptyId;
    m_listDirtyNotebooksRequestId = emptyId;
    m_listDirtyNotesRequestId = emptyId;
    m_listLinkedNotebooksRequestId = emptyId;

    m_listDirtyTagsFromLinkedNotebooksRequestIds.clear();
    m_listDirtyNotebooksFromLinkedNotebooksRequestIds.clear();
    m_listDirtyNotesFromLinkedNotebooksRequestIds.clear();

    m_tags.clear();
    m_savedSearches.clear();
    m_notebooks.clear();
    m_notes.clear();

    m_linkedNotebookGuidsForWhichStuffWasRequestedFromLocalStorage.clear();

    m_linkedNotebookAuthData.clear();
    m_pendingAuthenticationTokensForLinkedNotebooks = false;

    // NOTE: don't clear auth tokens by linked notebook guid as well as their
    // expiration timestamps, these might be useful later on

    m_updateTagRequestIds.clear();
    m_updateSavedSearchRequestIds.clear();
    m_updateNotebookRequestIds.clear();
    m_updateNoteRequestIds.clear();

    m_findNotebookRequestIds.clear();

    /**
     * NOTE: don't get any ideas on preserving the cache, it can easily get
     * stale especially when disconnected from the local storage
     */
    m_notebooksByGuidsCache.clear();

    killAllTimers();
}

void SendLocalChangesManager::killAllTimers()
{
    QNDEBUG("SendLocalChangesManager::killAllTimers");

    if (m_sendTagsPostponeTimerId > 0) {
        killTimer(m_sendTagsPostponeTimerId);
    }
    m_sendTagsPostponeTimerId = 0;

    if (m_sendSavedSearchesPostponeTimerId > 0) {
        killTimer(m_sendSavedSearchesPostponeTimerId);
    }
    m_sendSavedSearchesPostponeTimerId = 0;

    if (m_sendNotebooksPostponeTimerId > 0) {
        killTimer(m_sendNotebooksPostponeTimerId);
    }
    m_sendNotebooksPostponeTimerId = 0;

    if (m_sendNotesPostponeTimerId > 0) {
        killTimer(m_sendNotesPostponeTimerId);
    }
    m_sendNotesPostponeTimerId = 0;
}

bool SendLocalChangesManager::checkAndRequestAuthenticationTokensForLinkedNotebooks()
{
    QNDEBUG("SendLocalChangesManager::"
            "checkAndRequestAuthenticationTokensForLinkedNotebooks");

    if (m_linkedNotebookAuthData.isEmpty()) {
        QNDEBUG("The list of linked notebook guids and share keys is empty");
        return true;
    }

    const int numLinkedNotebookGuids = m_linkedNotebookAuthData.size();
    for(int i = 0; i < numLinkedNotebookGuids; ++i)
    {
        const LinkedNotebookAuthData & authData = m_linkedNotebookAuthData.at(i);
        const QString & guid = authData.m_guid;
        if (guid.isEmpty())
        {
            ErrorString error(
                QT_TR_NOOP("Found empty linked notebook guid within "
                           "the list of linked notebook guids and "
                           "shared notebook global ids"));
            QNWARNING(error);
            Q_EMIT failure(error);
            return false;
        }

        auto it = m_authenticationTokensAndShardIdsByLinkedNotebookGuid.find(guid);
        if (it == m_authenticationTokensAndShardIdsByLinkedNotebookGuid.end())
        {
            QNDEBUG("Authentication token for linked notebook with guid "
                    << guid << " was not found; will request authentication "
                    << "tokens for all linked notebooks at once");
            m_pendingAuthenticationTokensForLinkedNotebooks = true;
            Q_EMIT requestAuthenticationTokensForLinkedNotebooks(m_linkedNotebookAuthData);
            return false;
        }

        auto eit = m_authenticationTokenExpirationTimesByLinkedNotebookGuid.find(guid);
        if (eit == m_authenticationTokenExpirationTimesByLinkedNotebookGuid.end())
        {
            ErrorString error(
                QT_TR_NOOP("Can't find the cached expiration time "
                           "of a linked notebook's authentication token"));
            QNWARNING(error << ", linked notebook guid = " << guid);
            Q_EMIT failure(error);
            return false;
        }

        const qevercloud::Timestamp & expirationTime = eit.value();
        const qevercloud::Timestamp currentTime = QDateTime::currentMSecsSinceEpoch();
        if ((expirationTime - currentTime) < HALF_AN_HOUR_IN_MSEC)
        {
            QNDEBUG("Authentication token for linked notebook with guid "
                    << guid << " is too close to expiration: its "
                    << "expiration time is "
                    << printableDateTimeFromTimestamp(expirationTime)
                    << ", current time is "
                    << printableDateTimeFromTimestamp(currentTime)
                    << "; will request new authentication tokens "
                    << "for all linked notebooks");
            m_pendingAuthenticationTokensForLinkedNotebooks = true;
            Q_EMIT requestAuthenticationTokensForLinkedNotebooks(
                m_linkedNotebookAuthData);
            return false;
        }
    }

    QNDEBUG("Got authentication tokens for all linked notebooks, "
            "can proceed with their synchronization");

    return true;
}

void SendLocalChangesManager::handleAuthExpiration()
{
    QNINFO("SendLocalChangesManager::handleAuthExpiration");
    Q_EMIT requestAuthenticationToken();
}

} // namespace quentier

