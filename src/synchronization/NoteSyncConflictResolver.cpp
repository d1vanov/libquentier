/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include "NoteSyncConflictResolver.h"
#include "SynchronizationShared.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/DateTime.h>

#include <quentier/synchronization/INoteStore.h>

#include <QTimerEvent>

namespace quentier {

NoteSyncConflictResolver::NoteSyncConflictResolver(
    IManager & manager, const qevercloud::Note & remoteNote,
    const Note & localConflict, QObject * parent) :
    QObject(parent),
    m_manager(manager), m_remoteNote(remoteNote), m_localConflict(localConflict)
{}

void NoteSyncConflictResolver::start()
{
    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::start: remote note guid = "
            << (m_remoteNote.guid.isSet() ? m_remoteNote.guid.ref()
                                          : QStringLiteral("<not set>"))
            << ", local conflict local uid = " << m_localConflict.localUid());

    if (m_started) {
        QNDEBUG("synchronization:note_conflict", "Already started");
        return;
    }

    m_started = true;

    connectToLocalStorage();
    processNotesConflictByGuid();
}

void NoteSyncConflictResolver::onAuthDataUpdated(
    QString authToken, QString shardId, qevercloud::Timestamp expirationTime)
{
    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::onAuthDataUpdated");

    Q_UNUSED(authToken)
    Q_UNUSED(shardId)
    Q_UNUSED(expirationTime)

    if (Q_UNLIKELY(!m_pendingAuthDataUpdate)) {
        QNWARNING(
            "synchronization:note_conflict",
            "NoteSyncConflictResolver: "
                << "received unexpected auth data update, ignoring it");
        return;
    }

    m_pendingAuthDataUpdate = false;
    Q_UNUSED(downloadFullRemoteNoteData())
}

void NoteSyncConflictResolver::onLinkedNotebooksAuthDataUpdated(
    QHash<QString, std::pair<QString, QString>>
        authTokensAndShardIdsByLinkedNotebookGuid,
    QHash<QString, qevercloud::Timestamp>
        authTokenExpirationTimesByLinkedNotebookGuid)
{
    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::onLinkedNotebooksAuthDataUpdated");

    Q_UNUSED(authTokensAndShardIdsByLinkedNotebookGuid)
    Q_UNUSED(authTokenExpirationTimesByLinkedNotebookGuid)

    if (Q_UNLIKELY(!m_pendingLinkedNotebookAuthDataUpdate)) {
        QNWARNING(
            "synchronization:note_conflict",
            "NoteSyncConflictResolver: "
                << "received unexpected linked notebook auth data update, "
                   "ignoring "
                << "it");
        return;
    }

    m_pendingLinkedNotebookAuthDataUpdate = false;
    Q_UNUSED(downloadFullRemoteNoteData())
}

void NoteSyncConflictResolver::onAddNoteComplete(Note note, QUuid requestId)
{
    if (requestId != m_addNoteRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::onAddNoteComplete: request id = "
            << requestId << ", note: " << note);

    m_addNoteRequestId = QUuid();

    if (m_pendingRemoteNoteAdditionToLocalStorage) {
        QNDEBUG(
            "synchronization:note_conflict",
            "Successfully added remote "
                << "note as a new note to the local storage");
        m_pendingRemoteNoteAdditionToLocalStorage = false;
        Q_EMIT finished(m_remoteNote);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local notes: internal error, received "
                       "unidentified note addition acknowledge "
                       "event within the local storage"));
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
}

void NoteSyncConflictResolver::onAddNoteFailed(
    Note note, ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_addNoteRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::onAddNoteFailed: request id = "
            << requestId << ", error description = " << errorDescription
            << "; note: " << note);

    m_addNoteRequestId = QUuid();
    Q_EMIT failure(m_remoteNote, errorDescription);
}

