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

#include <quentier/local_storage/ILocalStorage.h>

class QSqlDatabase;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QString noteGuidByLocalId(
    const QString & noteLocalId, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] QString notebookLocalId(
    const qevercloud::Note & note, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] QString notebookGuid(
    const qevercloud::Note & note, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] QString noteLocalIdByGuid(
    const qevercloud::Guid & noteGuid, QSqlDatabase & database,
    ErrorString & errorDescription);

[[nodiscard]] QStringList queryNoteLocalIds(
    const NoteSearchQuery & noteSearchQuery, QSqlDatabase & database,
    ErrorString & errorDescription,
    TransactionOption transactionOption =
        TransactionOption::UseSeparateTransaction);

} // namespace quentier::local_storage::sql::utils
