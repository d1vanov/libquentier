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

#include "Fwd.h"

namespace quentier::local_storage::sql {

class TablesInitializer
{
public:
    static void initializeTables(QSqlDatabase & databaseConnection);

private:
    static void initializeAuxiliaryTable(QSqlDatabase & databaseConnection);
    static void initializeUserTables(QSqlDatabase & databaseConnection);
    static void initializeNotebookTables(QSqlDatabase & databaseConnection);
    static void initializeNoteTables(QSqlDatabase & databaseConnection);
    static void initializeResourceTables(QSqlDatabase & databaseConnection);
    static void initializeTagsTables(QSqlDatabase & databaseConnection);
    static void initializeSavedSearchTables(QSqlDatabase & databaseConnection);
    static void initializeExtraTriggers(QSqlDatabase & databaseConnection);
};

} // namespace quentier::local_storage::sql