void NoteSyncConflictResolver::onUpdateNoteComplete(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    if (requestId != m_updateNoteRequestId) {
        return;
    }

    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::onUpdateNoteComplete: note = "
            << note << "\nRequest id = " << requestId
            << ", update resource metadata = "
            << ((options &
                 LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata)
                    ? "true"
                    : "false")
            << ", update resource binary data = "
            << ((options &
                 LocalStorageManager::UpdateNoteOption::
                     UpdateResourceBinaryData)
                    ? "true"
                    : "false")
            << ", update tags = "
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags)
                    ? "true"
                    : "false"));

    m_updateNoteRequestId = QUuid();

    if (m_pendingLocalConflictUpdateInLocalStorage) {
        QNDEBUG(
            "synchronization:note_conflict",
            "Local conflicting note was "
                << "successfully updated in the local storage");

        m_pendingLocalConflictUpdateInLocalStorage = false;

        if (m_pendingFullRemoteNoteDataDownload) {
            QNDEBUG(
                "synchronization:note_conflict",
                "Still waiting for full "
                    << "remote note's data downloading");
            return;
        }

        if (m_retryNoteDownloadingTimerId != 0) {
            QNDEBUG(
                "synchronization:note_conflict",
                "Retry full remote note "
                    << "data timer is active, hence the remote note data has "
                       "not "
                    << "been fully downloaded yet");
            return;
        }

        if (m_pendingAuthDataUpdate) {
            QNDEBUG(
                "synchronization:note_conflict",
                "Remote note was not "
                    << "downloaded properly yet, pending auth token for full "
                    << "remote note data downloading");
            return;
        }

        if (m_manager.syncingLinkedNotebooksContent() &&
            m_pendingLinkedNotebookAuthDataUpdate)
        {
            QNDEBUG(
                "synchronization:note_conflict",
                "Remote note was not "
                    << "downloaded properly yet, pending linked notebook auth "
                    << "token for full remote note data downloading");
            return;
        }

        addRemoteNoteToLocalStorageAsNewNote();
    }
    else if (m_pendingRemoteNoteUpdateInLocalStorage) {
        QNDEBUG(
            "synchronization:note_conflict",
            "Remote note was successfully "
                << "updated in the local storage");
        m_pendingRemoteNoteUpdateInLocalStorage = false;
        Q_EMIT finished(m_remoteNote);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote and local "
                       "notes: internal error, received unidentified note "
                       "update acknowledge from the local storage"));
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
}

void NoteSyncConflictResolver::onUpdateNoteFailed(
    Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    if (requestId != m_updateNoteRequestId) {
        return;
    }

    QNWARNING(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::onUpdateNoteFailed: note = "
            << note << "\nRequest id = " << requestId
            << ", update resource metadata = "
            << ((options &
                 LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata)
                    ? "true"
                    : "false")
            << ", update resource binary data = "
            << ((options &
                 LocalStorageManager::UpdateNoteOption::
                     UpdateResourceBinaryData)
                    ? "true"
                    : "false")
            << ", update tags = "
            << ((options & LocalStorageManager::UpdateNoteOption::UpdateTags)
                    ? "true"
                    : "false")
            << "; error description = " << errorDescription);

    m_updateNoteRequestId = QUuid();

    if (m_pendingLocalConflictUpdateInLocalStorage) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local notes: failed to update the local "
                       "conflicting note in the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
    else if (m_pendingRemoteNoteUpdateInLocalStorage) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local notes: failed to update "
                       "the remote note in the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
    else {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local notes: internal error, received "
                       "unidentified note update reject event "
                       "within the local storage"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote)
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
    }
}

