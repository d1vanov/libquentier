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

#include "SqlUtils.h"

#include "../ErrorHandling.h"

#include <quentier/types/ErrorString.h>

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTextStream>
#include <QVariant>

namespace quentier::local_storage::sql::utils {

QString sqlEscape(QString source)
{
    return source.replace(QStringLiteral("\'"), QStringLiteral("\'\'"));
}

bool rowExists(
    const QString & tableName, const QString & columnName,
    const QVariant & value, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QSqlQuery query{database};
    bool res = query.prepare(
        QString::fromUtf8("SELECT COUNT(*) FROM %1 WHERE %2 = :value")
            .arg(sqlEscape(tableName), sqlEscape(columnName)));
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot check row existence: failed to prepare query"),
        false);

    query.bindValue(QStringLiteral(":value"), value);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot check row existence"),
        false);

    if (!query.next()) {
        return false;
    }

    bool conversionResult = false;
    int count = query.value(0).toInt(&conversionResult);
    if (!conversionResult) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot check row existence: failed to convert result to int"));
        QNWARNING("local_storage::sql::utils", errorDescription);
        return false;
    }

    return count > 0;
}

QString toQuotedSqlList(const QStringList & items)
{
    QString result;
    QTextStream strm{&result};

    for (const QString & item: qAsConst(items)) {
        strm << "'" << sqlEscape(item) << "'";
        if (&item != items.constLast()) {
            strm << ", ";
        }
    }

    return result;
}

} // namespace quentier::local_storage::sql::utils
