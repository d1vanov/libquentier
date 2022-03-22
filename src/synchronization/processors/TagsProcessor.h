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

#include "ITagsProcessor.h"

#include <synchronization/Fwd.h>

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/types/TypeAliases.h>

#include <memory>
#include <optional>

namespace quentier::synchronization {

class TagsProcessor final :
    public ITagsProcessor,
    public std::enable_shared_from_this<TagsProcessor>
{
public:
    explicit TagsProcessor(
        local_storage::ILocalStoragePtr localStorage,
        ISyncConflictResolverPtr syncConflictResolver,
        SyncChunksDataCountersPtr syncChunksDataCounters);

    [[nodiscard]] QFuture<void> processTags(
        const QList<qevercloud::SyncChunk> & syncChunks) override;

private:
    [[nodiscard]] QFuture<void> processTagsList(QList<qevercloud::Tag> tags);

    [[nodiscard]] QFuture<void> processExpungedTags(
        QList<qevercloud::Guid> expungedTags);

    [[nodiscard]] QFuture<void> processTag(qevercloud::Tag tag);

    void processTagsOneByOne(
        QList<qevercloud::Tag> tags,
        int tagIndex,
        QList<std::shared_ptr<QPromise<void>>> tagPromises);

    void tryToFindDuplicateByName(
        const std::shared_ptr<QPromise<void>> & tagPromise,
        qevercloud::Tag updatedTag);

    void onFoundDuplicate(
        const std::shared_ptr<QPromise<void>> & tagPromise,
        qevercloud::Tag updatedTag,
        qevercloud::Tag localTag);

private:
    const local_storage::ILocalStoragePtr m_localStorage;
    const ISyncConflictResolverPtr m_syncConflictResolver;
    const SyncChunksDataCountersPtr m_syncChunksDataCounters;
};

} // namespace quentier::synchronization