void NoteSyncConflictResolver::onGetNoteAsyncFinished(
    qint32 errorCode, qevercloud::Note qecNote, qint32 rateLimitSeconds,
    ErrorString errorDescription)
{
    if (!qecNote.guid.isSet() || !m_remoteNote.guid.isSet() ||
        (qecNote.guid.ref() != m_remoteNote.guid.ref()))
    {
        return;
    }

    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::onGetNoteAsyncFinished: "
            << "error code = " << errorCode << ", note = " << qecNote
            << "\nRate limit seconds = " << rateLimitSeconds
            << ", error description = " << errorDescription);

    m_pendingFullRemoteNoteDataDownload = false;

    if (errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED))
    {
        if (rateLimitSeconds < 0) {
            errorDescription.setBase(
                QT_TR_NOOP("QEverCloud or Evernote protocol error: caught "
                           "RATE_LIMIT_REACHED "
                           "exception but the number of seconds "
                           "to wait is zero or negative"));
            errorDescription.details() = QString::number(rateLimitSeconds);
            QNWARNING("synchronization:note_conflict", errorDescription);
            Q_EMIT failure(m_remoteNote, errorDescription);
            return;
        }

        int timerId = startTimer(secondsToMilliseconds(rateLimitSeconds));
        if (Q_UNLIKELY(timerId == 0)) {
            errorDescription.setBase(
                QT_TR_NOOP("Failed to start a timer to postpone the Evernote "
                           "API call due to rate limit exceeding"));
            QNWARNING("synchronization:note_conflict", errorDescription);
            Q_EMIT failure(m_remoteNote, errorDescription);
            return;
        }

        QNDEBUG(
            "synchronization:note_conflict",
            "Started timer to retry "
                << "downloading full note data: timer id = " << timerId
                << ", need to wait for " << rateLimitSeconds << " seconds");
        m_retryNoteDownloadingTimerId = timerId;
        Q_EMIT rateLimitExceeded(rateLimitSeconds);
        return;
    }
    else if (
        errorCode ==
        static_cast<qint32>(qevercloud::EDAMErrorCode::AUTH_EXPIRED))
    {
        if (m_manager.syncingLinkedNotebooksContent()) {
            m_pendingLinkedNotebookAuthDataUpdate = true;
        }
        else {
            m_pendingAuthDataUpdate = true;
        }

        Q_EMIT notifyAuthExpiration();
        return;
    }
    else if (errorCode != 0) {
        Q_EMIT failure(m_remoteNote, errorDescription);
        return;
    }

    m_remoteNoteAsLocalNote.qevercloudNote() = qecNote;

    if (m_shouldOverrideLocalNoteWithRemoteNote) {
        overrideLocalNoteWithRemoteChanges();

        m_pendingRemoteNoteUpdateInLocalStorage = true;

        m_updateNoteRequestId = QUuid::createUuid();
        LocalStorageManager::UpdateNoteOptions options(
            LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
            LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData |
            LocalStorageManager::UpdateNoteOption::UpdateTags);

        QNDEBUG(
            "synchronization:note_conflict",
            "Emitting the request to "
                << "update the local conflict overridden by the remote note "
                   "within "
                << "the local storage: request id = " << m_updateNoteRequestId
                << ", note: " << m_remoteNoteAsLocalNote);
        Q_EMIT updateNote(m_localConflict, options, m_updateNoteRequestId);
    }
    else {
        if (m_pendingLocalConflictUpdateInLocalStorage) {
            QNDEBUG(
                "synchronization:note_conflict",
                "Still pending the update "
                    << "of local conflicting note in the local storage");
            return;
        }

        addRemoteNoteToLocalStorageAsNewNote();
    }
}

void NoteSyncConflictResolver::connectToLocalStorage()
{
    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::connectToLocalStorage");

    auto & localStorageManagerAsync = m_manager.localStorageManagerAsync();

    // Connect local signals to local storage manager async's slots
    QObject::connect(
        this, &NoteSyncConflictResolver::addNote, &localStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNoteRequest);

    QObject::connect(
        this, &NoteSyncConflictResolver::updateNote, &localStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    // Connect local storage manager async's signals to local slots
    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &NoteSyncConflictResolver::onAddNoteComplete);

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::addNoteFailed,
        this, &NoteSyncConflictResolver::onAddNoteFailed);

    QObject::connect(
        &localStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &NoteSyncConflictResolver::onUpdateNoteComplete);

    QObject::connect(
        &localStorageManagerAsync, &LocalStorageManagerAsync::updateNoteFailed,
        this, &NoteSyncConflictResolver::onUpdateNoteFailed);
}

