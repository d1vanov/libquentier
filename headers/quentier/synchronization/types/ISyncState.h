/*
 * Copyright 2022-2025 Dmitry Ivanov
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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/TypeAliases.h>

#include <QHash>
#include <QString>

namespace quentier::synchronization {

/**
 * @brief The ISyncState interface provides accessory methods to determine
 * the sync state for the account
 */
class QUENTIER_EXPORT ISyncState : public utility::Printable
{
public:
    [[nodiscard]] virtual qint32 userDataUpdateCount() const = 0;

    [[nodiscard]] virtual qevercloud::Timestamp userDataLastSyncTime()
        const = 0;

    [[nodiscard]] virtual QHash<qevercloud::Guid, qint32>
        linkedNotebookUpdateCounts() const = 0;

    [[nodiscard]] virtual QHash<qevercloud::Guid, qevercloud::Timestamp>
        linkedNotebookLastSyncTimes() const = 0;
};

} // namespace quentier::synchronization
