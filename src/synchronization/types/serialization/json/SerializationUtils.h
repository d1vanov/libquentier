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

#include <quentier/synchronization/types/Errors.h>

#include <QException>
#include <QJsonObject>

#include <algorithm>
#include <memory>
#include <limits>
#include <optional>

namespace quentier::synchronization {

[[nodiscard]] QJsonObject serializeException(const QException & e);

[[nodiscard]] std::shared_ptr<QException> deserializeException(
    const QJsonObject & json);

[[nodiscard]] QJsonObject serializeStopSynchronizationError(
    const StopSynchronizationError & error);

[[nodiscard]] std::optional<StopSynchronizationError>
    deserializeStopSyncronizationError(const QJsonObject & json);

template <class From, class To>
[[nodiscard]] To safeCast(const From value)
{
    return static_cast<To>(std::clamp<From>(
        value, static_cast<From>(std::numeric_limits<To>::lowest()),
        static_cast<From>(std::numeric_limits<To>::max())));
}

} // namespace quentier::synchronization
