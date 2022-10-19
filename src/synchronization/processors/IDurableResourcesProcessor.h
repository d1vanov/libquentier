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

#include "../Fwd.h"

#include <quentier/utility/cancelers/Fwd.h>

#include <synchronization/types/Fwd.h>

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#include <QFuture>
#include <QList>

namespace quentier::synchronization {

/**
 * @brief The IDurableResourcesProcessor interface represents a resources
 * processor which retries downloading and processing of resources which for
 * some reason failed during the previous sync attempt.
 */
class IDurableResourcesProcessor
{
public:
    virtual ~IDurableResourcesProcessor() = default;

    struct ICallback
    {
        virtual ~ICallback() = default;

        virtual void onProcessedResource(
            const qevercloud::Guid & resourceGuid,
            qint32 resourceUpdateSequenceNum) noexcept = 0;

        virtual void onResourceFailedToDownload(
            const qevercloud::Resource & resource,
            const QException & e) noexcept = 0;

        virtual void onResourceFailedToProcess(
            const qevercloud::Resource & resource,
            const QException & e) noexcept = 0;

        virtual void onResourceProcessingCancelled(
            const qevercloud::Resource & resource) noexcept = 0;
    };

    using ICallbackWeakPtr = std::weak_ptr<ICallback>;

    [[nodiscard]] virtual QFuture<DownloadResourcesStatusPtr> processResources(
        const QList<qevercloud::SyncChunk> & syncChunks,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak = {}) = 0;
};

} // namespace quentier::synchronization
