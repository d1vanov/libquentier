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

#include <quentier/synchronization/types/ISyncOptions.h>

namespace quentier::synchronization {

/**
 * @brief Options for synchronization process
 */
struct SyncOptions final : public ISyncOptions
{
    ~SyncOptions() noexcept override = default;

    [[nodiscard]] bool downloadNoteThumbnails() const noexcept override;
    [[nodiscard]] std::optional<QDir> inkNoteImagesStorageDir() const override;

    [[nodiscard]] qevercloud::IRequestContextPtr requestContext()
        const noexcept override;

    [[nodiscard]] qevercloud::IRetryPolicyPtr retryPolicy()
        const noexcept override;

    QTextStream & print(QTextStream & strm) const override;

    bool m_downloadNoteThumbnails = false;
    std::optional<QDir> m_inkNoteImagesStorageDir;
    qevercloud::IRequestContextPtr m_ctx;
    qevercloud::IRetryPolicyPtr m_retryPolicy;
};

[[nodiscard]] bool operator==(
    const SyncOptions & lhs, const SyncOptions & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const SyncOptions & lhs, const SyncOptions & rhs) noexcept;

} // namespace quentier::synchronization
