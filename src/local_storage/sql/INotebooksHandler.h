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

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>

#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/TypeAliases.h>

#include <QFuture>
#include <QList>
#include <QSet>

#include <optional>

namespace quentier::local_storage::sql {

class INotebooksHandler
{
public:
    virtual ~INotebooksHandler() = default;

    [[nodiscard]] virtual QFuture<quint32> notebookCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putNotebook(
        qevercloud::Notebook notebook) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByLocalId(QString localId) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByName(
            QString name,
            std::optional<qevercloud::Guid> linkedNotebookGuid = {}) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findDefaultNotebook() const = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByLocalId(
        QString localId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByGuid(
        qevercloud::Guid guid) = 0;

    [[nodiscard]] virtual QFuture<void> expungeNotebookByName(
        QString name,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) = 0;

    using ListGuidsFilters = ILocalStorage::ListGuidsFilters;
    using ListNotebooksOptions = ILocalStorage::ListNotebooksOptions;
    using ListNotebooksOrder = ILocalStorage::ListNotebooksOrder;

    [[nodiscard]] virtual QFuture<QSet<qevercloud::Guid>> listNotebookGuids(
        ListGuidsFilters filters,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Notebook>> listNotebooks(
        ListNotebooksOptions options) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::SharedNotebook>>
        listSharedNotebooks(qevercloud::Guid notebookGuid = {}) const = 0;
};

} // namespace quentier::local_storage::sql