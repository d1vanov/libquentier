/*
 * Copyright 2022 Dmitry Ivanov
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

#include <quentier/synchronization/ISynchronizer.h>

namespace quentier::synchronization {

/**
 * @brief The IReceiver class represents the interface for handling of receiving
 * the changes in data items from Evernote during sync - notebooks, tags, notes
 * etc.
 */
class IReceiver
{
public:
    using SyncResult = ISynchronizer::SyncResult;
    using SyncState = ISynchronizer::SyncState;
    using SyncStats = ISynchronizer::SyncStats;

public:
    virtual ~IReceiver() = default;

    /**
     * @brief receive changes in data items from Evernote
     */
    [[nodiscard]] virtual QFuture<SyncResult> receive() = 0;
};

} // namespace quentier::synchronization
