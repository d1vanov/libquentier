/*
 * Copyright 2018-2024 Dmitry Ivanov
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

#include "NoteEditorLocalStorageBroker.h"

#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <QPointer>

#include <algorithm>
#include <utility>

namespace quentier {

NoteEditorLocalStorageBroker::NoteEditorLocalStorageBroker(QObject * parent) :
    QObject(parent), m_notebooksCache(5), m_notesCache(5), m_resourcesCache(5)
{}

NoteEditorLocalStorageBroker & NoteEditorLocalStorageBroker::instance()
{
    static NoteEditorLocalStorageBroker noteEditorLocalStorageBroker;
    return noteEditorLocalStorageBroker;
}

local_storage::ILocalStoragePtr NoteEditorLocalStorageBroker::localStorage()
{
    return m_localStorage;
}

void NoteEditorLocalStorageBroker::setLocalStorage(
    local_storage::ILocalStoragePtr localStorage)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::setLocalStorage");

    if (m_localStorage == localStorage) {
        return;
    }

    if (m_localStorage) {
        disconnectFromLocalStorageNotifier(m_localStorage->notifier());
    }

    if (m_localStorageCanceler) {
        m_localStorageCanceler->cancel();
    }

    m_localStorageCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();

    m_localStorage = std::move(localStorage);
    connectToLocalStorageNotifier(m_localStorage->notifier());
}

void NoteEditorLocalStorageBroker::saveNoteToLocalStorage(
    const qevercloud::Note & note)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::saveNoteToLocalStorage: note local id = "
            << note.localId());

    const auto * pCachedNote = m_notesCache.get(note.localId());
    if (!pCachedNote) {
        QNDEBUG(
            "note_editor::NoteEditorLocalStorageBroker",
            "Haven't found the note to be saved within the cache");

        if (Q_UNLIKELY(!m_localStorage)) {
            ErrorString error{QT_TR_NOOP(
                "Cannot save note to local storage: local storage is "
                "inaccessible")};
            QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
            Q_EMIT failedToSaveNoteToLocalStorage(note.localId(), error);
            return;
        }

        const auto selfWeak = QPointer{this};

        auto future = m_localStorage->findNoteByLocalId(
            note.localId(),
            local_storage::ILocalStorage::FetchNoteOptions{} |
                local_storage::ILocalStorage::FetchNoteOption::
                    WithResourceMetadata);

        auto thenFuture = threading::then(
            std::move(future), this,
            [this, selfWeak, note, canceler = m_localStorageCanceler](
                const std::optional<qevercloud::Note> & n) {
                if (selfWeak.isNull()) {
                    return;
                }

                if (canceler && canceler->isCanceled()) {
                    QNDEBUG(
                        "note_editor::NoteEditorLocalStorageBroker",
                        "Saving the note is canceled");
                    return;
                }

                if (Q_UNLIKELY(!n)) {
                    Q_EMIT failedToSaveNoteToLocalStorage(
                        note.localId(),
                        ErrorString{QT_TR_NOOP(
                            "Cannot save note to local storage: could not "
                            "find the previous version of the note")});
                    return;
                }

                saveNoteToLocalStorageImpl(*n, note);
            });

        threading::onFailed(
            std::move(thenFuture), this,
            [this, selfWeak, note,
             canceler = m_localStorageCanceler](const QException & e) {
                if (selfWeak.isNull()) {
                    return;
                }

                if (canceler && canceler->isCanceled()) {
                    QNDEBUG(
                        "note_editor::NoteEditorLocalStorageBroker",
                        "Saving the note is canceled");
                    return;
                }

                ErrorString error{QT_TR_NOOP(
                    "Cannot save note to local storage: failed to find "
                    "the previous version of the note")};
                error.details() = QString::fromUtf8(e.what());
                Q_EMIT failedToSaveNoteToLocalStorage(note.localId(), error);
            });

        return;
    }

    saveNoteToLocalStorageImpl(*pCachedNote, note);
}

void NoteEditorLocalStorageBroker::findNoteAndNotebook(
    const QString & noteLocalId)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::findNoteAndNotebook: "
            << "note local id = " << noteLocalId);

    const auto * cachedNote = m_notesCache.get(noteLocalId);
    if (!cachedNote) {
        QNDEBUG(
            "note_editor",
            "Note was not found within the cache, looking it up in the local "
                << "storage");
        findNoteImpl(noteLocalId);
        return;
    }

    const QString & notebookLocalId = cachedNote->notebookLocalId();
    Q_ASSERT(!notebookLocalId.isEmpty());

    const auto * cachedNotebook = m_notebooksCache.get(notebookLocalId);
    if (cachedNotebook) {
        QNDEBUG(
            "note_editor::NoteEditorLocalStorageBroker",
            "Found both note and notebook within caches");
        Q_EMIT foundNoteAndNotebook(*cachedNote, *cachedNotebook);
        return;
    }

    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "Notebook was not found within the cache, looking it up in "
            << "the local storage");

    findNotebookForNoteImpl(*cachedNote);
}

void NoteEditorLocalStorageBroker::findResourceData(
    const QString & resourceLocalId)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::findResourceData: "
            << "resource local id = " << resourceLocalId);

    const auto * cachedResource = m_resourcesCache.get(resourceLocalId);
    if (cachedResource) {
        QNDEBUG(
            "note_editor::NoteEditorLocalStorageBroker",
            "Found cached resource binary data");
        Q_EMIT foundResourceData(*cachedResource);
        return;
    }

    if (Q_UNLIKELY(!m_localStorage)) {
        ErrorString error{QT_TR_NOOP(
            "Cannot find note resource data: local storage is inaccessible")};
        QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
        Q_EMIT failedToFindResourceData(resourceLocalId, error);
        return;
    }

    const auto selfWeak = QPointer{this};

    auto future = m_localStorage->findResourceByLocalId(
        resourceLocalId,
        local_storage::ILocalStorage::FetchResourceOptions{} |
            local_storage::ILocalStorage::FetchResourceOption::WithBinaryData);

    auto thenFuture = threading::then(
        std::move(future), this,
        [this, selfWeak, resourceLocalId, canceler = m_localStorageCanceler](
            const std::optional<qevercloud::Resource> & resource) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Finding resource data is canceled");
                return;
            }

            if (Q_UNLIKELY(!resource)) {
                ErrorString error{QT_TR_NOOP(
                    "Could not find note resource data in the local storage")};
                QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
                Q_EMIT failedToFindResourceData(resourceLocalId, error);
            }

            qint32 totalBinaryDataSize = 0;
            if (resource->data() && resource->data()->size()) {
                totalBinaryDataSize += *resource->data()->size();
            }
            if (resource->alternateData() && resource->alternateData()->size())
            {
                totalBinaryDataSize += *resource->alternateData()->size();
            }

            constexpr qint32 maxTotalResourceBinaryDataSize = 10485760; // 10 Mb
            if (totalBinaryDataSize < maxTotalResourceBinaryDataSize) {
                m_resourcesCache.put(resource->localId(), *resource);
            }

            Q_EMIT foundResourceData(*resource);
        });

    threading::onFailed(
        std::move(thenFuture), this,
        [this, selfWeak, resourceLocalId,
         canceler = m_localStorageCanceler](const QException & e) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Finding resource data is canceled");
                return;
            }

            ErrorString error{QT_TR_NOOP(
                "Failed to find resource data in the local storage")};
            error.details() = QString::fromUtf8(e.what());
            QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
            Q_EMIT failedToFindResourceData(resourceLocalId, error);
        });
}

void NoteEditorLocalStorageBroker::onNoteUpdated(
    const qevercloud::Note & note,
    const local_storage::ILocalStorage::UpdateNoteOptions options)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::onNoteUpdated: "
            << "options = " << options
            << ", note local id: " << note.localId());

    onNotePutImpl(note);
}

void NoteEditorLocalStorageBroker::onNotePut(const qevercloud::Note & note)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::onNoteUpdated: " << "note local id: "
                                                        << note.localId());

    onNotePutImpl(note);
}

void NoteEditorLocalStorageBroker::onNotebookPut(
    const qevercloud::Notebook & notebook)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::onNotebookPut: " << notebook);

    const QString & notebookLocalId = notebook.localId();
    if (m_notebooksCache.exists(notebookLocalId)) {
        m_notebooksCache.put(notebookLocalId, notebook);
    }

    Q_EMIT notebookUpdated(notebook);
}

void NoteEditorLocalStorageBroker::onNoteExpunged(const QString & noteLocalId)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::onNoteExpunged: note local id = "
            << noteLocalId);

    Q_UNUSED(m_notesCache.remove(noteLocalId))

    QStringList resourceLocalIdsToRemoveFromCache;
    for (const auto & pair: m_resourcesCache) {
        if (Q_UNLIKELY(pair.second.noteLocalId().isEmpty())) {
            QNTRACE(
                "note_editor",
                "Detected resource without note local id; "
                    << "will remove it from the cache: " << pair.second);
            resourceLocalIdsToRemoveFromCache << pair.first;
            continue;
        }

        if (pair.second.noteLocalId() == noteLocalId) {
            resourceLocalIdsToRemoveFromCache << pair.first;
        }
    }

    for (const auto & localId: std::as_const(resourceLocalIdsToRemoveFromCache))
    {
        Q_UNUSED(m_resourcesCache.remove(localId))
    }

    Q_EMIT noteDeleted(noteLocalId);
}

void NoteEditorLocalStorageBroker::onNotebookExpunged(
    const QString & notebookLocalId)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker::onNotebookExpunged",
        "NoteEditorLocalStorageBroker::onNotebookExpunged: notebook local id = "
            << notebookLocalId);

    Q_UNUSED(m_notebooksCache.remove(notebookLocalId))

    QStringList noteLocalIdsToRemoveFromCache;
    for (const auto & pair: m_notesCache) {
        if (Q_UNLIKELY(pair.second.notebookLocalId().isEmpty())) {
            QNTRACE(
                "note_editor",
                "Detected note without notebook local id; "
                    << "will remove it from the cache: " << pair.second);
            noteLocalIdsToRemoveFromCache << pair.first;
            continue;
        }

        if (pair.second.notebookLocalId() == notebookLocalId) {
            noteLocalIdsToRemoveFromCache << pair.first;
        }
    }

    for (const auto & localId: std::as_const(noteLocalIdsToRemoveFromCache)) {
        Q_UNUSED(m_notesCache.remove(localId))
    }

    /**
     * The list of all notes removed along with the notebook is not known:
     * if we remove only those cached resources belonging to notes we have
     * removed from the cache, we still might have stale resources within the
     * cache so it's safer to just clear all cached resources
     */
    m_resourcesCache.clear();

    Q_EMIT notebookDeleted(notebookLocalId);
}

