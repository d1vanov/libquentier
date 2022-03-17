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

#include "NotebooksProcessor.h"
#include "Utils.h"

#include <synchronization/SyncChunksDataCounters.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/Future.h>

#include <qevercloud/types/SyncChunk.h>

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

namespace quentier::synchronization {

NotebooksProcessor::NotebooksProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver,
    SyncChunksDataCountersPtr syncChunksDataCounters) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)},
    m_syncChunksDataCounters{std::move(syncChunksDataCounters)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotebooksProcessor",
            "NotebooksProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncConflictResolver)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotebooksProcessor",
            "NotebooksProcessor ctor: sync conflict resolver is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksDataCounters)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::NotebooksProcessor",
            "NotebooksProcessor ctor: sync chuks data counters is null")}};
    }
}

QFuture<void> NotebooksProcessor::processNotebooks(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG(
        "synchronization::NotebooksProcessor",
        "NotebooksProcessor::processNotebooks");

    QList<qevercloud::Notebook> notebooks;
    QList<qevercloud::Guid> expungedNotebooks;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        notebooks << collectNotebooks(syncChunk);
        expungedNotebooks << collectExpungedNotebookGuids(syncChunk);
    }

    utils::filterOutExpungedItems(expungedNotebooks, notebooks);

    m_syncChunksDataCounters->m_totalNotebooks =
        static_cast<quint64>(std::max<int>(notebooks.size(), 0));

    m_syncChunksDataCounters->m_totalExpungedNotebooks =
        static_cast<quint64>(std::max<int>(expungedNotebooks.size(), 0));

    if (notebooks.isEmpty() && expungedNotebooks.isEmpty()) {
        QNDEBUG(
            "synchronization::NotebooksProcessor",
            "No new/updated/expunged notebooks in the sync chunks");

        return threading::makeReadyFuture();
    }

    const int totalItemCount = notebooks.size() + expungedNotebooks.size();

    const auto selfWeak = weak_from_this();
    QList<QFuture<void>> notebookFutures;
    notebookFutures.reserve(totalItemCount);

    for (const auto & notebook: qAsConst(notebooks)) {
        auto notebookPromise = std::make_shared<QPromise<void>>();
        notebookFutures << notebookPromise->future();
        notebookPromise->start();

        Q_ASSERT(notebook.guid());

        auto findNotebookByGuidFuture =
            m_localStorage->findNotebookByGuid(*notebook.guid());

        threading::thenOrFailed(
            std::move(findNotebookByGuidFuture), notebookPromise,
            [this, selfWeak, updatedNotebook = notebook, notebookPromise](
                const std::optional<qevercloud::Notebook> & notebook) mutable {
                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                if (notebook) {
                    onFoundDuplicate(
                        notebookPromise, std::move(updatedNotebook), *notebook);
                    return;
                }

                tryToFindDuplicateByName(
                    notebookPromise, std::move(updatedNotebook));
            });
    }

    for (const auto & guid: qAsConst(expungedNotebooks)) {
        auto notebookPromise = std::make_shared<QPromise<void>>();
        notebookFutures << notebookPromise->future();
        notebookPromise->start();

        auto expungeNotebookFuture =
            m_localStorage->expungeNotebookByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeNotebookFuture),
            [selfWeak]
            {
                const auto self = selfWeak.lock();
                if (!self) {
                    return;
                }

                ++self->m_syncChunksDataCounters->m_expungedNotebooks;
            });

        threading::thenOrFailed(
            std::move(thenFuture), notebookPromise);
    }

    return threading::whenAll(std::move(notebookFutures));
}

QList<qevercloud::Notebook> NotebooksProcessor::collectNotebooks(
    const qevercloud::SyncChunk & syncChunk) const
{
    if (!syncChunk.notebooks() || syncChunk.notebooks()->isEmpty()) {
        return {};
    }

    QList<qevercloud::Notebook> notebooks;
    notebooks.reserve(syncChunk.notebooks()->size());
    for (const auto & notebook: qAsConst(*syncChunk.notebooks())) {
        if (Q_UNLIKELY(!notebook.guid())) {
            QNWARNING(
                "synchronization::NotebooksProcessor",
                "Detected notebook without guid, skipping it: " << notebook);
            continue;
        }

        if (Q_UNLIKELY(!notebook.updateSequenceNum())) {
            QNWARNING(
                "synchronization::NotebooksProcessor",
                "Detected notebook without update sequence number, "
                    << "skipping it: " << notebook);
            continue;
        }

        if (Q_UNLIKELY(!notebook.name())) {
            QNWARNING(
                "synchronization::NotebooksProcessor",
                "Detected notebook without name, skipping it: " << notebook);
            continue;
        }

        notebooks << notebook;
    }

    return notebooks;
}

