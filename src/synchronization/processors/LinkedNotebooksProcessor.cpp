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

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>

#include <qevercloud/types/SyncChunk.h>

#include <QMutex>
#include <QMutexLocker>

namespace quentier::synchronization {

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
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QNDEBUG(
        "synchronization::LinkedNotebooksProcessor",
        "LinkedNotebooksProcessor::processLinkedNotebooks");

    QList<qevercloud::LinkedNotebook> linkedNotebooks;
    QList<qevercloud::Guid> expungedLinkedNotebooks;
    for (const auto & syncChunk: qAsConst(syncChunks)) {
        linkedNotebooks << collectLinkedNotebooks(syncChunk);
        expungedLinkedNotebooks
            << collectExpungedLinkedNotebookGuids(syncChunk);
    }

    utils::filterOutExpungedItems(expungedLinkedNotebooks, linkedNotebooks);

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

        threading::thenOrFailed(
            std::move(putLinkedNotebookFuture), linkedNotebookPromise);
    }

    for (const auto & guid: qAsConst(expungedLinkedNotebooks)) {
        auto linkedNotebookPromise = std::make_shared<QPromise<void>>();
        linkedNotebookFutures << linkedNotebookPromise->future();
        linkedNotebookPromise->start();

        auto expungeLinkedNotebookFuture =
            m_localStorage->expungeLinkedNotebookByGuid(guid);
    }

    return threading::whenAll(std::move(linkedNotebookFutures));
}

QList<qevercloud::LinkedNotebook>
    LinkedNotebooksProcessor::collectLinkedNotebooks(
        const qevercloud::SyncChunk & syncChunk) const
{
    if (!syncChunk.linkedNotebooks() || syncChunk.linkedNotebooks()->isEmpty())
    {
        return {};
    }

    QList<qevercloud::LinkedNotebook> linkedNotebooks;
    linkedNotebooks.reserve(syncChunk.linkedNotebooks()->size());
    for (const auto & linkedNotebook: qAsConst(*syncChunk.linkedNotebooks())) {
        if (Q_UNLIKELY(!linkedNotebook.guid())) {
            QNWARNING(
                "synchronization::LinkedNotebooksProcessor",
                "Detected linked notebook without guid, skipping it: "
                    << linkedNotebook);
            continue;
        }

        if (Q_UNLIKELY(!linkedNotebook.updateSequenceNum())) {
            QNWARNING(
                "synchronization::LinkedNotebooksProcessor",
                "Detected linked notebook without update sequence number, "
                    << "skipping it: " << linkedNotebook);
            continue;
        }

        linkedNotebooks << linkedNotebook;
    }

    return linkedNotebooks;
}

QList<qevercloud::Guid>
    LinkedNotebooksProcessor::collectExpungedLinkedNotebookGuids(
        const qevercloud::SyncChunk & syncChunk) const
{
    return syncChunk.expungedLinkedNotebooks().value_or(
        QList<qevercloud::Guid>{});
}

} // namespace quentier::synchronization
