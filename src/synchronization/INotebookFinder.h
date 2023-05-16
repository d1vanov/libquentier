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

#include <qevercloud/types/Notebook.h>

#include <QFuture>
#include <QString>

#include <optional>

namespace quentier::synchronization {

/**
 * @brief The INotebookFinder interface provides the means to look up
 * notebook given the local id of the note from the the notebook
 */
class INotebookFinder
{
public:
    virtual ~INotebookFinder() = default;

    /**
     * @brief Find notebook by local id of the note from the notebook
     * @param noteLocalId The local id of the note for which the notebook is
     *                    requested
     * @return future with std::nullopt if the notebook was not found or with
     *         notebook if it was found. Or future with exception in case of
     *         some error while trying to find the notebook.
     */
    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Notebook>>
        findNotebookByNoteLocalId(const QString & noteLocalId) = 0;
};

} // namespace quentier::synchronization
