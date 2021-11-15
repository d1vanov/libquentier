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

#include "SynchronizationInfoHandler.h"
#include "ConnectionPool.h"
#include "ErrorHandling.h"
#include "Tasks.h"
#include "utils/SqlUtils.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>

#include <utility/Threading.h>

#include <QException>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <utility/Qt5Promise.h>
#endif

#include <QSqlQuery>
#include <QSqlRecord>
#include <QThreadPool>

namespace quentier::local_storage::sql {

namespace {

struct HighUsnRequestData
{
    HighUsnRequestData() = default;

    HighUsnRequestData(
        QString tableName, QString usnColumnName, QString queryCondition) :
        m_tableName{std::move(tableName)},
        m_usnColumnName{std::move(usnColumnName)}, m_queryCondition{std::move(
                                                       queryCondition)}
    {}

    QString m_tableName;
    QString m_usnColumnName;
    QString m_queryCondition;
};

} // namespace

SynchronizationInfoHandler::SynchronizationInfoHandler(
    ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
    QThreadPtr writerThread) :
    m_connectionPool{std::move(connectionPool)},
    m_threadPool{threadPool}, m_writerThread{std::move(writerThread)}
{
    if (Q_UNLIKELY(!m_connectionPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::SynchronizationInfoHandler",
            "SynchronizationInfoHandler ctor: connection pool is null")}};
    }

    if (Q_UNLIKELY(!m_threadPool)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::SynchronizationInfoHandler",
            "SynchronizationInfoHandler ctor: thread pool is null")}};
    }

    if (Q_UNLIKELY(!m_writerThread)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "local_storage::sql::SynchronizationInfoHandler",
            "SynchronizationInfoHandler ctor: writer thread is null")}};
    }
}

QFuture<qint32> SynchronizationInfoHandler::highestUpdateSequenceNumber(
    HighestUsnOption option) const
{
    return makeReadTask<qint32>(
        makeTaskContext(), weak_from_this(),
        [option](
            const SynchronizationInfoHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) {
            return handler.highestUpdateSequenceNumberImpl(
                option, database, errorDescription);
        });
}

QFuture<qint32> SynchronizationInfoHandler::highestUpdateSequenceNumber(
    qevercloud::Guid linkedNotebookGuid) const
{
    return makeReadTask<qint32>(
        makeTaskContext(), weak_from_this(),
        [linkedNotebookGuid = std::move(linkedNotebookGuid)](
            const SynchronizationInfoHandler & handler, QSqlDatabase & database,
            ErrorString & errorDescription) mutable {
            return handler.highestUpdateSequenceNumberImpl(
                std::move(linkedNotebookGuid), database, errorDescription);
        });
}

