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

#include <QSqlDatabase>
#include <QStringList>

namespace quentier::local_storage::sql {

class ISqlDatabaseWrapper
{
public:
    virtual ~ISqlDatabaseWrapper() = default;

    [[nodiscard]] virtual bool isDriverAvailable(
        const QString & name) const = 0;

    [[nodiscard]] virtual QStringList drivers() const = 0;

    virtual void removeDatabase(const QString & connectionName) = 0;

    [[nodiscard]] virtual QSqlDatabase database(
        const QString & connectionName, bool open = true) = 0;

    [[nodiscard]] virtual QSqlDatabase addDatabase(
        const QString & type, const QString & connectionName) = 0;
};

} // namespace quentier::local_storage::sql
