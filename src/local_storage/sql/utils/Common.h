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

#include "../Transaction.h"

#include <QSet>

#include <algorithm>

class QDebug;

namespace quentier::local_storage::sql::utils {

enum class TransactionOption
{
    UseSeparateTransaction,
    DontUseSeparateTransaction
};

QDebug & operator<<(QDebug & dbg, TransactionOption transactionOption);

struct SelectTransactionGuard
{
    SelectTransactionGuard(const QSqlDatabase & database) :
        m_transaction(database, Transaction::Type::Selection)
    {}

    ~SelectTransactionGuard()
    {
        Q_UNUSED(m_transaction.end())
    }

    Transaction m_transaction;
};

template <class T>
[[nodiscard]] bool checkDuplicatesByLocalId(const QList<T> & lhs)
{
    return !std::any_of(
        lhs.begin(), lhs.end(),
        [s = QSet<QString>{}] (const auto & item) mutable
        {
            const auto & localId = item.localId();
            if (s.contains(localId)) {
                return true;
            }

            Q_UNUSED(s.insert(localId))
            return false;
        });
}

} // namespace quentier::local_storage::sql::utils