void NoteEditorLocalStorageBroker::onResourceExpunged(
    const QString & resourceLocalId)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::onResourceExpunged: resource local id = "
            << resourceLocalId);

    Q_UNUSED(m_resourcesCache.remove(resourceLocalId))
}

void NoteEditorLocalStorageBroker::onNotePutImpl(const qevercloud::Note & note)
{
    const auto * pCachedNote = m_notesCache.get(note.localId());
    if (pCachedNote) {
        if (*pCachedNote == note) {
            return;
        }

        if (!note.resources() || note.resources()->empty()) {
            m_notesCache.put(note.localId(), note);
        }
        else {
            auto resources = *note.resources();
            for (auto & resource: resources) {
                if (resource.data()) {
                    resource.mutableData()->setBody(std::nullopt);
                }
                if (resource.alternateData()) {
                    resource.mutableAlternateData()->setBody(std::nullopt);
                }
            }

            qevercloud::Note noteWithoutResourceDataBodies = note;
            noteWithoutResourceDataBodies.setResources(resources);
            m_notesCache.put(note.localId(), noteWithoutResourceDataBodies);
        }
    }

    Q_EMIT noteUpdated(note);
}

void NoteEditorLocalStorageBroker::connectToLocalStorageNotifier(
    const local_storage::ILocalStorageNotifier * notifier) const
{
    QObject::connect(
        notifier, &local_storage::ILocalStorageNotifier::noteUpdated, this,
        &NoteEditorLocalStorageBroker::onNoteUpdated);

    QObject::connect(
        notifier, &local_storage::ILocalStorageNotifier::notePut, this,
        &NoteEditorLocalStorageBroker::onNotePut);

    QObject::connect(
        notifier, &local_storage::ILocalStorageNotifier::notebookPut, this,
        &NoteEditorLocalStorageBroker::onNotebookPut);

    QObject::connect(
        notifier, &local_storage::ILocalStorageNotifier::noteExpunged, this,
        &NoteEditorLocalStorageBroker::onNoteExpunged);

    QObject::connect(
        notifier, &local_storage::ILocalStorageNotifier::notebookExpunged, this,
        &NoteEditorLocalStorageBroker::onNotebookExpunged);

    QObject::connect(
        notifier, &local_storage::ILocalStorageNotifier::resourceExpunged, this,
        &NoteEditorLocalStorageBroker::onResourceExpunged);
}

