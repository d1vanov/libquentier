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

#include <QMutex>

namespace quentier::local_storage::sql {

/**
 * @brief The DatabaseInfo struct is a value type mixing in itself a bunch of
 * things required to work with the local storage database
 */
struct DatabaseInfo
{
    /**
     * Pool from which database connections are received
     */
    ConnectionPoolPtr connectionPool;

    /**
     * Mutex which must be acquired for all writes to the local storage database
     */
    std::shared_ptr<QMutex> writerMutex;
};

} // namespace quentier::local_storage::sql
