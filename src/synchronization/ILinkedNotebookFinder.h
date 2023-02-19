/*
 * Copyright 2023 Dmitry Ivanov
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

#include <qevercloud/types/LinkedNotebook.h>

#include <QFuture>
#include <QString>

#include <optional>

namespace quentier::synchronization {

/**
 * @brief The ILinkedNotebookFinder interface provides the means to look up
 * linked notebook given the local id of the notebook that the linked notebook
 * corresponds to
 */
class ILinkedNotebookFinder
{
public:
    virtual ~ILinkedNotebookFinder() = default;

    /**
     * @brief Find linked notebook by local id of the notebook corresponding
     * to the linked notebook
     * @param notebookLocalId The local if of the notebook which might
     *                        correspond to a linked notebook
     * @return future with std::nullopt if the notebook with given local id
     *         was not found or does not correspond to a linked notebook or
     *         with linked notebook if the notebook with given local id
     *         actually corresponds to a linked notebook. Or future with
     *         exception in case of some error while trying to find the linked
     *         notebook.
     */
    [[nodiscard]] virtual QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByNotebookLocalId(
            const QString & notebookLocalId) = 0;

    /**
     * @brief Find linked notebook by its guid
     * @param guid The guid of the linked notebook to be found
     * @return future with std::nullopt if the linked notebook with given guid
     *         was not found or with linked notebook otherwise. Or future with
     *         exception in case of some error while trying to find the linked
     *         notebook.
     */
    [[nodiscard]] virtual QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByGuid(
            const qevercloud::Guid & guid) = 0;
};

} // namespace quentier::synchronization
