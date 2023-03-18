/*
 * Copyright 2020-2021 Dmitry Ivanov
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

#include <quentier/local_storage/ILocalStorage.h>

#include <QObject>

namespace quentier::local_storage {

class QUENTIER_EXPORT ILocalStorageNotifier : public QObject
{
    Q_OBJECT
protected:
    explicit ILocalStorageNotifier(QObject * parent = nullptr);

public:
    ~ILocalStorageNotifier() override;

Q_SIGNALS:
    // Notifications about user related events
    void userPut(qevercloud::User user);
    void userExpunged(qevercloud::UserID userId);

    // Notifications about notebook related events
    void notebookPut(qevercloud::Notebook notebook);
    void notebookExpunged(QString notebookLocalId);

    // Notifications about linked notebooks
    void linkedNotebookPut(qevercloud::LinkedNotebook linkedNotebook);
    void linkedNotebookExpunged(qevercloud::Guid linkedNotebookGuid);

    // Notifications about note related events
    void notePut(qevercloud::Note note);

    void noteUpdated(
        qevercloud::Note note, ILocalStorage::UpdateNoteOptions options);

    void noteNotebookChanged(
        QString noteLocalId, QString previousNotebookLocalId,
        QString newNotebookLocalId);

    void noteTagListChanged(
        QString noteLocalId, QStringList previousNoteTagLocalIds,
        QStringList newNoteTagLocalIds);

    void noteExpunged(QString noteLocalId);

    // Notifications about tag related events
    void tagPut(qevercloud::Tag tag);

    void tagExpunged(
        QString tagLocalId, QStringList expungedChildTagLocalIds);

    // Notifications about resource related events
    void resourcePut(qevercloud::Resource resource);
    void resourceMetadataPut(qevercloud::Resource resource);
    void resourceExpunged(QString resourceLocalId);

    // Notifications about saved search related events
    void savedSearchPut(qevercloud::SavedSearch savedSearch);
    void savedSearchExpunged(QString savedSearchLocalId);
};

} // namespace quentier::local_storage
