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

#include <qevercloud/types/Fwd.h>

#include <optional>

class QSqlRecord;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

[[nodiscard]] bool fillUserFromSqlRecord(
    const QSqlRecord & record, qevercloud::User & user,
    ErrorString & errorDescription);

void fillUserAttributesFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::UserAttributes> & userAttributes);

void fillAccountingFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::Accounting> & accounting);

void fillBusinessUserInfoFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::BusinessUserInfo> & businessUserInfo);

void fillAccountLimitsFromSqlRecord(
    const QSqlRecord & record,
    std::optional<qevercloud::AccountLimits> & accountLimits);

[[nodiscard]] bool fillNotebookFromSqlRecord(
    const QSqlRecord & record, qevercloud::Notebook & notebook,
    ErrorString & errorDescription);

} // namespace quentier::local_storage::sql::utils
