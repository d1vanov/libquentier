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

#include "NotebooksProcessor.h"
#include "Utils.h"

#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/SyncChunk.h>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

namespace quentier::synchronization {

class NotebooksProcessor::NotebookCounters
{
public:
    NotebookCounters(
        const qint32 totalNotebooks, const qint32 totalNotebooksToExpunge, // NOLINT
        INotebooksProcessor::ICallbackWeakPtr callbackWeak) :
        m_totalNotebooks{totalNotebooks},
        m_totalNotebooksToExpunge{totalNotebooksToExpunge},
        m_callbackWeak{std::move(callbackWeak)}
    {}

    void onAddedNotebook()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_addedNotebooks;
        notifyUpdate();
    }

    void onUpdatedNotebook()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_updatedNotebooks;
        notifyUpdate();
    }

    void onExpungedNotebook()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_expungedNotebooks;
        notifyUpdate();
    }

private:
    void notifyUpdate()
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onNotebooksProcessingProgress(
                m_totalNotebooks, m_totalNotebooksToExpunge,
                m_addedNotebooks, m_updatedNotebooks, m_expungedNotebooks);
        }
    }

private:
    const qint32 m_totalNotebooks;
    const qint32 m_totalNotebooksToExpunge;
    const INotebooksProcessor::ICallbackWeakPtr m_callbackWeak;

    QMutex m_mutex;
    qint32 m_addedNotebooks{0};
    qint32 m_updatedNotebooks{0};
    qint32 m_expungedNotebooks{0};
};

NotebooksProcessor::NotebooksProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    threading::QThreadPoolPtr threadPool) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_threadPool{
        threadPool ? std::move(threadPool) : threading::globalThreadPool()}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NotebooksProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "NotebooksProcessor ctor: sync conflict resolver is null")}};
    }

    Q_ASSERT(m_threadPool);
}

QFuture<void> NotebooksProcessor::processNotebooks(
    const QList<qevercloud::SyncChunk> & syncChunks,
    ICallbackWeakPtr callbackWeak)
{
    QNDEBUG(
        "synchronization::NotebooksProcessor",
        "NotebooksProcessor::processNotebooks");

    QList<qevercloud::Notebook> notebooks;
    QList<qevercloud::Guid> expungedNotebooks;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        notebooks << utils::collectNotebooksFromSyncChunk(syncChunk);

        expungedNotebooks << utils::collectExpungedNotebookGuidsFromSyncChunk(
            syncChunk);
    }

    utils::filterOutExpungedItems(expungedNotebooks, notebooks);

    if (notebooks.isEmpty() && expungedNotebooks.isEmpty()) {
        QNDEBUG(
            "synchronization::NotebooksProcessor",
            "No new/updated/expunged notebooks in the sync chunks");

        return threading::makeReadyFuture();
    }

    const qint32 totalNotebooks = notebooks.size();
    const qint32 totalExpungedNotebooks = expungedNotebooks.size();
    const qint32 totalItemCount = totalNotebooks + totalExpungedNotebooks;

    const auto selfWeak = weak_from_this();
    QList<QFuture<void>> notebookFutures;
    notebookFutures.reserve(totalItemCount);

    const auto notebookCounters = std::make_shared<NotebookCounters>(
        totalNotebooks, totalExpungedNotebooks, std::move(callbackWeak));

    for (const auto & notebook: qAsConst(notebooks)) {
        auto notebookPromise = std::make_shared<QPromise<void>>();
        notebookFutures << notebookPromise->future();
        notebookPromise->start();

        Q_ASSERT(notebook.guid());

        auto findNotebookByGuidFuture =
            m_localStorage->findNotebookByGuid(*notebook.guid());

        threading::thenOrFailed(
            std::move(findNotebookByGuidFuture), notebookPromise,
            threading::TrackedTask{
                selfWeak,
                [this, updatedNotebook = notebook, notebookPromise,
                 notebookCounters](const std::optional<qevercloud::Notebook> &
                                       notebook) mutable {
                    if (notebook) {
                        onFoundDuplicate(
                            notebookPromise, notebookCounters,
                            std::move(updatedNotebook), *notebook);
                        return;
                    }

                    tryToFindDuplicateByName(
                        notebookPromise, notebookCounters,
                        std::move(updatedNotebook));
                }});
    }

    for (const auto & guid: qAsConst(expungedNotebooks)) {
        auto notebookPromise = std::make_shared<QPromise<void>>();
        notebookFutures << notebookPromise->future();
        notebookPromise->start();

        auto expungeNotebookFuture =
            m_localStorage->expungeNotebookByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeNotebookFuture), m_threadPool.get(),
            [notebookCounters] { notebookCounters->onExpungedNotebook(); });

        threading::thenOrFailed(
            std::move(thenFuture), std::move(notebookPromise));
    }

    return threading::whenAll(std::move(notebookFutures));
}

