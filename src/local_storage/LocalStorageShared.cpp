/*
 * Copyright 2018-2019 Dmitry Ivanov
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

#include "LocalStorageShared.h"

#include <QMap>
#include <QVariant>

namespace quentier {

QString lastExecutedQuery(const QSqlQuery & query)
{
    QString str = query.lastQuery();
    QMap<QString, QVariant> boundValues = query.boundValues();

    for (auto it = boundValues.constBegin(), end = boundValues.constEnd();
         it != end; ++it)
    {
        str.replace(it.key(), it.value().toString());
    }

    return str;
}

QString sqlEscapeString(const QString & str)
{
    QString res = str;
    res.replace(QStringLiteral("\'"), QStringLiteral("\'\'"));
    return res;
}

} // namespace quentier
