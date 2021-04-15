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

#include "TablesInitializer.h"

#include <quentier/exception/DatabaseOpeningException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <QtGlobal>

#include <stdexcept>

namespace quentier::local_storage::sql {

TablesInitializer::TablesInitializer(DatabaseInfo databaseInfo)
    : m_databaseInfo{std::move(databaseInfo)}
{
    if (Q_UNLIKELY(!m_databaseInfo.connectionPool)) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::TablesInitializer",
            "Cannot create TablesInitializer: connection pool is null"));

        QNWARNING("local_storage:sql:tables_initializer", error);
        throw DatabaseOpeningException{error};
    }

    if (Q_UNLIKELY(!m_databaseInfo.writerMutex)) {
        ErrorString error(QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::TablesInitializer",
            "Cannot create TablesInitializer: writer mutex is null"));

        QNWARNING("local_storage:sql:tables_initializer", error);
        throw DatabaseOpeningException{error};
    }
}

void TablesInitializer::initializeTables()
{
    // TODO: implement
}

} // namespace quentier::local_storage::sql
