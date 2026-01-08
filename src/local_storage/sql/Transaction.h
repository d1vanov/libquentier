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

#include <QSqlDatabase>

namespace quentier::local_storage::sql {

class Transaction
{
public:
    enum class Type
    {
        Default = 0,
        Selection,
        Immediate,
        Exclusive
    };

    explicit Transaction(
        const QSqlDatabase & database, Type type = Type::Default);

    Transaction(Transaction && transaction) noexcept;

    ~Transaction();

    [[nodiscard]] bool commit();
    [[nodiscard]] bool rollback();
    [[nodiscard]] bool end();

private:
    void init();

private:
    QSqlDatabase m_database;
    const Type m_type;

    bool m_committed = false;
    bool m_rolledBack = false;
    bool m_ended = false;
};

} // namespace quentier::local_storage::sql
