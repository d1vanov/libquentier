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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/ISyncConflictResolver.h>
#include <quentier/threading/QtFutureContinuations.h>

namespace quentier::synchronization {

NotebooksProcessor::NotebooksProcessor(
    local_storage::ILocalStoragePtr localStorage,
    ISyncConflictResolverPtr syncConflictResolver) :
    m_localStorage{std::move(localStorage)},
    m_syncConflictResolver{std::move(syncConflictResolver)}
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
}

QFuture<void> NotebooksProcessor::processNotebooks(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG(
        "synchronization::NotebooksProcessor",
        "NotebooksProcessor::processNotebooks");

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    QList<qevercloud::Notebook> notebooks;
    QList<qevercloud::Guid> expungedNotebooks;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        if (syncChunk.notebooks() && !syncChunk.notebooks()->isEmpty()) {
            for (const auto & notebook: qAsConst(*syncChunk.notebooks())) {
                if (Q_UNLIKELY(!notebook.guid())) {
                    QNWARNING(
                        "synchronization::NotebooksProcessor",
                        "Detected notebook without guid, skipping it: "
                            << notebook);
                    continue;
                }

                if (Q_UNLIKELY(!notebook.updateSequenceNum())) {
                    QNWARNING(
                        "synchronization::NotebooksProcessor",
                        "Detected notebook without update sequence number, "
                            << "skipping it: " << notebook);
                    continue;
                }

                notebooks << notebook;
            }
        }

        if (syncChunk.expungedNotebooks() &&
            !syncChunk.expungedNotebooks()->isEmpty()) {
            expungedNotebooks << *syncChunk.expungedNotebooks();
        }
    }

    if (notebooks.isEmpty() && expungedNotebooks.isEmpty()) {
        QNDEBUG(
            "synchronization::NotebooksProcessor",
            "No new/updated/expunged notebooks in the sync chunks");

        promise->finish();
        return future;
    }

    const auto selfWeak = weak_from_this();
    QList<QFuture<void>> notebookFutures;

    for (const auto & notebook: qAsConst(notebooks)) {
        auto notebookPromise = std::make_shared<QPromise<void>>();
        notebookFutures << notebookPromise->future();
        notebookPromise->start();

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
                    onFoundDuplicateByGuid(
                        notebookPromise, std::move(updatedNotebook), *notebook);
                    return;
                }

                // TODO: continue from here
            });
    }

    // TODO: implement further

    return future;
}

void NotebooksProcessor::onFoundDuplicateByGuid(
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

                threading::thenOrFailed(
                    std::move(putNotebookFuture), notebookPromise);

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
                const auto mineResolution = std::get<
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
                            [notebookPromise]() mutable {
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
