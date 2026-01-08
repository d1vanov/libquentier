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

class ISavedSearchesProcessor
{
public:
    virtual ~ISavedSearchesProcessor() = default;

    class ICallback
    {
    public:
        virtual ~ICallback() = default;

        virtual void onSavedSearchesProcessingProgress(
            qint32 totalSavedSearches, qint32 totalSavedSearchesToExpunge,
            qint32 addedSavedSearches, qint32 updatedSavedSearches,
            qint32 expungedSavedSearches) = 0;
    };

    using ICallbackWeakPtr = std::weak_ptr<ICallback>;

    [[nodiscard]] virtual QFuture<void> processSavedSearches(
        const QList<qevercloud::SyncChunk> & syncChunks,
        ICallbackWeakPtr callbackWeak) = 0;
};

} // namespace quentier::synchronization