void NoteEditorLocalStorageBroker::disconnectFromLocalStorageNotifier(
    const local_storage::ILocalStorageNotifier * notifier)
{
    notifier->disconnect(this);
}

void NoteEditorLocalStorageBroker::findNoteImpl(const QString & noteLocalId)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::findNoteImpl: note local id = "
            << noteLocalId);

    if (Q_UNLIKELY(!m_localStorage)) {
        ErrorString error{
            QT_TR_NOOP("Cannot find note: local storage is inaccessible")};
        QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
        Q_EMIT failedToFindNoteOrNotebook(noteLocalId, error);
        return;
    }

    const auto selfWeak = QPointer{this};

    auto future = m_localStorage->findNoteByLocalId(
        noteLocalId,
        local_storage::ILocalStorage::FetchNoteOptions{} |
            local_storage::ILocalStorage::FetchNoteOption::
                WithResourceMetadata);

    auto thenFuture = threading::then(
        std::move(future), this,
        [this, selfWeak, noteLocalId, canceler = m_localStorageCanceler](
            const std::optional<qevercloud::Note> & note) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Finding note is canceled");
                return;
            }

            if (Q_UNLIKELY(!note)) {
                ErrorString error{QT_TR_NOOP(
                    "Could not find note in local storage by local id")};
                error.details() = noteLocalId;
                QNDEBUG("note_editor::NoteEditorLocalStorageBroker", error);
                Q_EMIT failedToFindNoteOrNotebook(noteLocalId, error);
                return;
            }

            m_notesCache.put(note->localId(), *note);

            const QString & notebookLocalId = note->notebookLocalId();
            const auto * cachedNotebook = m_notebooksCache.get(notebookLocalId);

            if (cachedNotebook) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Found notebook within the cache");
                Q_EMIT foundNoteAndNotebook(*note, *cachedNotebook);
            }
            else {
                QNDEBUG(
                    "note_editor",
                    "Notebook was not found within the cache, looking it up "
                        << "in the local storage");

                findNotebookForNoteImpl(*note);
            }
        });

    threading::onFailed(
        std::move(thenFuture), this,
        [this, selfWeak, noteLocalId,
         canceler = m_localStorageCanceler](const QException & e) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Finding note is canceled");
                return;
            }

            ErrorString error{
                QT_TR_NOOP("Failed to find note in local storage")};
            error.details() = QString::fromUtf8(e.what());
            QNDEBUG("note_editor::NoteEditorLocalStorageBroker", error);
            Q_EMIT failedToFindNoteOrNotebook(noteLocalId, error);
        });
}

