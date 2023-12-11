/*
 * Copyright 2018-2023 Dmitry Ivanov
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

#pragma once

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/cancelers/Fwd.h>
#include <quentier/utility/LRUCache.hpp>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QObject>
#include <QSet>
#include <QVector>

namespace quentier {

class NoteEditorLocalStorageBroker : public QObject
{
    Q_OBJECT
private:
    explicit NoteEditorLocalStorageBroker(QObject * parent = nullptr);

public:
    [[nodiscard]] static NoteEditorLocalStorageBroker & instance();

    [[nodiscard]] local_storage::ILocalStoragePtr localStorage();
    void setLocalStorage(local_storage::ILocalStoragePtr localStorage);

Q_SIGNALS:
    void noteSavedToLocalStorage(QString noteLocalId);

    void failedToSaveNoteToLocalStorage(
        QString noteLocalId, ErrorString errorDescription);

    void foundNoteAndNotebook(
        qevercloud::Note note, qevercloud::Notebook notebook);

    void failedToFindNoteOrNotebook(
        QString noteLocalId, ErrorString errorDescription);

    void noteUpdated(qevercloud::Note note);
    void notebookUpdated(qevercloud::Notebook notebook);
    void noteDeleted(QString noteLocalId);
    void notebookDeleted(QString notebookLocalId);

    void foundResourceData(qevercloud::Resource resource);

    void failedToFindResourceData(
        QString resourceLocalId, ErrorString errorDescription);

public Q_SLOTS:
    void saveNoteToLocalStorage(const qevercloud::Note & note);
    void findNoteAndNotebook(const QString & noteLocalId);
    void findResourceData(const QString & resourceLocalId);

private Q_SLOTS:
    void onNoteUpdated(
        const qevercloud::Note & note,
        local_storage::ILocalStorage::UpdateNoteOptions options);

    void onNotePut(const qevercloud::Note & note);
    void onNotebookPut(const qevercloud::Notebook & notebook);
    void onNoteExpunged(const QString & noteLocalId);
    void onNotebookExpunged(const QString & notebookLocalId);
    void onResourceExpunged(const QString & resourceLocalId);

private:
    void onNotePutImpl(const qevercloud::Note & note);

    void connectToLocalStorageNotifier(
        const local_storage::ILocalStorageNotifier * notifier) const;

    void disconnectFromLocalStorageNotifier(
        const local_storage::ILocalStorageNotifier * notifier);

    void findNoteImpl(const QString & noteLocalId);
    void findNotebookForNoteImpl(const qevercloud::Note & note);

    void saveNoteToLocalStorageImpl(
        const qevercloud::Note & previousNoteVersion,
        const qevercloud::Note & updatedNoteVersion);

    void updateNoteImpl(const qevercloud::Note & note);

private:
    Q_DISABLE_COPY(NoteEditorLocalStorageBroker)

private:
    local_storage::ILocalStoragePtr m_localStorage;
    utility::cancelers::ManualCancelerPtr m_localStorageCanceler;

    LRUCache<QString, qevercloud::Notebook> m_notebooksCache;
    LRUCache<QString, qevercloud::Note> m_notesCache;

    /**
     * This cache stores resources with binary data but only if that data is not
     * too large to prevent spending too much memory on it
     */
    LRUCache<QString, qevercloud::Resource> m_resourcesCache;
};

} // namespace quentier
