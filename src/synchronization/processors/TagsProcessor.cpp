/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/TagSortByParentChildRelations.h>

#include <qevercloud/types/SyncChunk.h>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <utility>

namespace quentier::synchronization {

class TagsProcessor::TagCounters
{
public:
    TagCounters(
        const qint32 totalTags, const qint32 totalTagsToExpunge,
        ITagsProcessor::ICallbackWeakPtr callbackWeak) :
        m_totalTags{totalTags}, m_totalExpungedTags{totalTagsToExpunge},
        m_callbackWeak{std::move(callbackWeak)}
    {}

    void onAddedTag()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_addedTags;
        notifyUpdate();
    }

    void onUpdatedTag()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_updatedTags;
        notifyUpdate();
    }

    void onExpungedTag()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_expungedTags;
        notifyUpdate();
    }

private:
    void notifyUpdate()
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onTagsProcessingProgress(
                m_totalTags, m_totalExpungedTags, m_addedTags, m_updatedTags,
                m_expungedTags);
        }
    }

private:
    const qint32 m_totalTags;
    const qint32 m_totalExpungedTags;
    const ITagsProcessor::ICallbackWeakPtr m_callbackWeak;

    QMutex m_mutex;
    qint32 m_addedTags{0};
    qint32 m_updatedTags{0};
    qint32 m_expungedTags{0};
};

TagsProcessor::TagsProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("TagsProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "TagsProcessor ctor: sync conflict resolver is null")}};
    }
}

QFuture<void> TagsProcessor::processTags(
    const QList<qevercloud::SyncChunk> & syncChunks,
    ICallbackWeakPtr callbackWeak)
{
    QNDEBUG("synchronization::TagsProcessor", "TagsProcessor::processTags");

    QList<qevercloud::Tag> tags;
    QList<qevercloud::Guid> expungedTags;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        tags << utils::collectTagsFromSyncChunk(syncChunk);
        expungedTags << utils::collectExpungedTagGuidsFromSyncChunk(syncChunk);
    }

    utils::filterOutExpungedItems(expungedTags, tags);

    // Also filtering out tags which parent tag guids are expunged.
    // NOTE: it still doesn't guarantee that, for example, grand-parent of some
    // tag is not expunged and for that reason putting tags into the local
    // storage and expunging tags from the local storage would be done one
    // after another.
    for (auto it = tags.begin(); it != tags.end();) {
        if (!it->parentGuid()) {
            ++it;
            continue;
        }

        auto pit = std::find_if(
            expungedTags.constBegin(), expungedTags.constEnd(),
            [&it](const qevercloud::Guid & guid) {
                return it->parentGuid() == guid;
            });
        if (pit != expungedTags.constEnd()) {
            it = tags.erase(it);
            continue;
        }

        ++it;
    }

    if (tags.isEmpty() && expungedTags.isEmpty()) {
        QNDEBUG(
            "synchronization::TagsProcessor",
            "No new/updated/expunged tags in the sync chunks");

        return threading::makeReadyFuture();
    }

    const auto totalTags = tags.size();
    const auto totalTagsToExpunge = expungedTags.size();

    const auto tagCounters = std::make_shared<TagCounters>(
        totalTags, totalTagsToExpunge, std::move(callbackWeak));

    QList<QFuture<void>> futures;
    futures.reserve(2);
    futures << processTagsList(std::move(tags), tagCounters);
    futures << processExpungedTags(std::move(expungedTags), tagCounters);

    return threading::whenAll(std::move(futures));
}

QFuture<void> TagsProcessor::processTagsList(
    QList<qevercloud::Tag> tags,
    const std::shared_ptr<TagCounters> & tagCounters)
{
    if (tags.isEmpty()) {
        return threading::makeReadyFuture();
    }

    ErrorString errorDescription;
    if (!sortTagsByParentChildRelations(tags, errorDescription)) {
        return threading::makeExceptionalFuture<void>(
            RuntimeError{std::move(errorDescription)});
    }

    const auto tagCount = tags.size();
    QList<std::shared_ptr<QPromise<void>>> tagPromises;
    tagPromises.reserve(tagCount);
    for (qint64 i = 0; i < tagCount; ++i) {
        tagPromises << std::make_shared<QPromise<void>>();
    }

    QList<QFuture<void>> tagFutures;
    tagFutures.reserve(tags.size());
    for (const auto & promise: std::as_const(tagPromises)) {
        tagFutures << promise->future();
    }

    processTagsOneByOne(
        std::move(tags), 0, std::move(tagPromises), tagCounters);

    return threading::whenAll(std::move(tagFutures));
}

