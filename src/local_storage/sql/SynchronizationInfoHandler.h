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

#include "ISynchronizationInfoHandler.h"

#include <quentier/threading/Fwd.h>

#include <QtGlobal>

#include <memory>
#include <optional>
#include <variant>

class QSqlDatabase;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class SynchronizationInfoHandler final :
    public ISynchronizationInfoHandler,
    public std::enable_shared_from_this<SynchronizationInfoHandler>
{
public:
    explicit SynchronizationInfoHandler(
        ConnectionPoolPtr connectionPool, threading::QThreadPoolPtr threadPool,
        threading::QThreadPtr writerThread);

    [[nodiscard]] QFuture<qint32> highestUpdateSequenceNumber(
        HighestUsnOption option) const override;

    [[nodiscard]] QFuture<qint32> highestUpdateSequenceNumber(
        qevercloud::Guid linkedNotebookGuid) const override;

private:
    using UsnVariant = std::variant<HighestUsnOption, qevercloud::Guid>;

    [[nodiscard]] std::optional<qint32> highestUpdateSequenceNumberImpl(
        UsnVariant usnVariant, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qint32> updateSequenceNumberFromTable(
        const QString & tableName, const QString & usnColumnName,
        const QString & queryCondition, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    threading::QThreadPoolPtr m_threadPool;
    threading::QThreadPtr m_writerThread;
};

} // namespace quentier::local_storage::sql
