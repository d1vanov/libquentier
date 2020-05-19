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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_TRANSACTION_H
#define LIB_QUENTIER_LOCAL_STORAGE_TRANSACTION_H

#include <quentier/types/ErrorString.h>

#include <QSqlDatabase>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerPrivate)

class Q_DECL_HIDDEN Transaction
{
public:
    enum class Type
    {
        Default = 0,
        // transaction type for speeding-up selection queries via holding
        // the shared lock
        Selection,
        Immediate,
        Exclusive
    };

    Transaction(
        const QSqlDatabase & db,
        const LocalStorageManagerPrivate & localStorageManager,
        Type type = Type::Default);

    virtual ~Transaction();

    bool commit(ErrorString & errorDescription);
    bool rollback(ErrorString & errorDescription);
    bool end(ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(Transaction)

    void init();

    const QSqlDatabase & m_db;
    const LocalStorageManagerPrivate & m_localStorageManager;

private:
    Type m_type;
    bool m_committed;
    bool m_rolledBack;
    bool m_ended;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_TRANSACTION_H
