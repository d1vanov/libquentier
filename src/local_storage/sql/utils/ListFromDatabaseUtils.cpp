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

#include "FillFromSqlRecordUtils.h"
#include "ListFromDatabaseUtils.h"

#include "../ErrorHandling.h"

#include <qevercloud/utility/ToRange.h>

#include <QSqlRecord>

namespace quentier::local_storage::sql::utils {

template <>
QString listObjectsGenericSqlQuery<qevercloud::Notebook>()
{
    return QStringLiteral(
        "SELECT * FROM Notebooks LEFT OUTER JOIN NotebookRestrictions "
        "ON Notebooks.localUid = NotebookRestrictions.localUid "
        "LEFT OUTER JOIN SharedNotebooks ON ((Notebooks.guid IS NOT NULL) "
        "AND (Notebooks.guid = SharedNotebooks.sharedNotebookNotebookGuid)) "
        "LEFT OUTER JOIN Users ON Notebooks.contactId = Users.id "
        "LEFT OUTER JOIN UserAttributes ON "
        "Notebooks.contactId = UserAttributes.id "
        "LEFT OUTER JOIN UserAttributesViewedPromotions ON "
        "Notebooks.contactId = UserAttributesViewedPromotions.id "
        "LEFT OUTER JOIN UserAttributesRecentMailedAddresses ON "
        "Notebooks.contactId = UserAttributesRecentMailedAddresses.id "
        "LEFT OUTER JOIN Accounting ON "
        "Notebooks.contactId = Accounting.id "
        "LEFT OUTER JOIN AccountLimits ON "
        "Notebooks.contactId = AccountLimits.id "
        "LEFT OUTER JOIN BusinessUserInfo ON "
        "Notebooks.contactId = BusinessUserInfo.id");
}

template <>
QString listObjectsGenericSqlQuery<qevercloud::SavedSearch>()
{
    return QStringLiteral("SELECT * FROM SavedSearches");
}

template <>
QString listObjectsGenericSqlQuery<qevercloud::Tag>()
{
    return QStringLiteral(
        "SELECT * FROM Tags LEFT OUTER JOIN NoteTags "
        "ON Tags.localUid = NoteTags.localTag");
}

template <>
QString listObjectsGenericSqlQuery<qevercloud::LinkedNotebook>()
{
    return QStringLiteral("SELECT * FROM LinkedNotebooks");
}

template <>
QString orderByToSqlTableColumn<ILocalStorage::ListNotebooksOrder>(
    const ILocalStorage::ListNotebooksOrder & order)
{
    QString result;

    using ListNotebooksOrder = ILocalStorage::ListNotebooksOrder;
    switch (order) {
    case ListNotebooksOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListNotebooksOrder::ByNotebookName:
        result = QStringLiteral("notebookNameUpper");
        break;
    case ListNotebooksOrder::ByCreationTimestamp:
        result = QStringLiteral("creationTimestamp");
        break;
    case ListNotebooksOrder::ByModificationTimestamp:
        result = QStringLiteral("modificationTimestamp");
        break;
    default:
        break;
    }

    return result;
}

template <>
QString orderByToSqlTableColumn<ILocalStorage::ListSavedSearchesOrder>(
    const ILocalStorage::ListSavedSearchesOrder & order)
{
    QString result;

    using ListSavedSearchesOrder = ILocalStorage::ListSavedSearchesOrder;
    switch (order) {
    case ListSavedSearchesOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListSavedSearchesOrder::ByName:
        result = QStringLiteral("nameLower");
        break;
    case ListSavedSearchesOrder::ByFormat:
        result = QStringLiteral("format");
        break;
    default:
        break;
    }

    return result;
}

template <>
QString orderByToSqlTableColumn<ILocalStorage::ListTagsOrder>(
    const ILocalStorage::ListTagsOrder & order)
{
    QString result;

    using ListTagsOrder = ILocalStorage::ListTagsOrder;
    switch (order) {
    case ListTagsOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListTagsOrder::ByName:
        result = QStringLiteral("nameLower");
        break;
    default:
        break;
    }

    return result;
}

template <>
QString orderByToSqlTableColumn<ILocalStorage::ListLinkedNotebooksOrder>(
    const ILocalStorage::ListLinkedNotebooksOrder & order)
{
    QString result;

    using ListLinkedNotebooksOrder = ILocalStorage::ListLinkedNotebooksOrder;
    switch (order) {
    case ListLinkedNotebooksOrder::ByUpdateSequenceNumber:
        result = QStringLiteral("updateSequenceNumber");
        break;
    case ListLinkedNotebooksOrder::ByShareName:
        result = QStringLiteral("shareName");
        break;
    case ListLinkedNotebooksOrder::ByUsername:
        result = QStringLiteral("username");
        break;
    default:
        break;
    }

    return result;
}

QList<qevercloud::SharedNotebook> listSharedNotebooks(
    const qevercloud::Guid & notebookGuid, QSqlDatabase & database,
    ErrorString & errorDescription)
{
    QSqlQuery query{database};
    bool res = query.prepare(QStringLiteral(
        "SELECT * FROM SharedNotebooks "
        "WHERE sharedNotebookNotebookGuid = :sharedNotebookNotebookGuid"));

    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot list shared notebooks by notebook guid from the local "
            "storage database: failed to prepare query"),
        {});

    query.bindValue(
        QStringLiteral(":sharedNotebookNotebookGuid"), notebookGuid);

    res = query.exec();
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::utils",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::utils",
            "Cannot list shared notebooks by notebook guid from the local "
            "storage database"),
        {});

    QMap<int, qevercloud::SharedNotebook> sharedNotebooksByIndex;
    while (query.next()) {
        qevercloud::SharedNotebook sharedNotebook;
        int indexInNotebook = -1;
        ErrorString error;
        if (!utils::fillSharedNotebookFromSqlRecord(
                query.record(), sharedNotebook, indexInNotebook,
                errorDescription)) {
            return {};
        }

        sharedNotebooksByIndex[indexInNotebook] = sharedNotebook;
    }

    QList<qevercloud::SharedNotebook> sharedNotebooks;
    sharedNotebooks.reserve(qMax(sharedNotebooksByIndex.size(), 0));
    for (const auto it: qevercloud::toRange(sharedNotebooksByIndex)) {
        sharedNotebooks << it.value();
    }

    return sharedNotebooks;
}

} // namespace quentier::local_storage::sql::utils
