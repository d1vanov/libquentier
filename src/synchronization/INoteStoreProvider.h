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

#include <qevercloud/Fwd.h>
#include <qevercloud/services/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#include <QFuture>

#include <optional>

namespace quentier::synchronization {

class INoteStoreProvider
{
public:
    virtual ~INoteStoreProvider() = default;

    [[nodiscard]] virtual QFuture<qevercloud::INoteStorePtr> noteStore(
        QString notebookLocalId,
        qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {}) = 0;
};

} // namespace quentier::synchronization