QFuture<void> TagsProcessor::processExpungedTags(
    QList<qevercloud::Guid> expungedTags,
    const std::shared_ptr<TagCounters> & tagCounters)
{
    if (expungedTags.isEmpty()) {
        return threading::makeReadyFuture();
    }

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    QList<QFuture<void>> expungedTagFutures;
    expungedTagFutures.reserve(expungedTags.size());
    for (const auto & guid: std::as_const(expungedTags)) {
        auto tagPromise = std::make_shared<QPromise<void>>();
        expungedTagFutures << tagPromise->future();
        tagPromise->start();

        auto expungeTagFuture = m_localStorage->expungeTagByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeTagFuture), currentThread,
            [tagCounters] { tagCounters->onExpungedTag(); });

        threading::thenOrFailed(
            std::move(thenFuture), currentThread, std::move(tagPromise));
    }

    return threading::whenAll(std::move(expungedTagFutures));
}

QFuture<void> TagsProcessor::processTag(
    const QList<qevercloud::Tag> & tags, int tagIndex,
    const std::shared_ptr<TagCounters> & tagCounters,
    CheckParentTag checkParentTag)
{
    if (Q_UNLIKELY(tagIndex < 0 || tagIndex >= tags.size())) { // NOLINT
        return threading::makeExceptionalFuture<void>(RuntimeError{
            ErrorString{QStringLiteral("TagsProcessor: wrong tag index")}});
    }

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    const auto & tag = tags[tagIndex];
    if (checkParentTag == CheckParentTag::Yes && tag.parentGuid()) {
        // In order to put the tag into the local storage its parent tag
        // must already be there. However, there is one exception to this rule:
        // if the tag comes from a linked notebook, it can have a parent tag
        // but this parent tag might not be shared with the current user and
        // thus would be unavailable. In that case parent guid needs to be
        // cleared from the tag which is shared with the user.

        // If the parent tag is present within the list of tags before tagIndex,
        // it must have already been put into the local storage.
        for (int i = 0; i < tagIndex; ++i) {
            if (tags[i].guid() == tag.parentGuid()) {
                // Found parent of the currently processed tag
                return processTag(
                    tags, tagIndex, tagCounters, CheckParentTag::No);
            }
        }

        // Haven't found the parent tag in the list of tags, need to check
        // its presence in the local storage asynchronously.
        auto findParentTagFuture =
            m_localStorage->findTagByGuid(*tag.parentGuid());

        auto promise = std::make_shared<QPromise<void>>();
        auto future = promise->future();

        promise->start();

        threading::thenOrFailed(
            std::move(findParentTagFuture), currentThread, promise,
            threading::TrackedTask{
                selfWeak,
                [this, promise, tags = tags, tagIndex, tagCounters,
                 currentThread](
                    const std::optional<qevercloud::Tag> & parentTag) mutable {
                    if (!parentTag) {
                        // Haven't found the parent tag, must clear parent guid
                        // from the processed tag
                        tags[tagIndex].setParentGuid(std::nullopt);
                        tags[tagIndex].setParentTagLocalId(QString{});
                    }

                    auto processTagFuture = processTag(
                        tags, tagIndex, tagCounters, CheckParentTag::No);

                    threading::thenOrFailed(
                        std::move(processTagFuture), currentThread,
                        std::move(promise));
                }});

        return future;
    }

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    Q_ASSERT(tag.guid());

    auto findTagFuture = m_localStorage->findTagByGuid(*tag.guid());
    threading::thenOrFailed(
        std::move(findTagFuture), currentThread, promise,
        threading::TrackedTask{
            selfWeak,
            [this, updatedTag = tag, tagCounters,
             promise](const std::optional<qevercloud::Tag> & tag) mutable {
                if (tag) {
                    onFoundDuplicate(
                        promise, tagCounters, std::move(updatedTag), *tag);
                    return;
                }

                tryToFindDuplicateByName(
                    promise, tagCounters, std::move(updatedTag));
            }});

    return future;
}

void TagsProcessor::processTagsOneByOne(
    QList<qevercloud::Tag> tags, int tagIndex,
    QList<std::shared_ptr<QPromise<void>>> tagPromises,
    const std::shared_ptr<TagCounters> & tagCounters)
{
    Q_ASSERT(tags.size() == tagPromises.size());

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto processTagFuture =
        processTag(tags, tagIndex, tagCounters, CheckParentTag::Yes);

    auto thenFuture = threading::then(
        std::move(processTagFuture), currentThread,
        threading::TrackedTask{
            selfWeak,
            [this, tags = std::move(tags), tagIndex, tagPromises,
             tagCounters]() mutable {
                tagPromises[tagIndex]->finish();

                ++tagIndex;
                if (tagIndex == tags.size()) {
                    return;
                }

                processTagsOneByOne(tags, tagIndex, tagPromises, tagCounters);
            }});

    threading::onFailed(
        std::move(thenFuture), currentThread,
        [tagPromises, tagIndex](const QException & e) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            int i = tagIndex;
#else
            qsizetype i = tagIndex;
#endif
            for (auto size = tagPromises.size(); i < size; ++i) {
                tagPromises[i]->setException(e);
            }
        });
}

