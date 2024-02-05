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

#pragma once

#include <quentier/local_storage/ILocalStorageNotifier.h>

namespace quentier::local_storage::sql {

class Notifier final : public ILocalStorageNotifier
{
    Q_OBJECT
public:
    explicit Notifier(QObject * parent = nullptr);
    ~Notifier() override = default;

    void notifyUserPut(qevercloud::User user);
    void notifyUserExpunged(qevercloud::UserID userId);

    void notifyNotebookPut(qevercloud::Notebook notebook);
    void notifyNotebookExpunged(QString notebookLocalId);

    void notifyLinkedNotebookPut(qevercloud::LinkedNotebook linkedNotebook);
    void notifyLinkedNotebookExpunged(qevercloud::Guid linkedNotebookGuid);

    void notifyNotePut(qevercloud::Note note);

    void notifyNoteUpdated(
        qevercloud::Note note, ILocalStorage::UpdateNoteOptions options);

    void notifyNoteExpunged(QString noteLocalId);

    void notifyTagPut(qevercloud::Tag tag);

    void notifyTagExpunged(
        QString tagLocalId, QStringList expungedChildTagLocalIds);

    void notifyResourcePut(qevercloud::Resource resource);
    void notifyResourceMetadataPut(qevercloud::Resource resource);
    void notifyResourceExpunged(QString resourceLocalId);

    void notifySavedSearchPut(qevercloud::SavedSearch savedSearch);
    void notifySavedSearchExpunged(QString savedSearchLocalId);
};

} // namespace quentier::local_storage::sql
