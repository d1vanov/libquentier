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

#include <QFuture>
#include <QList>

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class IVersionHandler
{
public:
    virtual ~IVersionHandler() = default;

    [[nodiscard]] virtual QFuture<bool> isVersionTooHigh() const = 0;
    [[nodiscard]] virtual QFuture<bool> requiresUpgrade() const = 0;

    [[nodiscard]] virtual QFuture<QList<IPatchPtr>> requiredPatches() const = 0;

    [[nodiscard]] virtual QFuture<qint32> version() const = 0;
    [[nodiscard]] virtual QFuture<qint32> highestSupportedVersion() const = 0;
};

} // namespace quentier::local_storage::sql