void TagsProcessor::tryToFindDuplicateByName(
    const std::shared_ptr<QPromise<void>> & tagPromise,
    const std::shared_ptr<TagCounters> & tagCounters,
    qevercloud::Tag updatedTag)
{
    Q_ASSERT(updatedTag.name());

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto findTagByName = m_localStorage->findTagByName(
        *updatedTag.name(), updatedTag.linkedNotebookGuid());

    threading::thenOrFailed(
        std::move(findTagByName), currentThread, tagPromise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, updatedTag = std::move(updatedTag),
             tagPromise = tagPromise, currentThread,
             tagCounters](const std::optional<qevercloud::Tag> & tag) mutable {
                if (tag) {
                    onFoundDuplicate(
                        tagPromise, tagCounters, std::move(updatedTag), *tag);
                    return;
                }

                // No duplicate by either guid or name was found,
                // just put the updated notebook to the local storage
                auto putTagFuture =
                    m_localStorage->putTag(std::move(updatedTag));

                auto thenFuture = threading::then(
                    std::move(putTagFuture), currentThread,
                    [tagCounters] { tagCounters->onAddedTag(); });

                threading::thenOrFailed(
                    std::move(thenFuture), currentThread,
                    std::move(tagPromise));
            }});
}

void TagsProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<void>> & tagPromise,
    const std::shared_ptr<TagCounters> & tagCounters,
    qevercloud::Tag updatedTag, qevercloud::Tag localTag)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;
    using TagConflictResolution = ISyncConflictResolver::TagConflictResolution;

    auto localTagLocalId = localTag.localId();
    const auto localTagLocallyFavorited = localTag.isLocallyFavorited();

    auto statusFuture = m_syncConflictResolver->resolveTagConflict(
        updatedTag, std::move(localTag));

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(statusFuture), currentThread, tagPromise,
        [this, selfWeak, tagPromise, tagCounters,
         updatedTag = std::move(updatedTag),
         localTagLocalId = std::move(localTagLocalId), localTagLocallyFavorited,
         currentThread](const TagConflictResolution & resolution) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseTheirs>(
                    resolution) ||
                std::holds_alternative<ConflictResolution::IgnoreMine>(
                    resolution))
            {
                if (std::holds_alternative<ConflictResolution::UseTheirs>(
                        resolution))
                {
                    updatedTag.setLocalId(localTagLocalId);
                    updatedTag.setLocallyFavorited(localTagLocallyFavorited);
                }

                auto putTagFuture =
                    m_localStorage->putTag(std::move(updatedTag));

                auto thenFuture = threading::then(
                    std::move(putTagFuture), currentThread,
                    [tagCounters] { tagCounters->onUpdatedTag(); });

                threading::thenOrFailed(
                    std::move(thenFuture), currentThread, tagPromise);
                return;
            }

            if (std::holds_alternative<ConflictResolution::UseMine>(resolution))
            {
                tagPromise->finish();
                return;
            }

            if (std::holds_alternative<
                    ConflictResolution::MoveMine<qevercloud::Tag>>(resolution))
            {
                const auto & mineResolution =
                    std::get<ConflictResolution::MoveMine<qevercloud::Tag>>(
                        resolution);

                auto updateLocalTagFuture =
                    m_localStorage->putTag(mineResolution.mine);

                threading::thenOrFailed(
                    std::move(updateLocalTagFuture), currentThread, tagPromise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, selfWeak, tagPromise, tagCounters, currentThread,
                         updatedTag = std::move(updatedTag)]() mutable {
                            auto putTagFuture =
                                m_localStorage->putTag(std::move(updatedTag));

                            auto thenFuture = threading::then(
                                std::move(putTagFuture), currentThread,
                                [tagPromise, tagCounters] {
                                    tagCounters->onAddedTag();
                                    tagPromise->finish();
                                });

                            threading::onFailed(
                                std::move(thenFuture), currentThread,
                                [tagPromise](const QException & e) {
                                    tagPromise->setException(e);
                                    tagPromise->finish();
                                });
                        }});
            }
        });
}

} // namespace quentier::synchronization
