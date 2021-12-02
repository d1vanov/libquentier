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

#include <QFuture>

namespace quentier::local_storage::sql {

class ISynchronizationInfoHandler
{
public:
    virtual ~ISynchronizationInfoHandler() = default;

    using HighestUsnOption = ILocalStorage::HighestUsnOption;

    [[nodiscard]] virtual QFuture<qint32> highestUpdateSequenceNumber(
        HighestUsnOption option) const = 0;

    [[nodiscard]] virtual QFuture<qint32> highestUpdateSequenceNumber(
        qevercloud::Guid linkedNotebookGuid) const = 0;
};

} // namespace quentier::local_storage::sql
