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

#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/SyncChunk.h>

#include <algorithm>
#include <atomic>

namespace quentier::synchronization {

namespace {

class LinkedNotebookCounters
{
public:
    LinkedNotebookCounters(
        const qint32 totalLinkedNotebooks, // NOLINT
        const qint32 totalLinkedNotebooksToExpunge, // NOLINT
        ILinkedNotebooksProcessor::ICallbackWeakPtr callbackWeak) :
        m_totalLinkedNotebooks{totalLinkedNotebooks},
        m_totalLinkedNotebooksToExpunge{totalLinkedNotebooksToExpunge},
        m_callbackWeak{std::move(callbackWeak)}
    {}

    void onProcessedLinkedNotebook()
    {
        m_processedLinkedNotebooks.fetch_add(1, std::memory_order_acq_rel);
        notifyUpdate();
    }

    void onExpungedLinkedNotebook()
    {
        m_expungedLinkedNotebooks.fetch_add(1, std::memory_order_acq_rel);
        notifyUpdate();
    }

private:
    void notifyUpdate()
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebooksProcessingProgress(
                m_totalLinkedNotebooks, m_totalLinkedNotebooksToExpunge,
                m_processedLinkedNotebooks.load(std::memory_order_acquire),
                m_expungedLinkedNotebooks.load(std::memory_order_acquire));
        }
    }

private:
    const qint32 m_totalLinkedNotebooks;
    const qint32 m_totalLinkedNotebooksToExpunge;
    const ILinkedNotebooksProcessor::ICallbackWeakPtr m_callbackWeak;

    std::atomic<qint32> m_processedLinkedNotebooks{0};
    std::atomic<qint32> m_expungedLinkedNotebooks{0};
};

} // namespace

LinkedNotebooksProcessor::LinkedNotebooksProcessor(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QT_TRANSLATE_NOOP(
            "synchronization::LinkedNotebooksProcessor",
            "LinkedNotebooksProcessor ctor: local storage is null")}};
    }
}

QFuture<void> LinkedNotebooksProcessor::processLinkedNotebooks(
    const QList<qevercloud::SyncChunk> & syncChunks,
    ICallbackWeakPtr callbackWeak)
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

    if (linkedNotebooks.isEmpty() && expungedLinkedNotebooks.isEmpty()) {
        QNDEBUG(
            "synchronization::LinkedNotebooksProcessor",
            "No new/updated/expunged linked notebooks in the sync chunks");

        return threading::makeReadyFuture();
    }

    const qint32 totalLinkedNotebooks = linkedNotebooks.size();
    const qint32 totalExpungedLinkedNotebooks = expungedLinkedNotebooks.size();

    const qint32 totalItemCount =
        totalLinkedNotebooks + totalExpungedLinkedNotebooks;

    QList<QFuture<void>> linkedNotebookFutures;
    linkedNotebookFutures.reserve(totalItemCount);

    const auto linkedNotebookCounters =
        std::make_shared<LinkedNotebookCounters>(
            totalLinkedNotebooks, totalExpungedLinkedNotebooks,
            std::move(callbackWeak));

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
            std::move(putLinkedNotebookFuture), [linkedNotebookCounters] {
                linkedNotebookCounters->onProcessedLinkedNotebook();
            });

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
            std::move(expungeLinkedNotebookFuture), [linkedNotebookCounters] {
                linkedNotebookCounters->onExpungedLinkedNotebook();
            });

        threading::thenOrFailed(
            std::move(thenFuture), std::move(linkedNotebookPromise));
    }

    return threading::whenAll(std::move(linkedNotebookFutures));
}

} // namespace quentier::synchronization
