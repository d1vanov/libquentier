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

#include "FillFromSqlRecordUtils.h"

#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QList<qevercloud::SharedNotebook> listSharedNotebooks(
    const qevercloud::Guid & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription);

template <class T>
[[nodiscard]] QString listObjectsGenericSqlQuery();

template <>
[[nodiscard]] QString listObjectsGenericSqlQuery<qevercloud::Notebook>();

template <>
[[nodiscard]] QString listObjectsGenericSqlQuery<qevercloud::Tag>();

template <>
[[nodiscard]] QString listObjectsGenericSqlQuery<qevercloud::LinkedNotebook>();

template <class TOrderBy>
[[nodiscard]] QString orderByToSqlTableColumn(const TOrderBy & orderBy);

template <>
[[nodiscard]] QString
    orderByToSqlTableColumn<ILocalStorage::ListNotebooksOrder>(
        const ILocalStorage::ListNotebooksOrder & order);

template <>
[[nodiscard]] QString orderByToSqlTableColumn<ILocalStorage::ListTagsOrder>(
    const ILocalStorage::ListTagsOrder & order);

template <>
[[nodiscard]] QString
    orderByToSqlTableColumn<ILocalStorage::ListLinkedNotebooksOrder>(
        const ILocalStorage::ListLinkedNotebooksOrder & order);

template <class T>
[[nodiscard]] QString listObjectsOptionsToSqlQueryConditions(
    const ILocalStorage::ListObjectsOptions & options,
    ErrorString & errorDescription)
{
    QString result;
    errorDescription.clear();

    using ListObjectsOption = ILocalStorage::ListObjectsOption;

    bool listAll = options.testFlag(ListObjectsOption::ListAll);

    bool listDirty = options.testFlag(ListObjectsOption::ListDirty);
    bool listNonDirty = options.testFlag(ListObjectsOption::ListNonDirty);

    bool listElementsWithoutGuid =
        options.testFlag(ListObjectsOption::ListElementsWithoutGuid);

    bool listElementsWithGuid =
        options.testFlag(ListObjectsOption::ListElementsWithGuid);

    bool listLocal = options.testFlag(ListObjectsOption::ListLocal);
    bool listNonLocal = options.testFlag(ListObjectsOption::ListNonLocal);

    bool listFavoritedElements =
        options.testFlag(ListObjectsOption::ListFavoritedElements);

    bool listNonFavoritedElements =
        options.testFlag(ListObjectsOption::ListNonFavoritedElements);

    if (!listAll && !listDirty && !listNonDirty && !listElementsWithoutGuid &&
        !listElementsWithGuid && !listLocal && !listNonLocal &&
        !listFavoritedElements && !listNonFavoritedElements)
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Can't list objects by filter: detected incorrect filter flag"));
        errorDescription.details() = QString::number(static_cast<int>(options));
        return result;
    }

    if (!(listDirty && listNonDirty)) {
        if (listDirty) {
            result += QStringLiteral("(isDirty=1) AND ");
        }

        if (listNonDirty) {
            result += QStringLiteral("(isDirty=0) AND ");
        }
    }

    if (!(listElementsWithoutGuid && listElementsWithGuid)) {
        if (listElementsWithoutGuid) {
            result += QStringLiteral("(guid IS NULL) AND ");
        }

        if (listElementsWithGuid) {
            result += QStringLiteral("(guid IS NOT NULL) AND ");
        }
    }

    if (!(listLocal && listNonLocal)) {
        if (listLocal) {
            result += QStringLiteral("(isLocal=1) AND ");
        }

        if (listNonLocal) {
            result += QStringLiteral("(isLocal=0) AND ");
        }
    }

    if (!(listFavoritedElements && listNonFavoritedElements)) {
        if (listFavoritedElements) {
            result += QStringLiteral("(isFavorited=1) AND ");
        }

        if (listNonFavoritedElements) {
            result += QStringLiteral("(isFavorited=0) AND ");
        }
    }

    return result;
}