void NoteEditorLocalStorageBroker::findNotebookForNoteImpl(
    const qevercloud::Note & note)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::findNotebookForNoteImpl: "
            << "note local id = " << note.localId()
            << ", notebook local id = " << note.notebookLocalId());

    if (Q_UNLIKELY(!m_localStorage)) {
        ErrorString error{
            QT_TR_NOOP("Cannot find notebook: local storage is inaccessible")};
        QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
        Q_EMIT failedToFindNoteOrNotebook(note.localId(), error);
        return;
    }

    const auto selfWeak = QPointer{this};

    auto future = m_localStorage->findNotebookByLocalId(note.notebookLocalId());
    auto thenFuture = threading::then(
        std::move(future), this,
        [this, selfWeak, note, canceler = m_localStorageCanceler](
            const std::optional<qevercloud::Notebook> & notebook) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Finding notebook for note is canceled");
                return;
            }

            if (Q_UNLIKELY(!notebook)) {
                ErrorString error{QT_TR_NOOP(
                    "Could not find notebook in local storage by local id")};
                error.details() = note.notebookLocalId();
                QNDEBUG("note_editor::NoteEditorLocalStorageBroker", error);
                Q_EMIT failedToFindNoteOrNotebook(note.localId(), error);
                return;
            }

            m_notebooksCache.put(notebook->localId(), *notebook);
            Q_EMIT foundNoteAndNotebook(note, *notebook);
        });

    threading::onFailed(
        std::move(thenFuture), this,
        [this, selfWeak, note,
         canceler = m_localStorageCanceler](const QException & e) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Finding notebook for note is canceled");
                return;
            }

            ErrorString error{
                QT_TR_NOOP("Failed to find notebook in local storage")};
            error.details() = QString::fromUtf8(e.what());
            QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
            Q_EMIT failedToFindNoteOrNotebook(note.localId(), error);
            return;
        });
}

