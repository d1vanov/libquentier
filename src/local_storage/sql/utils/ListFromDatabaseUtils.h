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

#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/types/ErrorString.h>

#include <QSqlDatabase>
#include <QSqlQuery>

namespace quentier::local_storage::sql::utils {

[[nodiscard]] QList<qevercloud::SharedNotebook> listSharedNotebooks(
    const qevercloud::Guid & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription);

template <class T>
[[nodiscard]] QString listObjectsGenericSqlQuery();

template <class TOrderBy>
[[nodiscard]] QString orderByToSqlTableColumn(const TOrderBy & orderBy);

template <class T>
[[nodiscard]] QString listObjectsOptionsToSqlQueryConditions(
    const ILocalStorage::ListObjectsOptions & options,
    QSqlDatabase & database, ErrorString & errorDescription)
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


} // namespace quentier::local_storage::sql::utils
