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

#include "LinkedNotebooksProcessor.h"
#include "Utils.h"

#include <synchronization/sync_chunks/Utils.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Factory.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <qevercloud/types/SyncChunk.h>

#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <utility>

namespace quentier::synchronization {

namespace {

class LinkedNotebookCounters
{
public:
    LinkedNotebookCounters(
        const qint32 totalLinkedNotebooks,          // NOLINT
        const qint32 totalLinkedNotebooksToExpunge, // NOLINT
        ILinkedNotebooksProcessor::ICallbackWeakPtr callbackWeak) :
        m_totalLinkedNotebooks{totalLinkedNotebooks},
        m_totalLinkedNotebooksToExpunge{totalLinkedNotebooksToExpunge},
        m_callbackWeak{std::move(callbackWeak)}
    {}

    void onProcessedLinkedNotebook()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_processedLinkedNotebooks;
        notifyUpdate();
    }

    void onExpungedLinkedNotebook()
    {
        const QMutexLocker locker{&m_mutex};
        ++m_expungedLinkedNotebooks;
        notifyUpdate();
    }

private:
    void notifyUpdate()
    {
        if (const auto callback = m_callbackWeak.lock()) {
            callback->onLinkedNotebooksProcessingProgress(
                m_totalLinkedNotebooks, m_totalLinkedNotebooksToExpunge,
                m_processedLinkedNotebooks, m_expungedLinkedNotebooks);
        }
    }

private:
    const qint32 m_totalLinkedNotebooks;
    const qint32 m_totalLinkedNotebooksToExpunge;
    const ILinkedNotebooksProcessor::ICallbackWeakPtr m_callbackWeak;

    QMutex m_mutex;
    qint32 m_processedLinkedNotebooks{0};
    qint32 m_expungedLinkedNotebooks{0};
};

} // namespace

LinkedNotebooksProcessor::LinkedNotebooksProcessor(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
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
    for (const auto & syncChunk: std::as_const(syncChunks)) {
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

    const auto totalLinkedNotebooks = linkedNotebooks.size();
    const auto totalExpungedLinkedNotebooks = expungedLinkedNotebooks.size();

    const auto totalItemCount =
        totalLinkedNotebooks + totalExpungedLinkedNotebooks;

    QList<QFuture<void>> linkedNotebookFutures;
    linkedNotebookFutures.reserve(totalItemCount);

    const auto linkedNotebookCounters =
        std::make_shared<LinkedNotebookCounters>(
            totalLinkedNotebooks, totalExpungedLinkedNotebooks,
            std::move(callbackWeak));

    auto * currentThread = QThread::currentThread();

    for (const auto & linkedNotebook: std::as_const(linkedNotebooks)) {
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
            std::move(putLinkedNotebookFuture), currentThread,
            [linkedNotebookCounters] {
                linkedNotebookCounters->onProcessedLinkedNotebook();
            });

        threading::thenOrFailed(
            std::move(thenFuture), currentThread,
            std::move(linkedNotebookPromise));
    }

    for (const auto & guid: std::as_const(expungedLinkedNotebooks)) {
        auto linkedNotebookPromise = std::make_shared<QPromise<void>>();
        linkedNotebookFutures << linkedNotebookPromise->future();
        linkedNotebookPromise->start();

        auto expungeLinkedNotebookFuture =
            m_localStorage->expungeLinkedNotebookByGuid(guid);

        auto thenFuture = threading::then(
            std::move(expungeLinkedNotebookFuture), currentThread,
            [linkedNotebookCounters] {
                linkedNotebookCounters->onExpungedLinkedNotebook();
            });

        threading::thenOrFailed(
            std::move(thenFuture), currentThread,
            std::move(linkedNotebookPromise));
    }

    return threading::whenAll(std::move(linkedNotebookFutures));
}

} // namespace quentier::synchronization
