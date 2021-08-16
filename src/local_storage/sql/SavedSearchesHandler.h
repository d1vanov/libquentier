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

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>

#include <qevercloud/types/SavedSearch.h>

#include <memory>
#include <optional>

namespace quentier::local_storage::sql {

class SavedSearchesHandler final :
    public std::enable_shared_from_this<SavedSearchesHandler>
{
public:
    explicit SavedSearchesHandler(
        ConnectionPoolPtr connectionPool, QThreadPool * threadPool,
        Notifier * notifier, QThreadPtr writerThread);

    [[nodiscard]] QFuture<quint32> savedSearchCount() const;

    [[nodiscard]] QFuture<void> putSavedSearch(qevercloud::SavedSearch search);

    [[nodiscard]] QFuture<qevercloud::SavedSearch> findSavedSearchByLocalId(
        QString localId) const;

    template <class T>
    using ListOptions = ILocalStorage::ListOptions<T>;

    using ListSavedSearchesOrder = ILocalStorage::ListSavedSearchesOrder;

    [[nodiscard]] QFuture<QList<qevercloud::SavedSearch>> listSavedSearches(
        ListOptions<ListSavedSearchesOrder> options = {}) const;

    [[nodiscard]] QFuture<void> expungeSavedSearchByLocalId(QString localId);

private:
    [[nodiscard]] std::optional<quint32> savedSearchCountImpl(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] std::optional<qevercloud::SavedSearch>
        findSavedSearchByLocalIdImpl(
            const QString & localId, QSqlDatabase & database,
            ErrorString & errorDescription) const;

    [[nodiscard]] bool expungeSavedSearchByLocalIdImpl(
        const QString & localId, QSqlDatabase & database,
        ErrorString & errorDescription);

    [[nodiscard]] QList<qevercloud::SavedSearch> listSavedSearchesImpl(
        const ListOptions<ListSavedSearchesOrder> & options,
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] TaskContext makeTaskContext() const;

private:
    ConnectionPoolPtr m_connectionPool;
    QThreadPool * m_threadPool;
    Notifier * m_notifier;
    QThreadPtr m_writerThread;
};

} // namespace quentier::local_storage::sql
