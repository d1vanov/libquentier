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

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/Resource.h>

#include <QException>
#include <QFuture>
#include <QList>

#include <memory>
#include <utility>

namespace quentier::synchronization {

class IResourcesProcessor
{
public:
    virtual ~IResourcesProcessor() = default;

    struct ProcessResourcesStatus
    {
        quint64 m_totalNewResources = 0UL;
        quint64 m_totalUpdatedResources = 0UL;

        struct ResourceWithException
        {
            qevercloud::Resource m_resource;
            std::shared_ptr<QException> m_exception;
        };

        using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

        QList<ResourceWithException> m_resourcesWhichFailedToDownload;
        QList<ResourceWithException> m_resourcesWhichFailedToProcess;

        UpdateSequenceNumbersByGuid m_processedResourceGuidsAndUsns;
        UpdateSequenceNumbersByGuid m_cancelledResourceGuidsAndUsns;
    };

    [[nodiscard]] virtual QFuture<ProcessResourcesStatus> processResources(
        const QList<qevercloud::SyncChunk> & syncChunks) = 0;
};

} // namespace quentier::synchronization