void NoteSyncConflictResolver::processNotesConflictByGuid()
{
    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::processNotesConflictByGuid");

    if (Q_UNLIKELY(!m_remoteNote.guid.isSet())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local notes: the remote note has no guid set"));
        APPEND_NOTE_DETAILS(error, Note(m_remoteNote))
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(!m_remoteNote.updateSequenceNum.isSet())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local notes: the remote note has no "
                       "update sequence number set"));
        APPEND_NOTE_DETAILS(error, Note(m_remoteNote))
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_remoteNote);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(!m_localConflict.hasGuid())) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve the conflict between remote "
                       "and local notes: the local note has no guid set"));
        APPEND_NOTE_DETAILS(error, m_localConflict)
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_localConflict);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (Q_UNLIKELY(m_remoteNote.guid.ref() != m_localConflict.guid())) {
        ErrorString error(
            QT_TR_NOOP("Note sync conflict resolution was applied "
                       "to notes which do not conflict by guid"));
        APPEND_NOTE_DETAILS(error, m_localConflict)
        QNWARNING(
            "synchronization:note_conflict", error << ": " << m_localConflict);
        Q_EMIT failure(m_remoteNote, error);
        return;
    }

    if (m_localConflict.hasUpdateSequenceNumber() &&
        (m_localConflict.updateSequenceNumber() >=
         m_remoteNote.updateSequenceNum.ref()))
    {
        QNDEBUG(
            "synchronization:note_conflict",
            "The local note has update "
                << "sequence number equal to or greater than the remote note's "
                   "one "
                << "=> local note should override the remote one");
        Q_EMIT finished(m_remoteNote);
        return;
    }

    // Regardless of the exact way of further processing we need to download
    // full note data for the remote note
    if (!downloadFullRemoteNoteData()) {
        return;
    }

    if (!m_localConflict.isDirty()) {
        QNDEBUG(
            "synchronization:note_conflict",
            "The local conflicting note "
                << "is not dirty and thus should be overridden with the remote "
                << "note");
        m_shouldOverrideLocalNoteWithRemoteNote = true;
        return;
    }

    QNDEBUG(
        "synchronization:note_conflict",
        "The local conflicting note has "
            << "been marked as dirty one, need to clear Evernote-assigned "
               "fields "
            << "from it");

    m_localConflict.setGuid(QString());
    m_localConflict.setUpdateSequenceNumber(-1);
    m_localConflict.noteAttributes().conflictSourceNoteGuid =
        m_remoteNote.guid.ref();

    QString conflictingNoteTitle;
    if (m_localConflict.hasTitle()) {
        conflictingNoteTitle =
            m_localConflict.title() + QStringLiteral(" - ") + tr("conflicting");
    }
    else {
        QString previewText = m_localConflict.plainText();
        if (!previewText.isEmpty()) {
            previewText.truncate(12);
            conflictingNoteTitle =
                previewText + QStringLiteral("... - ") + tr("conflicting");
        }
        else {
            conflictingNoteTitle = tr("Conflicting note");
        }
    }

    m_localConflict.setTitle(conflictingNoteTitle);

    if (m_localConflict.hasResources()) {
        auto resources = m_localConflict.resources();
        for (auto & resource: resources) {
            resource.setGuid({});
            resource.setNoteGuid({});
            resource.setUpdateSequenceNumber(-1);
            resource.setDirty(true);
        }

        m_localConflict.setResources(resources);
    }

    m_pendingLocalConflictUpdateInLocalStorage = true;
    m_updateNoteRequestId = QUuid::createUuid();

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata);

    QNDEBUG(
        "synchronization:note_conflict",
        "Emitting the request to update "
            << "the local conflicting note (after clearing Evernote assigned "
            << "fields from it): request id = " << m_updateNoteRequestId
            << ", note to update: " << m_localConflict);
    Q_EMIT updateNote(m_localConflict, options, m_updateNoteRequestId);
}

void NoteSyncConflictResolver::overrideLocalNoteWithRemoteChanges()
{
    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::overrideLocalNoteWithRemoteChanges");

    QNTRACE(
        "synchronization:note_conflict",
        "Local conflict: " << m_localConflict
                           << "\nRemote note: " << m_remoteNoteAsLocalNote);

    Note localNote(m_localConflict);

    // Need to clear out the tag local uids from the local note so that
    // the local storage uses tag guids list from the remote note instead
    localNote.setTagLocalUids({});

    // NOTE: dealing with resources is tricky: need to not screw up the local
    // uids of note's resources
    QList<Resource> resources;
    if (localNote.hasResources()) {
        resources = localNote.resources();
    }

    localNote.qevercloudNote() = m_remoteNoteAsLocalNote.qevercloudNote();
    localNote.setDirty(false);
    localNote.setLocal(false);

    auto updatedResources = m_remoteNoteAsLocalNote.resources();

    QList<Resource> amendedResources;
    amendedResources.reserve(updatedResources.size());

    // First update those resources which were within the local note already
    for (auto & resource: resources) {
        if (!resource.hasGuid()) {
            continue;
        }

        bool foundResource = false;
        for (const auto & updatedResource: qAsConst(updatedResources)) {
            if (!updatedResource.hasGuid()) {
                continue;
            }

            if (updatedResource.guid() == resource.guid()) {
                resource.qevercloudResource() =
                    updatedResource.qevercloudResource();

                // NOTE: need to not forget to reset the dirty flag since we are
                // resetting the state of the local resource here
                resource.setDirty(false);
                resource.setLocal(false);
                foundResource = true;
                break;
            }
        }

        if (foundResource) {
            amendedResources << resource;
        }
    }

    // Then account for new resources
    for (const auto & updatedResource: qAsConst(updatedResources)) {
        if (Q_UNLIKELY(!updatedResource.hasGuid())) {
            QNWARNING(
                "synchronization:note_conflict",
                "Skipping resource from "
                    << "remote note without guid: " << updatedResource);
            continue;
        }

        const Resource * pExistingResource = nullptr;
        for (const auto & resource: qAsConst(resources)) {
            if (resource.hasGuid() &&
                (resource.guid() == updatedResource.guid())) {
                pExistingResource = &resource;
                break;
            }
        }

        if (pExistingResource) {
            continue;
        }

        Resource newResource;
        newResource.qevercloudResource() = updatedResource.qevercloudResource();
        newResource.setDirty(false);
        newResource.setLocal(false);
        newResource.setNoteLocalUid(localNote.localUid());
        amendedResources << newResource;
    }

    localNote.setResources(amendedResources);
    QNTRACE(
        "synchronization:note_conflict",
        "Local note after overriding: " << localNote);

    m_localConflict = localNote;
}

