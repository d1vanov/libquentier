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

#include <qevercloud/types/LinkedNotebook.h>

#include <QFuture>
#include <QList>

namespace quentier::local_storage::sql {

class ILinkedNotebooksHandler
{
public:
    virtual ~ILinkedNotebooksHandler() = default;

    [[nodiscard]] virtual QFuture<quint32> linkedNotebookCount() const = 0;

    [[nodiscard]] virtual QFuture<void> putLinkedNotebook(
        qevercloud::LinkedNotebook linkedNotebook) = 0;

    [[nodiscard]] virtual QFuture<qevercloud::LinkedNotebook>
        findLinkedNotebookByGuid(qevercloud::Guid guid) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeLinkedNotebookByGuid(
        qevercloud::Guid guid) = 0;

    using ListLinkedNotebooksOptions =
        ILocalStorage::ListLinkedNotebooksOptions;

    using ListLinkedNotebooksOrder = ILocalStorage::ListLinkedNotebooksOrder;

    [[nodiscard]] virtual QFuture<QList<qevercloud::LinkedNotebook>>
        listLinkedNotebooks(ListLinkedNotebooksOptions options = {}) const = 0;
};

} // namespace quentier::local_storage::sql
