/*
 * Copyright 2020 Dmitry Ivanov
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
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/User.h>
#include <quentier/utility/Linkage.h>

#include <QObject>

namespace quentier::local_storage {

class QUENTIER_EXPORT ILocalStorageNotifier : public QObject
{
    Q_OBJECT
public:
    virtual ~ILocalStorageNotifier() = default;

Q_SIGNALS:
    // Notifications about user related events
    void userAdded(User user);
    void userUpdated(User user);
    void userExpunged(qint32 userId);

    // Notifications about notebook related events
    void notebookAdded(Notebook notebook);
    void notebookUpdated(Notebook notebook);
    void notebookExpunged(QString notebookLocalUid);

    // Notifications about linked notebooks
    void linkedNotebookAdded(LinkedNotebook linkedNotebook);
    void linkedNotebookUpdated(LinkedNotebook linkedNotebook);
    void linkedNotebookExpunged(QString linkedNotebookGuid);

    // Notifications about note related events
    void noteAdded(Note note);
    void noteUpdated(Note note);

    void noteMovedToAnotherNotebook(
        QString noteLocalUid, QString previousNotebookLocalUid,
        QString newNotebookLocalUid);

    void noteTagListChanged(
        QString noteLocalUid, QStringList previousNoteTagLocalUids,
        QStringList newNoteTagLocalUids);

    void noteExpunged(QString noteLocalUid);

    // Notifications about tag related events
    void tagAdded(Tag tag);
    void tagUpdated(Tag tag);

    void tagExpunged(
        QString tagLocalUid, QStringList expungedChildTagLocalUids);

    // Notifications about resource related events
    void resourceAdded(Resource resource);
    void resourceUpdated(Resource resource);
    void resourceExpunged(QString resourceLocalUid);

    // Notifications about saved search related events
    void savedSearchAdded(SavedSearch savedSearch);
    void savedSearchUpdated(SavedSearch savedSearch);
    void savedSearchExpunged(QString savedSearchLocalUid);
};

} // namespace quentier::local_storage