void NoteSyncConflictResolver::addRemoteNoteToLocalStorageAsNewNote()
{
    QNDEBUG(
        "synchronization:note_conflict",
        "NoteSyncConflictResolver::addRemoteNoteToLocalStorageAsNewNote");

    m_pendingRemoteNoteAdditionToLocalStorage = true;
    m_addNoteRequestId = QUuid::createUuid();

    QNDEBUG(
        "synchronization:note_conflict",
        "Emitting the request to add note "
            << "to the local storage: request id = " << m_addNoteRequestId
            << ", note: " << m_remoteNoteAsLocalNote);
    Q_EMIT addNote(m_remoteNoteAsLocalNote, m_addNoteRequestId);
}

bool NoteSyncConflictResolver::downloadFullRemoteNoteData()
{
    m_remoteNoteAsLocalNote = Note(m_remoteNote);
    m_remoteNoteAsLocalNote.setDirty(false);
    m_remoteNoteAsLocalNote.setLocal(false);

    QString authToken;
    ErrorString errorDescription;
    INoteStore * pNoteStore = m_manager.noteStoreForNote(
        m_remoteNoteAsLocalNote, authToken, errorDescription);

    if (Q_UNLIKELY(!pNoteStore)) {
        ErrorString error(
            QT_TR_NOOP("Can't resolve sync conflict between notes: internal "
                       "error, failed to find note store for the remote note"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        APPEND_NOTE_DETAILS(error, m_remoteNoteAsLocalNote);
        QNWARNING(
            "synchronization:note_conflict",
            error << ": " << m_remoteNoteAsLocalNote);
        Q_EMIT failure(m_remoteNote, error);
        return false;
    }

    QObject::connect(
        pNoteStore, &INoteStore::getNoteAsyncFinished, this,
        &NoteSyncConflictResolver::onGetNoteAsyncFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    bool withContent = true;
    bool withResourceData = true;
    bool withResourceRecognition = true;
    bool withResourceAlternateData = true;
    bool withSharedNotes = true;
    bool withNoteAppDataValues = true;
    bool withResourceAppDataValues = true;
    bool withNoteLimits = m_manager.syncingLinkedNotebooksContent();

    bool res = pNoteStore->getNoteAsync(
        withContent, withResourceData, withResourceRecognition,
        withResourceAlternateData, withSharedNotes, withNoteAppDataValues,
        withResourceAppDataValues, withNoteLimits,
        m_remoteNoteAsLocalNote.guid(), authToken, errorDescription);

    if (!res) {
        APPEND_NOTE_DETAILS(errorDescription, m_remoteNoteAsLocalNote)
        QNWARNING(
            "synchronization:note_conflict",
            errorDescription << ", note: " << m_remoteNoteAsLocalNote);
        Q_EMIT failure(m_remoteNote, errorDescription);
        return false;
    }

    m_pendingFullRemoteNoteDataDownload = true;
    QNDEBUG(
        "synchronization:note_conflict",
        "Pending full remote note data "
            << "downloading");
    return true;
}

void NoteSyncConflictResolver::timerEvent(QTimerEvent * pEvent)
{
    if (!pEvent) {
        ErrorString errorDescription(
            QT_TR_NOOP("Qt error: detected null pointer to QTimerEvent"));
        QNWARNING("synchronization:note_conflict", errorDescription);
        Q_EMIT failure(m_remoteNote, errorDescription);
        return;
    }

    int timerId = pEvent->timerId();
    killTimer(timerId);
    QNDEBUG(
        "synchronization:note_conflict", "Killed timer with id " << timerId);

    if (timerId == m_retryNoteDownloadingTimerId) {
        QNDEBUG(
            "synchronization:note_conflict",
            "NoteSyncConflictResolver::timerEvent: retrying the download of "
                << "full remote note data");
        Q_UNUSED(downloadFullRemoteNoteData());
    }
}

} // namespace quentier
