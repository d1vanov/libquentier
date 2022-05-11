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

#include "LinkedNotebooksProcessor.h"
#include "Utils.h"

#include <synchronization/SyncChunksDataCounters.h>
#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/SyncChunk.h>

#include <algorithm>

namespace quentier::synchronization {

LinkedNotebooksProcessor::LinkedNotebooksProcessor(
    local_storage::ILocalStoragePtr localStorage,
    SyncChunksDataCountersPtr syncChunksDataCounters) :
    m_localStorage{std::move(localStorage)},
    m_syncChunksDataCounters{std::move(syncChunksDataCounters)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::LinkedNotebooksProcessor",
            "LinkedNotebooksProcessor ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_syncChunksDataCounters)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::LinkedNotebooksProcessor",
            "LinkedNotebooksProcessor ctor: sync chuks data counters is "
            "null")}};
    }
}

QFuture<void> LinkedNotebooksProcessor::processLinkedNotebooks(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG(
        "synchronization::LinkedNotebooksProcessor",
        "LinkedNotebooksProcessor::processLinkedNotebooks");

    QList<qevercloud::LinkedNotebook> linkedNotebooks;
    QList<qevercloud::Guid> expungedLinkedNotebooks;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        linkedNotebooks << utils::collectLinkedNotebooksFromSyncChunk(
            syncChunk);

        expungedLinkedNotebooks
            << utils::collectExpungedLinkedNotebookGuidsFromSyncChunk(
                   syncChunk);
    }

    utils::filterOutExpungedItems(expungedLinkedNotebooks, linkedNotebooks);

    m_syncChunksDataCounters->m_totalLinkedNotebooks =
        static_cast<quint64>(std::max(linkedNotebooks.size(), 0));

    m_syncChunksDataCounters->m_totalExpungedLinkedNotebooks =
        static_cast<quint64>(std::max(expungedLinkedNotebooks.size(), 0));

    if (linkedNotebooks.isEmpty() && expungedLinkedNotebooks.isEmpty()) {
        QNDEBUG(
            "synchronization::LinkedNotebooksProcessor",
            "No new/updated/expunged linked notebooks in the sync chunks");

        return threading::makeReadyFuture();
    }

    const int totalItemCount =
        linkedNotebooks.size() + expungedLinkedNotebooks.size();

    const auto selfWeak = weak_from_this();
    QList<QFuture<void>> linkedNotebookFutures;
    linkedNotebookFutures.reserve(totalItemCount);

    for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
        auto linkedNotebookPromise = std::make_shared<QPromise<void>>();
        linkedNotebookFutures << linkedNotebookPromise->future();
        linkedNotebookPromise->start();

        // NOTE: won't search for local duplicates in order to resolve potential
        // conflict between local and remote linked notebooks. Linked notebook
        // is essentially just a pointer to a notebook in someone else's account
        // so it makes little sense to resolve the conflict in any other way
        // than having a remote linked notebook always override the local one.
        auto putLinkedNotebookFuture =
            m_localStorage->putLinkedNotebook(linkedNotebook);

        auto thenFuture = threading::then(
            std::move(putLinkedNotebookFuture),
            threading::TrackedTask{
                selfWeak, [this] {
                    ++m_syncChunksDataCounters->m_updatedLinkedNotebooks;
                }});

        threading::thenOrFailed(
            std::move(thenFuture), std::move(linkedNotebookPromise));
    }

    for (const auto & guid: qAsConst(expungedLinkedNotebooks)) {
        auto linkedNotebookPromise = std::make_shared<QPromise<void>>();
        linkedNotebookFutures << linkedNotebookPromise->future();
        linkedNotebookPromise->start();

        auto expungeLinkedNotebookFuture =
            m_localStorage->expungeLinkedNotebookByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeLinkedNotebookFuture),
            threading::TrackedTask{
                selfWeak, [this] {
                    ++m_syncChunksDataCounters->m_expungedLinkedNotebooks;
                }});

        threading::thenOrFailed(
            std::move(thenFuture), std::move(linkedNotebookPromise));
    }

    return threading::whenAll(std::move(linkedNotebookFutures));
}

} // namespace quentier::synchronization