QList<qevercloud::Guid> NotebooksProcessor::collectExpungedNotebookGuids(
    const qevercloud::SyncChunk & syncChunk) const
{
    return syncChunk.expungedNotebooks().value_or(QList<qevercloud::Guid>{});
}

void NotebooksProcessor::tryToFindDuplicateByName(
    const std::shared_ptr<QPromise<void>> & notebookPromise,
    qevercloud::Notebook updatedNotebook)
{
    Q_ASSERT(updatedNotebook.name());

    const auto selfWeak = weak_from_this();

    auto findNotebookByNameFuture =
        m_localStorage->findNotebookByName(*updatedNotebook.name());

    threading::thenOrFailed(
        std::move(findNotebookByNameFuture), notebookPromise,
        [this, selfWeak,
        updatedNotebook = std::move(updatedNotebook),
        notebookPromise](
            const std::optional<qevercloud::Notebook> &
            notebook) mutable {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (notebook) {
                onFoundDuplicate(
                    notebookPromise, std::move(updatedNotebook),
                    *notebook);
                return;
            }

            // No duplicate by either guid or name was found,
            // just put the updated notebook to the local storage
            auto putNotebookFuture = m_localStorage->putNotebook(
                std::move(updatedNotebook));

            auto thenFuture = threading::then(
                std::move(putNotebookFuture),
                [selfWeak]
                {
                    const auto self = selfWeak.lock();
                    if (!self) {
                        return;
                    }

                    ++self->m_syncChunksDataCounters->m_addedNotebooks;
                });

            threading::thenOrFailed(
                std::move(thenFuture), notebookPromise);
        });
}

void NotebooksProcessor::onFoundDuplicate(
    const std::shared_ptr<QPromise<void>> & notebookPromise,
    qevercloud::Notebook updatedNotebook, qevercloud::Notebook localNotebook)
{
    using ConflictResolution = ISyncConflictResolver::ConflictResolution;
    using NotebookConflictResolution =
        ISyncConflictResolver::NotebookConflictResolution;

    auto statusFuture = m_syncConflictResolver->resolveNotebookConflict(
        updatedNotebook, std::move(localNotebook));

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(statusFuture), notebookPromise,
        [this, selfWeak, notebookPromise,
         updatedNotebook = std::move(updatedNotebook)](
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
                auto putNotebookFuture =
                    m_localStorage->putNotebook(std::move(updatedNotebook));

                auto thenFuture = threading::then(
                    std::move(putNotebookFuture),
                    [selfWeak]
                    {
                        const auto self = selfWeak.lock();
                        if (!self) {
                            return;
                        }

                        ++self->m_syncChunksDataCounters->m_updatedNotebooks;
                    });

                threading::thenOrFailed(
                    std::move(thenFuture), notebookPromise);

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
                    std::move(updateLocalNotebookFuture), notebookPromise,
                    [this, selfWeak, notebookPromise,
                     updatedNotebook = std::move(updatedNotebook)]() mutable {
                        const auto self = selfWeak.lock();
                        if (!self) {
                            return;
                        }

                        auto putNotebookFuture = m_localStorage->putNotebook(
                            std::move(updatedNotebook));

                        auto thenFuture = threading::then(
                            std::move(putNotebookFuture),
                            [selfWeak, notebookPromise]() mutable {
                                if (const auto self = selfWeak.lock()) {
                                    ++self->m_syncChunksDataCounters
                                          ->m_addedNotebooks;
                                }

                                notebookPromise->finish();
                            });

                        threading::onFailed(
                            std::move(thenFuture),
                            [notebookPromise](const QException & e) {
                                notebookPromise->setException(e);
                                notebookPromise->finish();
                            });
                    });
            }
        });
}

} // namespace quentier::synchronization