template <class T, class TOrderBy>
QList<T> listObjects(
    const ILocalStorage::ListObjectsOptions & flag,
    quint64 limit, quint64 offset, const TOrderBy & orderBy,
    const ILocalStorage::OrderDirection & orderDirection,
    const QString & additionalSqlQueryCondition,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "Listing " << T::staticMetaObject.className() << " objects: flag = "
            << flag << ", limit = " << limit << ", offset = " << offset
            << ", order by " << orderBy <<  ", order direction = "
            << orderDirection << ", additional SQL query condition = "
            << additionalSqlQueryCondition);

    ErrorString flagError;

    QString sqlQueryConditions =
        listObjectsOptionsToSqlQueryConditions<T>(flag, flagError);

    if (sqlQueryConditions.isEmpty() && !flagError.isEmpty()) {
        errorDescription = flagError;
        QNWARNING("local_storage::sql::utils", flagError);
        return QList<T>();
    }

    QString sumSqlQueryConditions;
    if (!sqlQueryConditions.isEmpty()) {
        sumSqlQueryConditions += sqlQueryConditions;
    }

    if (!additionalSqlQueryCondition.isEmpty()) {
        if (!sumSqlQueryConditions.isEmpty() &&
            !sumSqlQueryConditions.endsWith(QStringLiteral(" AND ")))
        {
            sumSqlQueryConditions += QStringLiteral(" AND ");
        }

        sumSqlQueryConditions += additionalSqlQueryCondition;
    }

    if (sumSqlQueryConditions.endsWith(QStringLiteral(" AND "))) {
        sumSqlQueryConditions.chop(5);
    }

    QString queryString = listObjectsGenericSqlQuery<T>();
    if (!sumSqlQueryConditions.isEmpty()) {
        sumSqlQueryConditions.prepend(QStringLiteral("("));
        sumSqlQueryConditions.append(QStringLiteral(")"));
        queryString += QStringLiteral(" WHERE ");
        queryString += sumSqlQueryConditions;
    }

    QString orderByColumn = orderByToSqlTableColumn<TOrderBy>(orderBy);
    if (!orderByColumn.isEmpty()) {
        queryString += QStringLiteral(" ORDER BY ");
        queryString += orderByColumn;

        switch (orderDirection) {
        case ILocalStorage::OrderDirection::Descending:
            queryString += QStringLiteral(" DESC");
            break;
        case ILocalStorage::OrderDirection::Ascending:
            [[fallthrough]];
        default:
            queryString += QStringLiteral(" ASC");
            break;
        }
    }

    if (limit != 0) {
        queryString += QStringLiteral(" LIMIT ") + QString::number(limit);
    }

    if (offset != 0) {
        queryString += QStringLiteral(" OFFSET ") + QString::number(offset);
    }

    QNDEBUG(
        "local_storage::sql::utils",
        "Listing " << T::staticMetaObject.className() << " objects with SQL "
        "query: " << queryString);

    QList<T> objects;

    const ErrorString errorPrefix(QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "can't list objects from the local storage database by filter"));

    QSqlQuery query{database};
    if (!query.exec(queryString)) {
        errorDescription.base() = errorPrefix.base();
        QNWARNING(
            "local_storage::sql::utils",
            errorDescription << ", last query = " << query.lastQuery()
                             << ", last error = " << query.lastError());
        errorDescription.details() = query.lastError().text();
        return objects;
    }

    ErrorString error;
    if (!fillObjectsFromSqlQuery<T>(query, database, objects, error)) {
        errorDescription.base() = errorPrefix.base();
        errorDescription.appendBase(error.base());
        errorDescription.appendBase(error.additionalBases());
        errorDescription.details() = error.details();
        QNWARNING("local_storage::sql::utils", errorDescription);
        objects.clear();
        return objects;
    }

    QNDEBUG(
        "local_storage::sql::utils",
        "Found " << objects.size() << " " << T::staticMetaObject.className()
            << " objects");

    return objects;
}

} // namespace quentier::local_storage::sql::utils
