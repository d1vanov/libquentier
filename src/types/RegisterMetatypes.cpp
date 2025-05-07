/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/synchronization/types/ISyncChunksDataCounters.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/utility/IKeychainService.h>

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/User.h>

#include <QList>
#include <QMetaType>
#include <QNetworkCookie>
#include <QSqlError>
#include <QVector>

#include <cstddef>

namespace quentier {

void registerMetatypes()
{
    qRegisterMetaType<Account>("Account");

    qRegisterMetaType<QList<qevercloud::Notebook>>(
        "QList<qevercloud::Notebook>");
    qRegisterMetaType<QList<qevercloud::Note>>("QList<qevercloud::Note>");
    qRegisterMetaType<QList<qevercloud::Tag>>("QList<qevercloud::Tag>");
    qRegisterMetaType<QList<qevercloud::Resource>>(
        "QList<qevercloud::Resource>");
    qRegisterMetaType<QList<qevercloud::User>>("QList<qevercloud::User>");
    qRegisterMetaType<QList<qevercloud::LinkedNotebook>>(
        "QList<qevercloud::LinkedNotebook>");
    qRegisterMetaType<QList<qevercloud::SavedSearch>>(
        "QList<qevercloud::SavedSearch>");
    qRegisterMetaType<QList<qevercloud::SharedNotebook>>(
        "QList<qevercloud::SharedNotebook>");

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

    qRegisterMetaType<local_storage::NoteSearchQuery>("NoteSearchQuery");

    qRegisterMetaType<ErrorString>("ErrorString");
    qRegisterMetaType<QSqlError>("QSqlError");

    qRegisterMetaType<QList<std::pair<qevercloud::Tag, QStringList>>>(
        "QList<std::pair<qevercloud::Tag, QStringList> >");

    qRegisterMetaType<QHash<QString, std::pair<QString, QString>>>(
        "QHash<QString,std::pair<QString,QString> >");

    using ErrorCode = utility::IKeychainService::ErrorCode;
    qRegisterMetaType<ErrorCode>("ErrorCode");

    qRegisterMetaType<utility::IKeychainService::ErrorCode>(
        "utility::IKeychainService::ErrorCode");

    qRegisterMetaType<QList<QNetworkCookie>>("QList<QNeworkCookie>");

    qRegisterMetaType<synchronization::ISyncStatePtr>("ISyncStatePtr");

    qRegisterMetaType<std::size_t>("std::size_t");

    qRegisterMetaType<synchronization::ISyncChunksDataCountersPtr>(
        "ISyncChunksDataCountersPtr");

    qRegisterMetaType<local_storage::ILocalStorage::UpdateNoteOptions>(
        "ILocalStorage::UpdateNoteOptions");

    qRegisterMetaType<synchronization::ISendStatusPtr>("ISendStatusPtr");
}

} // namespace quentier
