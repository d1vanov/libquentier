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

#include <QFuture>
#include <QList>

#include <memory>

namespace quentier::synchronization {

class ITagsProcessor
{
public:
    virtual ~ITagsProcessor() = default;

    class ICallback
    {
    public:
        virtual ~ICallback() = default;

        virtual void onTagsProcessingProgress(
            qint32 totalTags, qint32 totalTagsToExpunge, qint32 addedTags,
            qint32 updatedTags, qint32 expungedTags) = 0;
    };

    using ICallbackWeakPtr = std::weak_ptr<ICallback>;

    [[nodiscard]] virtual QFuture<void> processTags(
        const QList<qevercloud::SyncChunk> & syncChunks,
        ICallbackWeakPtr callbackWeak) = 0;
};

} // namespace quentier::synchronization
