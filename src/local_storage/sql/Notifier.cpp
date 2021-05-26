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

#include "Notifier.h"

namespace quentier::local_storage::sql {

Notifier::Notifier(QObject * parent) : ILocalStorageNotifier(parent)
{}

void Notifier::notifyUserPut(qevercloud::User user)
{
    Q_EMIT userPut(std::move(user));
}

void Notifier::notifyUserExpunged(qevercloud::UserID userId)
{
    Q_EMIT userExpunged(userId);
}

void Notifier::notifyNotebookPut(qevercloud::Notebook notebook)
{
    Q_EMIT notebookPut(std::move(notebook));
}

void Notifier::notifyNotebookExpunged(QString notebookLocalId)
{
    Q_EMIT notebookExpunged(std::move(notebookLocalId));
}

void Notifier::notifyLinkedNotebookPut(
    qevercloud::LinkedNotebook linkedNotebook)
{
    Q_EMIT linkedNotebookPut(std::move(linkedNotebook));
}

void Notifier::notifyLinkedNotebookExpunged(qevercloud::Guid linkedNotebookGuid)
{
    Q_EMIT linkedNotebookExpunged(std::move(linkedNotebookGuid));
}

void Notifier::notifyNotePut(qevercloud::Note note)
{
    Q_EMIT notePut(std::move(note));
}

void Notifier::notifyNoteUpdated(
    qevercloud::Note note, ILocalStorage::UpdateNoteOptions options)
{
    Q_EMIT noteUpdated(std::move(note), options);
}

void Notifier::notifyNoteNotebookChanged(
    QString noteLocalId, QString previousNotebookLocalId,
    QString newNotebookLocalId)
{
    Q_EMIT noteNotebookChanged(
        std::move(noteLocalId), std::move(previousNotebookLocalId),
        std::move(newNotebookLocalId));
}

void Notifier::notifyNoteTagListChanged(
    QString noteLocalId, QStringList previousNoteTagLocalIds,
    QStringList newNoteTagLocalIds)
{
    Q_EMIT noteTagListChanged(
        std::move(noteLocalId), std::move(previousNoteTagLocalIds),
        std::move(newNoteTagLocalIds));
}

void Notifier::notifyNoteExpunged(QString noteLocalId)
{
    Q_EMIT noteExpunged(std::move(noteLocalId));
}

void Notifier::notifyTagPut(qevercloud::Tag tag)
{
    Q_EMIT tagPut(std::move(tag));
}

void Notifier::notifyTagExpunged(
    QString tagLocalId, QStringList expungedChildTagLocalIds)
{
    Q_EMIT tagExpunged(
        std::move(tagLocalId), std::move(expungedChildTagLocalIds));
}

void Notifier::notifyResourcePut(qevercloud::Resource resource)
{
    Q_EMIT resourcePut(std::move(resource));
}

void Notifier::notifyResourceExpunged(QString resourceLocalId)
{
    Q_EMIT resourceExpunged(std::move(resourceLocalId));
}

void Notifier::notifySavedSearchPut(qevercloud::SavedSearch savedSearch)
{
    Q_EMIT savedSearchPut(std::move(savedSearch));
}

void Notifier::notifySavedSearchExpunged(QString savedSearchLocalId)
{
    Q_EMIT savedSearchExpunged(std::move(savedSearchLocalId));
}

} // namespace quentier::local_storage::sql
