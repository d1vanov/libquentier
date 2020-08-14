/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "../src/synchronization/SynchronizationShared.h"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/types/Resource.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/User.h>
#include <quentier/utility/IKeychainService.h>

#include <QList>
#include <QMetaType>
#include <QNetworkCookie>
#include <QSqlError>
#include <QVector>

namespace quentier {

void registerMetatypes()
{
    qRegisterMetaType<Notebook>("Notebook");
    qRegisterMetaType<Note>("Note");
    qRegisterMetaType<Tag>("Tag");
    qRegisterMetaType<Resource>("Resource");
    qRegisterMetaType<User>("User");
    qRegisterMetaType<LinkedNotebook>("LinkedNotebook");
    qRegisterMetaType<SavedSearch>("SavedSearch");
    qRegisterMetaType<Account>("Account");

    qRegisterMetaType<qevercloud::UserID>("qevercloud::UserID");
    qRegisterMetaType<qevercloud::Timestamp>("qevercloud::Timestamp");
    qRegisterMetaType<qevercloud::Note>("qevercloud::Note");
    qRegisterMetaType<qevercloud::SavedSearch>("qevercloud::SavedSearch");
    qRegisterMetaType<qevercloud::Tag>("qevercloud::Tag");
    qRegisterMetaType<qevercloud::Notebook>("qevercloud::Notebook");
    qRegisterMetaType<qevercloud::Resource>("qevercloud::Resource");

    qRegisterMetaType<QVector<LinkedNotebookAuthData>>(
        "QVector<LinkedNotebookAuthData>");

    qRegisterMetaType<QList<Notebook>>("QList<Notebook>");
    qRegisterMetaType<QList<Note>>("QList<Note>");
    qRegisterMetaType<QList<Tag>>("QList<Tag>");
    qRegisterMetaType<QList<Resource>>("QList<Resource>");
    qRegisterMetaType<QList<User>>("QList<User>");
    qRegisterMetaType<QList<LinkedNotebook>>("QList<LinkedNotebook>");
    qRegisterMetaType<QList<SavedSearch>>("QList<SavedSearch>");

    qRegisterMetaType<QList<SharedNotebook>>("QList<SharedNotebook>");

    qRegisterMetaType<LocalStorageManager::ListObjectsOptions>(
        "LocalStorageManager::ListObjectsOptions");

    qRegisterMetaType<LocalStorageManager::ListNotesOrder>(
        "LocalStorageManager::ListNotesOrder");

    qRegisterMetaType<LocalStorageManager::ListNotebooksOrder>(
        "LocalStorageManager::ListNotebooksOrder");

    qRegisterMetaType<LocalStorageManager::ListLinkedNotebooksOrder>(
        "LocalStorageManager::ListLinkedNotebooksOrder");

    qRegisterMetaType<LocalStorageManager::ListTagsOrder>(
        "LocalStorageManager::ListTagsOrder");

    qRegisterMetaType<LocalStorageManager::ListSavedSearchesOrder>(
        "LocalStorageManager::ListSavedSearchesOrder");

    qRegisterMetaType<LocalStorageManager::OrderDirection>(
        "LocalStorageManager::OrderDirection");

    qRegisterMetaType<LocalStorageManager::UpdateNoteOptions>(
        "LocalStorageManager::UpdateNoteOptions");

    qRegisterMetaType<LocalStorageManager::GetNoteOptions>(
        "LocalStorageManager::GetNoteOptions");

    qRegisterMetaType<LocalStorageManager::GetResourceOptions>(
        "LocalStorageManager::GetResourceOptions");

    qRegisterMetaType<LocalStorageManager::StartupOptions>(
        "LocalStorageManager::StartupOptions");

    qRegisterMetaType<LocalStorageManager::NoteCountOptions>(
        "LocalStorageManager::NoteCountOptions");

    qRegisterMetaType<size_t>("size_t");
    qRegisterMetaType<QUuid>("QUuid");

    qRegisterMetaType<QList<QPair<QString, QString>>>(
        "QList<QPair<QString,QString> >");

    qRegisterMetaType<QHash<QString, QPair<QString, QString>>>(
        "QHash<QString, QPair<QString,QString> >");

    qRegisterMetaType<QHash<QString, QString>>("QHash<QString,QString>");

    qRegisterMetaType<QHash<QString, qevercloud::Timestamp>>(
        "QHash<QString,qevercloud::Timestamp>");

    qRegisterMetaType<QHash<QString, qint32>>("QHash<QString,qint32>");
    qRegisterMetaType<QHash<QString, int>>("QHash<QString,int>");

    qRegisterMetaType<NoteSearchQuery>("NoteSearchQuery");

    qRegisterMetaType<ErrorString>("ErrorString");
    qRegisterMetaType<QSqlError>("QSqlError");

    qRegisterMetaType<QList<std::pair<Tag, QStringList>>>(
        "QList<std::pair<Tag, QStringList> >");

    qRegisterMetaType<QHash<QString, std::pair<QString, QString>>>(
        "QHash<QString,std::pair<QString,QString> >");

    using ErrorCode = IKeychainService::ErrorCode;
    qRegisterMetaType<ErrorCode>("ErrorCode");

    qRegisterMetaType<IKeychainService::ErrorCode>(
        "IKeychainService::ErrorCode");

    qRegisterMetaType<QList<QNetworkCookie>>("QList<QNeworkCookie>");

    using ISyncStatePtr = ISyncStateStorage::ISyncStatePtr;
    qRegisterMetaType<ISyncStatePtr>("ISyncStatePtr");
}

} // namespace quentier
