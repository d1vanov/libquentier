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

#include "ISavedSearchesHandler.h"
#include "Transaction.h"

#include <quentier/threading/Fwd.h>

#include <memory>

namespace quentier::local_storage::sql {

class SavedSearchesHandler final :
    public ISavedSearchesHandler,
    public std::enable_shared_from_this<SavedSearchesHandler>
{
public:
    explicit SavedSearchesHandler(
        ConnectionPoolPtr connectionPool, threading::QThreadPoolPtr threadPool,
        Notifier * notifier, threading::QThreadPtr writerThread);

    [[nodiscard]] QFuture<quint32> savedSearchCount() const override;

    [[nodiscard]] QFuture<void> putSavedSearch(
        qevercloud::SavedSearch search) override;

    [[nodiscard]] QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByLocalId(QString localId) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByGuid(qevercloud::Guid guid) const override;

    [[nodiscard]] QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByName(QString name) const override;

    [[nodiscard]] QFuture<QList<qevercloud::SavedSearch>> listSavedSearches(
        ListSavedSearchesOptions options = {}) const override;

    [[nodiscard]] QFuture<QSet<qevercloud::Guid>> listSavedSearchGuids(
        ListGuidsFilters filters) const override;

    [[nodiscard]] QFuture<void> expungeSavedSearchByLocalId(
        QString localId) override;

    [[nodiscard]] QFuture<void> expungeSavedSearchByGuid(
        qevercloud::Guid guid) override;

private:
    [[nodiscard]] std::optional<quint32> savedSearchCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::SavedSearch> findSavedSearchImpl(
        const QString & columnName, const QString & columnValue,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeSavedSearchByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription,
        std::optional<Transaction> transaction = std::nullopt);

    [[nodiscard]] bool expungeSavedSearchByGuidImpl(
        const qevercloud::Guid & guid, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] QList<qevercloud::SavedSearch> listSavedSearchesImpl(
        const ListSavedSearchesOptions & options, QSqlDatabase & database,
        ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    threading::QThreadPoolPtr m_threadPool;
    Notifier * m_notifier;
    threading::QThreadPtr m_writerThread;
};

} // namespace quentier::local_storage::sql
