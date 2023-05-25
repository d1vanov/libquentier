/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <qevercloud/Fwd.h>

#include <QDir>

#include <optional>

namespace quentier::synchronization {

class QUENTIER_EXPORT ISyncOptionsBuilder
{
public:
    virtual ~ISyncOptionsBuilder() noexcept;

    virtual ISyncOptionsBuilder & setDownloadNoteThumbnails(bool value) = 0;

    virtual ISyncOptionsBuilder & setInkNoteImagesStorageDir(
        std::optional<QDir> dir) = 0;

    virtual ISyncOptionsBuilder & setRequestContext(
        qevercloud::IRequestContextPtr ctx) = 0;

    virtual ISyncOptionsBuilder & setRetryPolicy(
        qevercloud::IRetryPolicyPtr retryPolicy) = 0;

    [[nodiscard]] virtual ISyncOptionsPtr build() = 0;
};

[[nodiscard]] QUENTIER_EXPORT ISyncOptionsBuilderPtr createSyncOptionsBuilder();

} // namespace quentier::synchronization