void NoteEditorLocalStorageBroker::saveNoteToLocalStorageImpl(
    const qevercloud::Note & previousNoteVersion,
    const qevercloud::Note & updatedNoteVersion)
{
    QNDEBUG(
        "note_editor",
        "NoteEditorLocalStorageBroker::saveNoteToLocalStorageImpl");

    QNTRACE(
        "note_editor",
        "Previous note version: " << previousNoteVersion
                                  << "\nUpdated note version: "
                                  << updatedNoteVersion);

    if (Q_UNLIKELY(!m_localStorage)) {
        ErrorString error{
            QT_TR_NOOP("Cannot save note: local storage is inaccessible")};
        QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
        Q_EMIT failedToSaveNoteToLocalStorage(
            updatedNoteVersion.localId(), error);
        return;
    }

    const auto previousNoteResources =
        previousNoteVersion.resources().value_or(QList<qevercloud::Resource>{});

    const auto resources =
        updatedNoteVersion.resources().value_or(QList<qevercloud::Resource>{});

    QList<qevercloud::Resource> newAndUpdatedResources;
    for (const auto & resource: std::as_const(resources)) {
        if (!resource.data() || !resource.data()->body()) {
            continue;
        }

        const QString & resourceLocalId = resource.localId();
        const auto it = std::find_if(
            previousNoteResources.constBegin(),
            previousNoteResources.constEnd(),
            [&resourceLocalId](const qevercloud::Resource & r) {
                return r.localId() == resourceLocalId;
            });
        if (it == previousNoteResources.constEnd()) {
            QNDEBUG(
                "note_editor::NoteEditorLocalStorageBroker",
                "Resource with local id "
                    << resourceLocalId
                    << " did not appear in the previous note version");
            newAndUpdatedResources << resource;
            continue;
        }

        const qevercloud::Resource & prevResource = *it;
        const bool changed = [&] {
            const bool resourceHasDataSize =
                (resource.data() && resource.data()->size());

            const bool prevResourceHasDataSize =
                (prevResource.data() && prevResource.data()->size());

            if (resourceHasDataSize != prevResourceHasDataSize) {
                return true;
            }

            if (resourceHasDataSize &&
                (*resource.data()->size() != *prevResource.data()->size()))
            {
                return true;
            }

            ////////////////////////////////////////////////////////////////////

            const bool resourceHasDataHash =
                (resource.data() && resource.data()->bodyHash());

            const bool prevResourceHasDataHash =
                (prevResource.data() && prevResource.data()->bodyHash());

            if (resourceHasDataHash != prevResourceHasDataHash) {
                return true;
            }

            if (resourceHasDataHash &&
                (*resource.data()->bodyHash() !=
                 *prevResource.data()->bodyHash()))
            {
                return true;
            }

            ////////////////////////////////////////////////////////////////////

            const bool resourceHasAlternateDataSize =
                (resource.alternateData() && resource.alternateData()->size());

            const bool prevResourceHasAlternateDataSize =
                (prevResource.alternateData() &&
                 prevResource.alternateData()->size());

            if (resourceHasAlternateDataSize !=
                prevResourceHasAlternateDataSize)
            {
                return true;
            }

            if (resourceHasAlternateDataSize &&
                (*resource.alternateData()->size() !=
                 *prevResource.alternateData()->size()))
            {
                return true;
            }

            ////////////////////////////////////////////////////////////////////

            const bool resourceHasAlternateDataHash =
                (resource.alternateData() &&
                 resource.alternateData()->bodyHash());

            const bool prevResourceHasAlternateDataHash =
                (prevResource.alternateData() &&
                 prevResource.alternateData()->bodyHash());

            if (resourceHasAlternateDataHash !=
                prevResourceHasAlternateDataHash)
            {
                return true; // NOLINT
            }

            return false;
        }();

        if (changed) {
            QNDEBUG(
                "note_editor::NoteEditorLocalStorageBroker",
                "Resource with local id "
                    << resource.localId()
                    << " has changed since the previous note version");
            newAndUpdatedResources << resource;
        }

        QNDEBUG(
            "note_editor::NoteEditorLocalStorageBroker",
            "Resource with local id "
                << resource.localId()
                << " has not changed since the previous note version");
    }

    QStringList expungedResourcesLocalIds;
    for (const auto & resource: std::as_const(previousNoteResources)) {
        const auto & localId = resource.localId();

        const auto it = std::find_if(
            resources.constBegin(), resources.constEnd(),
            [&localId](const qevercloud::Resource & r) {
                return r.localId() == localId;
            });
        if (it == resources.constEnd()) {
            QNDEBUG(
                "note_editor::NoteEditorLocalStorageBroker",
                "Resource with local id "
                    << localId << " no longer appears in the updated note");
            expungedResourcesLocalIds << localId;
        }
    }

    if (newAndUpdatedResources.isEmpty() && expungedResourcesLocalIds.isEmpty())
    {
        QNDEBUG(
            "note_editor::NoteEditorLocalStorageBroker",
            "No change detected in note's resources");

        updateNoteImpl(updatedNoteVersion);
        return;
    }

    QFuture<void> putAllResourcesFuture = [&, this] {
        if (newAndUpdatedResources.isEmpty()) {
            return threading::makeReadyFuture();
        }

        QList<QFuture<void>> futures;
        futures.reserve(newAndUpdatedResources.size());
        for (const auto & resource: std::as_const(newAndUpdatedResources)) {
            futures << m_localStorage->putResource(resource);
        }

        return threading::whenAll(std::move(futures));
    }();

    QFuture<void> expungeAllResourcesFuture = [&, this] {
        if (expungedResourcesLocalIds.isEmpty()) {
            return threading::makeReadyFuture();
        }

        QList<QFuture<void>> futures;
        futures.reserve(expungedResourcesLocalIds.size());
        for (const auto & localId: std::as_const(expungedResourcesLocalIds)) {
            futures << m_localStorage->expungeResourceByLocalId(localId);
        }

        return threading::whenAll(std::move(futures));
    }();

    QFuture<void> resourcesFuture = threading::whenAll(
        QList<QFuture<void>>{} << putAllResourcesFuture
                               << expungeAllResourcesFuture);

    const auto selfWeak = QPointer{this};

    auto thenFuture = threading::then(
        std::move(resourcesFuture), this,
        [this, selfWeak, note = updatedNoteVersion,
         canceler = m_localStorageCanceler] {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Note updating processing is canceled");
                return;
            }

            updateNoteImpl(note);
        });

    threading::onFailed(
        std::move(thenFuture), this,
        [this, selfWeak, noteLocalId = updatedNoteVersion.localId(),
         canceler = m_localStorageCanceler](const QException & e) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Note updating processing is canceled");
                return;
            }

            ErrorString error{QT_TR_NOOP(
                "Failed to update note's resources in the local storage")};
            error.details() = QString::fromUtf8(e.what());
            QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
            Q_EMIT failedToSaveNoteToLocalStorage(noteLocalId, error);
        });
}

