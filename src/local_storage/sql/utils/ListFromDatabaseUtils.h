/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include "SqlUtils.h"

#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/TypeAliases.h>

#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

class QDir;

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QList<qevercloud::SharedNotebook> listSharedNotebooks(
    const qevercloud::Guid & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription);

enum class ListNoteResourcesOption
{
    WithBinaryData,
    WithoutBinaryData
};

[[nodiscard]] QList<qevercloud::Resource> listNoteResources(
    const QString & noteLocalId, const QDir & localStorageDir,
    ListNoteResourcesOption option, QSqlDatabase & database,
    ErrorString & errorDescription);

////////////////////////////////////////////////////////////////////////////////

template <class T>
[[nodiscard]] QString listObjectsGenericSqlQuery();

template <>
[[nodiscard]] QString listObjectsGenericSqlQuery<qevercloud::Notebook>();

template <>
[[nodiscard]] QString listObjectsGenericSqlQuery<qevercloud::SavedSearch>();

template <>
[[nodiscard]] QString listObjectsGenericSqlQuery<qevercloud::Tag>();

template <>
[[nodiscard]] QString listObjectsGenericSqlQuery<qevercloud::LinkedNotebook>();

////////////////////////////////////////////////////////////////////////////////

template <class T>
[[nodiscard]] QString listGuidsGenericSqlQuery();

template <>
[[nodiscard]] QString listGuidsGenericSqlQuery<qevercloud::Notebook>();

template <>
[[nodiscard]] QString listGuidsGenericSqlQuery<qevercloud::Note>();

template <>
[[nodiscard]] QString listGuidsGenericSqlQuery<qevercloud::SavedSearch>();

template <>
[[nodiscard]] QString listGuidsGenericSqlQuery<qevercloud::Tag>();

template <>
[[nodiscard]] QString listGuidsGenericSqlQuery<qevercloud::LinkedNotebook>();

////////////////////////////////////////////////////////////////////////////////

template <class TOrderBy>
[[nodiscard]] QString orderByToSqlTableColumn(const TOrderBy & orderBy);

template <>
[[nodiscard]] QString
    orderByToSqlTableColumn<ILocalStorage::ListNotebooksOrder>(
        const ILocalStorage::ListNotebooksOrder & order);

template <>
[[nodiscard]] QString
    orderByToSqlTableColumn<ILocalStorage::ListSavedSearchesOrder>(
        const ILocalStorage::ListSavedSearchesOrder & order);

template <>
[[nodiscard]] QString orderByToSqlTableColumn<ILocalStorage::ListTagsOrder>(
    const ILocalStorage::ListTagsOrder & order);

template <>
[[nodiscard]] QString
    orderByToSqlTableColumn<ILocalStorage::ListLinkedNotebooksOrder>(
        const ILocalStorage::ListLinkedNotebooksOrder & order);

////////////////////////////////////////////////////////////////////////////////

template <class T>
[[nodiscard]] QString listObjectsFiltersToSqlQueryConditions(
    const ILocalStorage::ListObjectsFilters & filters)
{
    QString result;

    using ListObjectsFilter = ILocalStorage::ListObjectsFilter;

    if (filters.m_locallyModifiedFilter) {
        switch (*filters.m_locallyModifiedFilter) {
        case ListObjectsFilter::Include:
            result += QStringLiteral("(isDirty=1) AND ");
            break;
        case ListObjectsFilter::Exclude:
            result += QStringLiteral("(isDirty=0) AND ");
            break;
        }
    }

    if (filters.m_withGuidFilter) {
        switch (*filters.m_withGuidFilter) {
        case ListObjectsFilter::Include:
            result += QStringLiteral("(guid IS NULL) AND ");
            break;
        case ListObjectsFilter::Exclude:
            result += QStringLiteral("(guid IS NOT NULL) AND ");
            break;
        }
    }

    if (filters.m_localOnlyFilter) {
        switch (*filters.m_localOnlyFilter) {
        case ListObjectsFilter::Include:
            result += QStringLiteral("(isLocal=1) AND ");
            break;
        case ListObjectsFilter::Exclude:
            result += QStringLiteral("(isLocal=0) AND ");
            break;
        }
    }

    if (filters.m_locallyFavoritedFilter) {
        switch (*filters.m_locallyFavoritedFilter) {
        case ListObjectsFilter::Include:
            result += QStringLiteral("(isFavorited=1) AND ");
            break;
        case ListObjectsFilter::Exclude:
            result += QStringLiteral("(isFavorited=0) AND ");
            break;
        }
    }

    return result;
}

