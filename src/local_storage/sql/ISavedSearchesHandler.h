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

#include <QFuture>
#include <QList>

#include <optional>

namespace quentier::local_storage::sql {

class ISavedSearchesHandler
{
public:
    virtual ~ISavedSearchesHandler() = default;

    [[nodiscard]] virtual QFuture<quint32> savedSearchCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putSavedSearch(
        qevercloud::SavedSearch search) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByLocalId(QString localId) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::SavedSearch>>
        findSavedSearchByName(QString name) const = 0;

    using ListSavedSearchesOptions = ILocalStorage::ListSavedSearchesOptions;
    using ListSavedSearchesOrder = ILocalStorage::ListSavedSearchesOrder;

    [[nodiscard]] virtual QFuture<QList<qevercloud::SavedSearch>>
        listSavedSearches(ListSavedSearchesOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeSavedSearchByLocalId(
        QString localId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeSavedSearchByGuid(
        qevercloud::Guid guid) = 0;
};

} // namespace quentier::local_storage::sql
