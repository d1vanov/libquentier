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

#include "Common.h"

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#include <optional>

class QDebug;
class QDir;
class QSqlDatabase;
class QString;
class QStringList;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

[[nodiscard]] bool putUser(
    const qevercloud::User & user, QSqlDatabase & database,
    ErrorString & errorDescription,
    TransactionOption transactionOption =
        TransactionOption::UseSeparateTransaction);

[[nodiscard]] bool putCommonUserData(
    const qevercloud::User & user, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putUserAttributes(
    const qevercloud::UserAttributes & userAttributes,
    const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putUserAttributesViewedPromotions(
    const QString & userId,
    const std::optional<QStringList> & viewedPromotions,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putUserAttributesRecentMailedAddresses(
    const QString & userId,
    const std::optional<QStringList> & recentMailedAddresses,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putAccounting(
    const qevercloud::Accounting & accounting, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putAccountLimits(
    const qevercloud::AccountLimits & accountLimits, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putBusinessUserInfo(
    const qevercloud::BusinessUserInfo & info, const QString & userId,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putNotebook(
    qevercloud::Notebook notebook, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool putCommonNotebookData(
    const qevercloud::Notebook & notebook, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool putNotebookRestrictions(
    const QString & localId,
    const qevercloud::NotebookRestrictions & notebookRestrictions,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putSharedNotebook(
    const qevercloud::SharedNotebook & sharedNotebook,
    int indexInNotebook, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool putTag(
    qevercloud::Tag tag, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool putLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] bool putSavedSearch(
    qevercloud::SavedSearch savedSearch, QSqlDatabase & database,
    ErrorString & errorDescription);

enum class PutResourceBinaryDataOption
{
    WithBinaryData,
    WithoutBinaryData
};

QDebug & operator<<(QDebug & dbg, PutResourceBinaryDataOption option);

[[nodiscard]] bool putResource(
    const QDir & localStorageDir, qevercloud::Resource & resource,
    int indexInNote, QSqlDatabase & database, ErrorString & errorDescription,
    PutResourceBinaryDataOption putResourceBinaryDataOption =
        PutResourceBinaryDataOption::WithBinaryData,
    TransactionOption transactionOption =
        TransactionOption::UseSeparateTransaction);

enum class PutResourceMetadataOption
{
    WithBinaryDataProperties,
    WithoutBinaryDataProperties
};

[[nodiscard]] bool putCommonResourceData(
    const qevercloud::Resource & resource, int indexInNote,
    PutResourceMetadataOption putResourceMetadataOption,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putResourceAttributes(
    const QString & localId, const qevercloud::ResourceAttributes & attributes,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putResourceAttributesAppDataKeysOnly(
    const QString & localId, const QSet<QString> & keysOnly,
    QSqlDatabase & database, ErrorString & errorDescription);

[[nodiscard]] bool putResourceAttributesAppDataFullMap(
    const QString & localId,
    const QMap<QString, QString> & fullMap,
    QSqlDatabase & database, ErrorString & errorDescription);

} // namespace quentier::local_storage::sql::utils
