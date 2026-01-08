/*
 * Copyright 2024 Dmitry Ivanov
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

#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <QJsonObject>

namespace quentier::synchronization {

/**
 * Serialize ISyncChunksDataCounters instance to json object.
 */
[[nodiscard]] QJsonObject QUENTIER_EXPORT serializeSyncChunksDataCountersToJson(
    const ISyncChunksDataCounters & counters);

/**
 * Create ISyncChunksDataCounters instance from serialized json object.
 * @return nonnull pointer to ISyncChunksDataCounters in case of success or null
 *         pointer in case of deserialization failure.
 */
[[nodiscard]] ISyncChunksDataCountersPtr QUENTIER_EXPORT
    deserializeSyncChunksDataCountersFromJson(const QJsonObject & json);

} // namespace quentier::synchronization