void NoteEditorLocalStorageBroker::updateNoteImpl(const qevercloud::Note & note)
{
    QNDEBUG(
        "note_editor::NoteEditorLocalStorageBroker",
        "NoteEditorLocalStorageBroker::updateNoteImpl: note local id = "
            << note.localId());

    if (Q_UNLIKELY(!m_localStorage)) {
        ErrorString error{
            QT_TR_NOOP("Cannot update note: local storage is inaccessible")};
        QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
        Q_EMIT failedToSaveNoteToLocalStorage(note.localId(), error);
        return;
    }

    // Remove the note from the cache for the time being - during the attempt to
    // update its state within the local storage its state is not really quite
    // consistent
    Q_UNUSED(m_notesCache.remove(note.localId()))

    const auto selfWeak = QPointer{this};

    auto future = m_localStorage->updateNote(
        note,
        local_storage::ILocalStorage::UpdateNoteOptions{} |
            local_storage::ILocalStorage::UpdateNoteOption::UpdateTags |
            local_storage::ILocalStorage::UpdateNoteOption::
                UpdateResourceMetadata);

    auto thenFuture = threading::then(
        std::move(future), this,
        [this, selfWeak, noteLocalId = note.localId(),
         canceler = m_localStorageCanceler] {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Note updating processing is canceled");
                return;
            }

            Q_EMIT noteSavedToLocalStorage(noteLocalId);
        });

    threading::onFailed(
        std::move(thenFuture), this,
        [this, selfWeak, noteLocalId = note.localId(),
         canceler = m_localStorageCanceler](const QException & e) {
            if (selfWeak.isNull()) {
                return;
            }

            if (canceler && canceler->isCanceled()) {
                QNDEBUG(
                    "note_editor::NoteEditorLocalStorageBroker",
                    "Note updating processing is canceled");
                return;
            }

            ErrorString error{
                QT_TR_NOOP("Failed to update note in local storage")};
            error.details() = QString::fromUtf8(e.what());
            QNWARNING("note_editor::NoteEditorLocalStorageBroker", error);
            Q_EMIT failedToSaveNoteToLocalStorage(noteLocalId, error);
        });
}

} // namespace quentier
