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

#include "TagsProcessor.h"
#include "Utils.h"

#include <synchronization/SyncChunksDataCounters.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/TagSortByParentChildRelations.h>

#include <qevercloud/types/SyncChunk.h>

#include <algorithm>

namespace quentier::synchronization {

namespace {

[[nodiscard]] QList<qevercloud::Tag> collectTags(
    const qevercloud::SyncChunk & syncChunk)
{
    if (!syncChunk.tags() || syncChunk.tags()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Tag> tags;
    tags.reserve(syncChunk.tags()->size());
    for (const auto & tag: qAsConst(*syncChunk.tags())) {
        if (Q_UNLIKELY(!tag.guid())) {
            QNWARNING(
                "synchronization::TagsProcessor",
                "Detected tag without guid, skipping it: " << tag);
            continue;
        }

        if (Q_UNLIKELY(!tag.updateSequenceNum())) {
            QNWARNING(
                "synchronization::TagsProcessor",
                "Detected tag without update sequence number, "
                    << "skipping it: " << tag);
            continue;
        }

        if (Q_UNLIKELY(!tag.name())) {
            QNWARNING(
                "synchronization::TagsProcessor",
                "Detected tag without name, skipping it: " << tag);
            continue;
        }

        tags << tag;
    }

    return tags;
}

[[nodiscard]] QList<qevercloud::Guid> collectExpungedTagGuids(
    const qevercloud::SyncChunk & syncChunk)
{
    return syncChunk.expungedTags().value_or(QList<qevercloud::Guid>{});
}

} // namespace

TagsProcessor::TagsProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    SyncChunksDataCountersPtr syncChunksDataCounters) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_syncChunksDataCounters{std::move(syncChunksDataCounters)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::TagsProcessor",
            "TagsProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::TagsProcessor",
            "TagsProcessor ctor: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksDataCounters)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::TagsProcessor",
            "TagsProcessor ctor: sync chuks data counters is null")}};
    }
}

QFuture<void> TagsProcessor::processTags(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG(
        "synchronization::TagsProcessor", "TagsProcessor::processTags");

    QList<qevercloud::Tag> tags;
    QList<qevercloud::Guid> expungedTags;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        tags << collectTags(syncChunk);
        expungedTags << collectExpungedTagGuids(syncChunk);
    }

    utils::filterOutExpungedItems(expungedTags, tags);

    // Also filtering out tags which parent tag guids are expunged.
    // NOTE: it still doesn't guarantee that, for example, grand-parent of some
    // tag is not expunged and for that reason putting tags into the local
    // storage and expunging tags from the local storage would be done one
    // after another.
    for (auto it = tags.begin(); it != tags.end(); )
    {
        if (!it->parentGuid()) {
            continue;
        }

        auto pit = std::find_if(
            expungedTags.constBegin(),
            expungedTags.constEnd(),
            [&it](const qevercloud::Guid & guid)
            {
                return it->parentGuid() == guid;
            });
        if (pit != expungedTags.constEnd()) {
            it = tags.erase(it);
            continue;
        }

        ++it;
    }

    m_syncChunksDataCounters->m_totalTags =
        static_cast<quint64>(std::max<int>(tags.size(), 0));

    m_syncChunksDataCounters->m_totalExpungedTags =
        static_cast<quint64>(std::max<int>(expungedTags.size(), 0));

    if (tags.isEmpty() && expungedTags.isEmpty()) {
        QNDEBUG(
            "synchronization::TagsProcessor",
            "No new/updated/expunged tags in the sync chunks");

        return threading::makeReadyFuture();
    }

    QList<QFuture<void>> futures;
    futures.reserve(2);
    futures << processTagsList(std::move(tags));
    futures << processExpungedTags(std::move(expungedTags));

    return threading::whenAll(std::move(futures));
}

QFuture<void> TagsProcessor::processTagsList(QList<qevercloud::Tag> tags)
{
    if (tags.isEmpty()) {
        return threading::makeReadyFuture();
    }

    ErrorString errorDescription;
    if (!sortTagsByParentChildRelations(tags, errorDescription)) {
        return threading::makeExceptionalFuture<void>(
            RuntimeError{std::move(errorDescription)});
    }

    const int tagCount = tags.size();
    QList<std::shared_ptr<QPromise<void>>> tagPromises;
    tagPromises.reserve(tagCount);
    for (int i = 0; i < tagCount; ++i) {
        tagPromises << std::make_shared<QPromise<void>>();
    }

    QList<QFuture<void>> tagFutures;
    tagFutures.reserve(tags.size());
    for (const auto & promise: qAsConst(tagPromises)) {
        tagFutures << promise->future();
    }

    processTagsOneByOne(std::move(tags), 0, std::move(tagPromises));
    return threading::whenAll(std::move(tagFutures));
}

QFuture<void> TagsProcessor::processExpungedTags(
    QList<qevercloud::Guid> expungedTags)
{
    if (expungedTags.isEmpty()) {
        return threading::makeReadyFuture();
    }

    const auto selfWeak = weak_from_this();

    QList<QFuture<void>> expungedTagFutures;
    expungedTagFutures.reserve(expungedTags.size());
    for (const auto & guid: qAsConst(expungedTags)) {
        auto tagPromise = std::make_shared<QPromise<void>>();
        expungedTagFutures << tagPromise->future();
        tagPromise->start();

        auto expungeTagFuture = m_localStorage->expungeTagByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeTagFuture),
            threading::TrackedTask{
                selfWeak,
                [this] { ++m_syncChunksDataCounters->m_expungedTags; }});

        threading::thenOrFailed(
            std::move(thenFuture), std::move(tagPromise));
    }

    return threading::whenAll(std::move(expungedTagFutures));
}

} // namespace quentier::synchronization
