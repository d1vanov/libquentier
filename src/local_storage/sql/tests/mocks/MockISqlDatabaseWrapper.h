/*
 * Copyright 2025 Dmitry Ivanov
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

#include <local_storage/sql/ISqlDatabaseWrapper.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockISqlDatabaseWrapper : public ISqlDatabaseWrapper
{
public:
    MOCK_METHOD(
        bool, isDriverAvailable, (const QString & name), (const, override));

    MOCK_METHOD(QStringList, drivers, (), (const, override));

    MOCK_METHOD(
        void, removeDatabase, (const QString & connectionName), (override));

    MOCK_METHOD(
        QSqlDatabase, database, (const QString & connectionName, bool open),
        (override));

    MOCK_METHOD(
        QSqlDatabase, addDatabase,
        (const QString & type, const QString & connectionName), (override));
};

} // namespace quentier::local_storage::sql::tests::mocks
