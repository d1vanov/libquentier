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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/Resource.h>
#include <qevercloud/types/TypeAliases.h>

#include <QException>

#include <memory>
#include <utility>

namespace quentier::synchronization {

class QUENTIER_EXPORT IDownloadResourcesStatus : public Printable
{
public:
    ~IDownloadResourcesStatus() noexcept override;

    using QExceptionPtr = std::shared_ptr<QException>;

    using ResourceWithException =
        std::pair<qevercloud::Resource, QExceptionPtr>;

    using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

    [[nodiscard]] virtual quint64 totalNewResources() const = 0;
    [[nodiscard]] virtual quint64 totalUpdatedResources() const = 0;

    [[nodiscard]] virtual QList<ResourceWithException>
        resourcesWhichFailedToDownload() const = 0;

    [[nodiscard]] virtual QList<ResourceWithException>
        resourcesWhichFailedToProcess() const = 0;

    [[nodiscard]] virtual UpdateSequenceNumbersByGuid
        processedResourceGuidsAndUsns() const = 0;

    [[nodiscard]] virtual UpdateSequenceNumbersByGuid
        cancelledResourceGuidsAndUsns() const = 0;
};

} // namespace quentier::synchronization
