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

#include <quentier/synchronization/types/Errors.h>

#include <qevercloud/EDAMErrorCode.h>
#include <qevercloud/exceptions/EDAMNotFoundException.h>
#include <qevercloud/exceptions/EDAMSystemException.h>
#include <qevercloud/exceptions/EDAMUserException.h>

#include <QString>

#include <optional>

namespace quentier::synchronization::tests::utils {

[[nodiscard]] qevercloud::EDAMNotFoundException createNotFoundException(
    QString identifier, std::optional<QString> key = std::nullopt);

[[nodiscard]] qevercloud::EDAMUserException createUserException(
    qevercloud::EDAMErrorCode errorCode, QString parameter);

[[nodiscard]] qevercloud::EDAMSystemException createStopSyncException(
    const StopSynchronizationError & error);

} // namespace quentier::synchronization::tests::utils
