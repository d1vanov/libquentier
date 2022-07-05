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

#include <quentier/synchronization/types/IDownloadResourcesStatus.h>

namespace quentier::synchronization {

struct DownloadResourcesStatus final : public IDownloadResourcesStatus
{
    [[nodiscard]] quint64 totalNewResources() const noexcept override;
    [[nodiscard]] quint64 totalUpdatedResources() const noexcept override;

    [[nodiscard]] QList<ResourceWithException> resourcesWhichFailedToDownload()
        const override;

    [[nodiscard]] QList<ResourceWithException> resourcesWhichFailedToProcess()
        const override;

    [[nodiscard]] UpdateSequenceNumbersByGuid processedResourceGuidsAndUsns()
        const override;

    [[nodiscard]] UpdateSequenceNumbersByGuid cancelledResourceGuidsAndUsns()
        const override;

    QTextStream & print(QTextStream & strm) const override;

    quint64 m_totalNewResources = 0UL;
    quint64 m_totalUpdatedResources = 0UL;

    QList<ResourceWithException> m_resourcesWhichFailedToDownload;
    QList<ResourceWithException> m_resourcesWhichFailedToProcess;

    UpdateSequenceNumbersByGuid m_processedResourceGuidsAndUsns;
    UpdateSequenceNumbersByGuid m_cancelledResourceGuidsAndUsns;
};

[[nodiscard]] bool operator==(
    const DownloadResourcesStatus & lhs,
    const DownloadResourcesStatus & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const DownloadResourcesStatus & lhs,
    const DownloadResourcesStatus & rhs) noexcept;

} // namespace quentier::synchronization