void NotebooksProcessor::tryToFindDuplicateByName(
    const std::shared_ptr<QPromise<void>> & notebookPromise,
    const std::shared_ptr<NotebookCounters> & notebookCounters,
    qevercloud::Notebook updatedNotebook)
{
    Q_ASSERT(updatedNotebook.name());

    const auto selfWeak = weak_from_this();

    auto findNotebookByNameFuture = m_localStorage->findNotebookByName(
        *updatedNotebook.name(), updatedNotebook.linkedNotebookGuid());

    threading::thenOrFailed(
        std::move(findNotebookByNameFuture), notebookPromise,
        threading::TrackedTask{
            selfWeak,
            [this, selfWeak, updatedNotebook = std::move(updatedNotebook),
             notebookPromise = notebookPromise, notebookCounters](
                const std::optional<qevercloud::Notebook> & notebook) mutable {
                if (notebook) {
                    onFoundDuplicate(
                        notebookPromise, notebookCounters,
                        std::move(updatedNotebook), *notebook);
                    return;
                }

                // No duplicate by either guid or name was found,
                // just put the updated notebook into the local storage
                auto putNotebookFuture =
                    m_localStorage->putNotebook(std::move(updatedNotebook));

                auto thenFuture = threading::then(
                    std::move(putNotebookFuture), m_threadPool.get(),
                    [notebookCounters] {
                        notebookCounters->onAddedNotebook();
                    });

                threading::thenOrFailed(
                    std::move(thenFuture), std::move(notebookPromise));
            }});
}

void NotebooksProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<void>> & notebookPromise,
    const std::shared_ptr<NotebookCounters> & notebookCounters,
    qevercloud::Notebook updatedNotebook, qevercloud::Notebook localNotebook)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;
    using NotebookConflictResolution =
        ISyncConflictResolver::NotebookConflictResolution;

    auto localNotebookLocalId = localNotebook.localId();
    const bool localNotebookLocallyFavorited =
        localNotebook.isLocallyFavorited();

    auto statusFuture = m_syncConflictResolver->resolveNotebookConflict(
        updatedNotebook, std::move(localNotebook));

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(statusFuture), notebookPromise,
        [this, selfWeak, notebookPromise,
         updatedNotebook = std::move(updatedNotebook),
         localNotebookLocalId = std::move(localNotebookLocalId),
         localNotebookLocallyFavorited, notebookCounters](
            const NotebookConflictResolution & resolution) mutable {
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
                        resolution)) {
                    updatedNotebook.setLocalId(localNotebookLocalId);

                    updatedNotebook.setLocallyFavorited(
                        localNotebookLocallyFavorited);
                }

                auto putNotebookFuture =
                    m_localStorage->putNotebook(std::move(updatedNotebook));

                auto thenFuture = threading::then(
                    std::move(putNotebookFuture), m_threadPool.get(),
                    [notebookCounters] {
                        notebookCounters->onUpdatedNotebook();
                    });

                threading::thenOrFailed(std::move(thenFuture), notebookPromise);

                return;
            }

            if (std::holds_alternative<ConflictResolution::UseMine>(resolution))
            {
                notebookPromise->finish();
                return;
            }

            if (std::holds_alternative<
                    ConflictResolution::MoveMine<qevercloud::Notebook>>(
                    resolution))
            {
                const auto & mineResolution = std::get<
                    ConflictResolution::MoveMine<qevercloud::Notebook>>(
                    resolution);

                auto updateLocalNotebookFuture =
                    m_localStorage->putNotebook(mineResolution.mine);

                threading::thenOrFailed(
                    std::move(updateLocalNotebookFuture),
                    m_threadPool.get(), notebookPromise,
                    threading::TrackedTask{
                        selfWeak,
                        [this, selfWeak, notebookPromise, notebookCounters,
                         updatedNotebook =
                             std::move(updatedNotebook)]() mutable {
                            auto putNotebookFuture =
                                m_localStorage->putNotebook(
                                    std::move(updatedNotebook));

                            auto thenFuture = threading::then(
                                std::move(putNotebookFuture),
                                m_threadPool.get(),
                                [notebookPromise, notebookCounters]() mutable {
                                    notebookCounters->onAddedNotebook();
                                    notebookPromise->finish();
                                });

                            threading::onFailed(
                                std::move(thenFuture),
                                [notebookPromise](const QException & e) {
                                    notebookPromise->setException(e);
                                    notebookPromise->finish();
                                });
                        }});
            }
        });
}

} // namespace quentier::synchronization