template <class T, class TOrderBy>
[[nodiscard]] QList<T> listObjects(
    const ILocalStorage::ListObjectsFilters & filters, quint64 limit,
    quint64 offset, const TOrderBy & orderBy,
    const ILocalStorage::OrderDirection & orderDirection,
    const QString & additionalSqlQueryCondition, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "local_storage::sql::utils",
        "Listing " << T::staticMetaObject.className()
                   << " objects: filters = " << filters << ", limit = " << limit
                   << ", offset = " << offset << ", order by " << orderBy
                   << ", order direction = " << orderDirection
                   << ", additional SQL query condition = "
                   << additionalSqlQueryCondition);

    QString sqlQueryConditions =
        listObjectsFiltersToSqlQueryConditions<T>(filters);

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
        "Listing " << T::staticMetaObject.className()
                   << " objects with SQL "
                      "query: "
                   << queryString);

    QList<T> objects;

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "can't list objects from the local storage database")};

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

////////////////////////////////////////////////////////////////////////////////

[[nodiscard]] QString listGuidsFiltersToSqlQueryConditions(
    const ILocalStorage::ListGuidsFilters & filters);

template <class T>
std::optional<QSet<qevercloud::Guid>> listGuids(
    const ILocalStorage::ListGuidsFilters & filters,
    const std::optional<qevercloud::Guid> & linkedNotebookGuid,
    QSqlDatabase & database, ErrorString & errorDescription)
{
    const QString queryString = [&] {
        QString queryString;
        QTextStream strm{&queryString};

        strm << listGuidsGenericSqlQuery<T>();

        const QString sqlQueryConditions =
            listGuidsFiltersToSqlQueryConditions(filters);

        if constexpr (std::is_same_v<std::decay_t<T>, qevercloud::SavedSearch>)
        {
            if (!sqlQueryConditions.isEmpty()) {
                strm << " WHERE ";
                strm << sqlQueryConditions;
            }
        }
        else if (!sqlQueryConditions.isEmpty() || linkedNotebookGuid) {
            strm << " WHERE ";

            if (!sqlQueryConditions.isEmpty()) {
                strm << sqlQueryConditions;
            }

            if (linkedNotebookGuid) {
                if (!sqlQueryConditions.isEmpty()) {
                    strm << " AND ";
                }

                strm << "(linkedNotebookGuid ";
                if (!linkedNotebookGuid->isEmpty()) {
                    strm << "= '";
                    strm << sqlEscape(*linkedNotebookGuid);
                    strm << "')";
                }
                else {
                    strm << "IS NULL)";
                }
            }
        }

        return queryString;
    }();

    const ErrorString errorPrefix{QT_TRANSLATE_NOOP(
        "local_storage::sql::utils",
        "can't list guids from the local storage database")};

    QSqlQuery query{database};
    if (!query.exec(queryString)) {
        errorDescription.base() = errorPrefix.base();
        QNWARNING(
            "local_storage::sql::utils",
            errorDescription << ", last query = " << query.lastQuery()
                             << ", last error = " << query.lastError());
        errorDescription.details() = query.lastError().text();
        return std::nullopt;
    }

    QSet<qevercloud::Guid> guids;
    while (query.next()) {
        guids.insert(query.value(0).toString());
    }

    return guids;
}

} // namespace quentier::local_storage::sql::utils
