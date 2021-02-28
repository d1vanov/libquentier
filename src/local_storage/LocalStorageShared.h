/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_SHARED_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_SHARED_H

#include <QSqlQuery>

namespace quentier {

#define DATABASE_CHECK_AND_SET_ERROR()                                         \
    if (!res) {                                                                \
        errorDescription.base() = errorPrefix.base();                          \
        errorDescription.details() = query.lastError().text();                 \
        QNERROR(                                                               \
            "local_storage",                                                   \
            errorDescription << ", last executed query: "                      \
                             << lastExecutedQuery(query));                     \
        return false;                                                          \
    }

QString lastExecutedQuery(const QSqlQuery & query);

QString sqlEscapeString(const QString & str);

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_SHARED_H
