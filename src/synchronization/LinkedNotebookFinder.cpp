/*
 * Copyright 2023 Dmitry Ivanov
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

#include "LinkedNotebookFinder.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/TrackedTask.h>

namespace quentier::synchronization {

namespace {

[[nodiscard]] bool isLinkedNotebookFutureValid(
    const QFuture<std::optional<qevercloud::LinkedNotebook>> & future)
{
    if (!future.isFinished()) {
        return true;
    }

    if (future.resultCount() != 1) {
        return false;
    }

    try {
        Q_UNUSED(future.result());
    }
    catch (...) {
        return false;
    }

    return true;
}

} // namespace

LinkedNotebookFinder::LinkedNotebookFinder(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "LinkedNotebookFinder ctor: local storage is null")}};
    }
}

void LinkedNotebookFinder::init()
{
    const auto selfWeak = weak_from_this();
    const auto * localStorageNotifier = m_localStorage->notifier();

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::notebookPut,
        localStorageNotifier,
        [this, selfWeak](const qevercloud::Notebook & notebook) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeFutureByNotebookLocalId(notebook.localId());

            if (notebook.guid()) {
                removeFutureByNotebookGuid(*notebook.guid());
            }
        });

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::notebookExpunged,
        localStorageNotifier,
        [this, selfWeak](const QString & notebookLocalId) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeFutureByNotebookLocalId(notebookLocalId);

            std::optional<qevercloud::Guid> notebookGuid;
            {
                const QMutexLocker locker{&m_notebookGuidsByLocalIdsMutex};
                const auto it =
                    m_notebookGuidsByLocalIds.constFind(notebookLocalId);
                if (it != m_notebookGuidsByLocalIds.constEnd()) {
                    notebookGuid = it.value();
                    m_notebookGuidsByLocalIds.erase(it);
                }
            }

            if (notebookGuid) {
                removeFutureByNotebookGuid(*notebookGuid);
            }
        });

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::linkedNotebookPut,
        localStorageNotifier,
        [this, selfWeak](const qevercloud::LinkedNotebook & linkedNotebook) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            const auto & linkedNotebookGuid = linkedNotebook.guid();
            if (!linkedNotebookGuid) {
                return;
            }

            removeFuturesByLinkedNotebookGuid(*linkedNotebookGuid);
        });

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::linkedNotebookExpunged,
        localStorageNotifier,
        [this, selfWeak](const qevercloud::Guid & linkedNotebookGuid) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeFuturesByLinkedNotebookGuid(linkedNotebookGuid);
        });
}

LinkedNotebookFinder::~LinkedNotebookFinder()
{
    for (const auto & connection: qAsConst(m_localStorageConnections)) {
        QObject::disconnect(connection);
    }
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByNotebookLocalId(
        const QString & notebookLocalId)
{
    const QMutexLocker locker{&m_linkedNotebooksByNotebookLocalIdMutex};
    auto it = m_linkedNotebooksByNotebookLocalId.find(notebookLocalId);
    if (it != m_linkedNotebooksByNotebookLocalId.end() &&
        isLinkedNotebookFutureValid(it.value()))
    {
        return it.value();
    }

    it = m_linkedNotebooksByNotebookLocalId.insert(
        notebookLocalId,
        findLinkedNotebookByNotebookLocalIdImpl(notebookLocalId));
    return it.value();
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByNotebookGuid(
        const qevercloud::Guid & notebookGuid)
{
    const QMutexLocker locker{&m_linkedNotebooksByNotebookGuidMutex};
    auto it = m_linkedNotebooksByNotebookGuid.find(notebookGuid);
    if (it != m_linkedNotebooksByNotebookGuid.end() &&
        isLinkedNotebookFutureValid(it.value()))
    {
        return it.value();
    }

    it = m_linkedNotebooksByNotebookGuid.insert(
        notebookGuid, findLinkedNotebookByNotebookGuidImpl(notebookGuid));
    return it.value();
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByGuid(
        const qevercloud::Guid & guid)
{
    const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};
    auto it = m_linkedNotebooksByGuid.find(guid);
    if (it != m_linkedNotebooksByGuid.end() &&
        isLinkedNotebookFutureValid(it.value()))
    {
        return it.value();
    }

    it = m_linkedNotebooksByGuid.insert(
        guid, m_localStorage->findLinkedNotebookByGuid(guid));
    return it.value();
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByNotebookLocalIdImpl(
        const QString & notebookLocalId)
{
    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::LinkedNotebook>>>();

    auto future = promise->future();
    promise->start();

    auto notebookFuture =
        m_localStorage->findNotebookByLocalId(notebookLocalId);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(notebookFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, notebookLocalId](
                const std::optional<qevercloud::Notebook> & notebook) {
                if (Q_UNLIKELY(!notebook)) {
                    QNDEBUG(
                        "synchronization::LinkedNotebookFinder",
                        "Could not find notebook by local id in the local "
                        "storage: "
                            << notebookLocalId);
                    promise->addResult(std::nullopt);
                    promise->finish();
                    return;
                }

                onNotebookFound(*notebook, promise);
            }});

    return future;
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByNotebookGuidImpl(
        const qevercloud::Guid & notebookGuid)
{
    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::LinkedNotebook>>>();

    auto future = promise->future();
    promise->start();

    auto notebookFuture = m_localStorage->findNotebookByGuid(notebookGuid);

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(notebookFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, notebookGuid](
                const std::optional<qevercloud::Notebook> & notebook) {
                if (Q_UNLIKELY(!notebook)) {
                    QNDEBUG(
                        "synchronization::LinkedNotebookFinder",
                        "Could not find notebook by guid in the local storage: "
                            << notebookGuid);
                    promise->addResult(std::nullopt);
                    promise->finish();
                    return;
                }

                onNotebookFound(*notebook, promise);
            }});

    return future;
}

void LinkedNotebookFinder::onNotebookFound(
    const qevercloud::Notebook & notebook,
    const std::shared_ptr<QPromise<std::optional<qevercloud::LinkedNotebook>>> &
        promise)
{
    {
        const QMutexLocker locker{&m_notebookGuidsByLocalIdsMutex};
        m_notebookGuidsByLocalIds[notebook.localId()] = notebook.guid();
    }

    if (!notebook.linkedNotebookGuid()) {
        promise->addResult(std::nullopt);
        promise->finish();
        return;
    }

    auto linkedNotebookGuid = *notebook.linkedNotebookGuid();

    QFuture<std::optional<qevercloud::LinkedNotebook>> linkedNotebookFuture =
        [&] {
            const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};

            auto it = m_linkedNotebooksByGuid.find(linkedNotebookGuid);
            if (it != m_linkedNotebooksByGuid.end() &&
                isLinkedNotebookFutureValid(it.value()))
            {
                return it.value();
            }

            it = m_linkedNotebooksByGuid.insert(
                linkedNotebookGuid,
                m_localStorage->findLinkedNotebookByGuid(linkedNotebookGuid));
            return it.value();
        }();

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, linkedNotebookGuid = std::move(linkedNotebookGuid)](
                std::optional<qevercloud::LinkedNotebook> linkedNotebook) {
                if (Q_UNLIKELY(!linkedNotebook)) {
                    QNDEBUG(
                        "synchronization::LinkedNotebookFinder",
                        "Could not find linked notebook by guid in the "
                            << "local storage: linked notebook guid = "
                            << linkedNotebookGuid);
                    promise->addResult(std::nullopt);
                    promise->finish();
                    return;
                }

                {
                    const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};

                    m_linkedNotebooksByGuid[linkedNotebookGuid] =
                        threading::makeReadyFuture<
                            std::optional<qevercloud::LinkedNotebook>>(
                            linkedNotebook);
                }

                promise->addResult(std::move(linkedNotebook));
                promise->finish();
            }});
}

void LinkedNotebookFinder::removeFutureByNotebookLocalId(
    const QString & notebookLocalId)
{
    const QMutexLocker locker{&m_linkedNotebooksByNotebookLocalIdMutex};
    const auto it =
        m_linkedNotebooksByNotebookLocalId.constFind(notebookLocalId);
    if (it != m_linkedNotebooksByNotebookLocalId.constEnd()) {
        m_linkedNotebooksByNotebookLocalId.erase(it);
    }
}

void LinkedNotebookFinder::removeFutureByNotebookGuid(
    const qevercloud::Guid & notebookGuid)
{
    const QMutexLocker locker{&m_linkedNotebooksByNotebookGuidMutex};
    const auto it = m_linkedNotebooksByNotebookGuid.constFind(notebookGuid);
    if (it != m_linkedNotebooksByNotebookGuid.constEnd()) {
        m_linkedNotebooksByNotebookGuid.erase(it);
    }
}

void LinkedNotebookFinder::removeFuturesByLinkedNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid)
{
    {
        const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};
        auto it = m_linkedNotebooksByGuid.constFind(linkedNotebookGuid);
        if (it != m_linkedNotebooksByGuid.constEnd()) {
            m_linkedNotebooksByGuid.erase(it);
        }
    }

    QStringList removedNotebookLocalIds;
    {
        const QMutexLocker locker{&m_linkedNotebooksByNotebookLocalIdMutex};
        for (auto it = m_linkedNotebooksByNotebookLocalId.begin();
             it != m_linkedNotebooksByNotebookLocalId.end();)
        {
            if (!it.value().isFinished()) {
                ++it;
                continue;
            }

            if (Q_UNLIKELY(it.value().resultCount() != 1)) {
                ++it;
                continue;
            }

            std::optional<qevercloud::LinkedNotebook> linkedNotebook;
            try {
                linkedNotebook = it.value().result();
            }
            catch (...) {
            }

            if (!linkedNotebook) {
                ++it;
                continue;
            }

            if (linkedNotebook->guid() == linkedNotebookGuid) {
                removedNotebookLocalIds << it.key();
                it = m_linkedNotebooksByNotebookLocalId.erase(it);
                continue;
            }

            ++it;
        }
    }

    QList<qevercloud::Guid> removedNotebookGuids;
    if (!removedNotebookLocalIds.isEmpty()) {
        const QMutexLocker locker{&m_notebookGuidsByLocalIdsMutex};
        for (const auto & notebookLocalId: qAsConst(removedNotebookLocalIds)) {
            const auto it =
                m_notebookGuidsByLocalIds.constFind(notebookLocalId);
            if (it != m_notebookGuidsByLocalIds.constEnd()) {
                if (it.value()) {
                    removedNotebookGuids << *it.value();
                }

                m_notebookGuidsByLocalIds.erase(it);
            }
        }
    }

    {
        const QMutexLocker locker{&m_linkedNotebooksByNotebookGuidMutex};
        for (auto it = m_linkedNotebooksByNotebookGuid.begin();
             it != m_linkedNotebooksByNotebookGuid.end();)
        {
            if (removedNotebookGuids.contains(it.key())) {
                it = m_linkedNotebooksByNotebookGuid.erase(it);
                continue;
            }

            if (!it.value().isFinished()) {
                ++it;
                continue;
            }

            if (Q_UNLIKELY(it.value().resultCount() != 1)) {
                ++it;
                continue;
            }

            std::optional<qevercloud::LinkedNotebook> linkedNotebook;
            try {
                linkedNotebook = it.value().result();
            }
            catch (...) {
            }

            if (!linkedNotebook) {
                ++it;
                continue;
            }

            if (linkedNotebook->guid() == linkedNotebookGuid) {
                it = m_linkedNotebooksByNotebookGuid.erase(it);
                continue;
            }

            ++it;
        }
    }
}

} // namespace quentier::synchronization