std::optional<qint32>
    SynchronizationInfoHandler::highestUpdateSequenceNumberImpl(
        UsnVariant usnVariant, QSqlDatabase & database,
        ErrorString & errorDescription) const
{
    const bool isHighestUsnOption =
        std::holds_alternative<HighestUsnOption>(usnVariant);

    if (!isHighestUsnOption) {
        const auto & linkedNotebookGuid =
            std::get<qevercloud::Guid>(usnVariant);
        if (linkedNotebookGuid.isEmpty()) {
            return std::nullopt;
        }
    }

    QList<HighUsnRequestData> tablesAndUsnColumns;

    if (isHighestUsnOption) {
        tablesAndUsnColumns.reserve(6);
    }
    else {
        tablesAndUsnColumns.reserve(4);
    }

    QString queryCondition = QStringLiteral("WHERE linkedNotebookGuid");

    if (isHighestUsnOption) {
        queryCondition += QStringLiteral(" IS NULL");
    }
    else {
        const auto & linkedNotebookGuid =
            std::get<qevercloud::Guid>(usnVariant);

        queryCondition += QString::fromUtf8("='%1'").arg(
            utils::sqlEscape(linkedNotebookGuid));
    }

    const QString usn = QStringLiteral("updateSequenceNumber");

    tablesAndUsnColumns << HighUsnRequestData{
        QStringLiteral("Notebooks"), usn, queryCondition};

    tablesAndUsnColumns << HighUsnRequestData{
        QStringLiteral("Tags"), usn, queryCondition};

    // Separate query condition is required for notes table
    queryCondition = QStringLiteral(
        "WHERE notebookLocalUid IN (SELECT DISTINCT localUid "
        "FROM Notebooks WHERE linkedNotebookGuid");

    if (isHighestUsnOption) {
        queryCondition += QStringLiteral(" IS NULL)");
    }
    else {
        const auto & linkedNotebookGuid =
            std::get<qevercloud::Guid>(usnVariant);

        queryCondition += QString::fromUtf8("='%1')").arg(
            utils::sqlEscape(linkedNotebookGuid));
    }

    tablesAndUsnColumns << HighUsnRequestData{
        QStringLiteral("Notes"), usn, queryCondition};

    // Separate query condition is required for resources table
    queryCondition = QStringLiteral(
        "WHERE noteLocalUid IN (SELECT DISTINCT localUid FROM "
        "Notes WHERE notebookLocalUid IN "
        "(SELECT DISTINCT localUid FROM Notebooks "
        "WHERE linkedNotebookGuid");

    if (isHighestUsnOption) {
        queryCondition += QStringLiteral(" IS NULL))");
    }
    else {
        const auto & linkedNotebookGuid =
            std::get<qevercloud::Guid>(usnVariant);

        queryCondition += QString::fromUtf8("='%1'))").arg(
            utils::sqlEscape(linkedNotebookGuid));
    }

    tablesAndUsnColumns << HighUsnRequestData{
        QStringLiteral("Resource"),
        QStringLiteral("resourceUpdateSequenceNumber"), queryCondition};

    /**
     * No query condition is required for linked notebooks and saved searches
     * tables + only need to consider them for highest USN from user's own
     * account, not from some linked notebook
     */
    if (isHighestUsnOption) {
        tablesAndUsnColumns << HighUsnRequestData{
            QStringLiteral("LinkedNotebooks"), usn, QString{}};

        tablesAndUsnColumns << HighUsnRequestData{
            QStringLiteral("SavedSearches"), usn, QString{}};
    }

    qint32 updateSequenceNumber = 0;
    for (const auto & requestData: tablesAndUsnColumns) {
        auto usn = updateSequenceNumberFromTable(
            requestData.m_tableName, requestData.m_usnColumnName,
            requestData.m_queryCondition, database, errorDescription);
        if (!usn) {
            return std::nullopt;
        }

        updateSequenceNumber = std::max(updateSequenceNumber, *usn);

        QNTRACE(
            "local_storage::sql::SynchronizationInfoHandler",
            "Max update sequence number from table "
                << requestData.m_tableName << ": " << *usn
                << ", overall max USN so far: " << updateSequenceNumber);
    }

    QNDEBUG(
        "local_storage::sql::SynchronizationInfoHandler",
        "Max USN = " << updateSequenceNumber);

    return updateSequenceNumber;
}

std::optional<qint32> SynchronizationInfoHandler::updateSequenceNumberFromTable(
    const QString & tableName, const QString & usnColumnName,
    const QString & queryCondition, QSqlDatabase & database,
    ErrorString & errorDescription) const
{
    QNDEBUG(
        "local_storage::sql::SynchronizationInfoHandler",
        "SynchronizationInfoHandler::updateSequenceNumberFromTable: "
            << tableName << ", usn column name = " << usnColumnName
            << ", query condition = " << queryCondition);

    const ErrorString errorPrefix{
        QT_TR_NOOP("failed to get the update sequence number "
                   "from one of local storage database tables")};

    QString queryString = QStringLiteral("SELECT MAX(") + usnColumnName +
        QStringLiteral(") FROM ") + tableName;

    if (!queryCondition.isEmpty()) {
        queryString += QStringLiteral(" ") + queryCondition;
    }

    QSqlQuery query{database};
    const bool res = query.exec(queryString);
    ENSURE_DB_REQUEST_RETURN(
        res, query, "local_storage::sql::SynchronizationInfoHandler",
        QT_TRANSLATE_NOOP(
            "local_storage::sql::SynchronizationInfoHandler",
            "Failed to get the update sequence number from one of local "
            "storage database tables"),
        std::nullopt);

    if (!query.next()) {
        QNDEBUG(
            "local_storage::sql::SynchronizationInfoHandler",
            "No query result for table " << tableName);
        // NOTE: consider this the acceptable result, the table might be empty
        return 0;
    }

    bool conversionResult = false;
    const QVariant value = query.value(0);
    qint32 usn = value.toInt(&conversionResult);
    if (Q_UNLIKELY(!conversionResult)) {
        QNDEBUG(
            "local_storage::sql::SynchronizationInfoHandler",
            "Failed to convert the query result to int");
        /**
         * NOTE: surprisingly, this also seems to happen when the table on which
         * the query runs is empty, so need to handle it gently: don't return
         * error, return zero instead
         */
        usn = 0;
    }

    return usn;
}

} // namespace quentier::local_storage::sql
