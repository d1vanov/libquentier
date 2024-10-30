/*
 * Copyright 2023-2024 Dmitry Ivanov
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
#include <quentier/exception/OperationCanceled.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>
#include <quentier/threading/TrackedTask.h>

#include <QThread>

#include <utility>

namespace quentier::synchronization {

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

            removeCachedLinkedNotebookByNotebookLocalId(notebook.localId());

            if (notebook.guid()) {
                removeCachedLinkedNotebookByNotebookGuid(*notebook.guid());
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

            removeCachedLinkedNotebookByNotebookLocalId(notebookLocalId);

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
                removeCachedLinkedNotebookByNotebookGuid(*notebookGuid);
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

            removeCachedLinkedNotebookByLinkedNotebookGuid(*linkedNotebookGuid);
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

            removeCachedLinkedNotebookByLinkedNotebookGuid(linkedNotebookGuid);
        });
}

LinkedNotebookFinder::~LinkedNotebookFinder()
{
    for (const auto & connection: std::as_const(m_localStorageConnections)) {
        QObject::disconnect(connection);
    }
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByNotebookLocalId(
        const QString & notebookLocalId)
{
    {
        const QMutexLocker locker{&m_linkedNotebooksByNotebookLocalIdMutex};
        if (const auto it =
                m_linkedNotebooksByNotebookLocalId.find(notebookLocalId);
            it != m_linkedNotebooksByNotebookLocalId.end())
        {
            return threading::makeReadyFuture(it.value());
        }
    }

    return findLinkedNotebookByNotebookLocalIdImpl(notebookLocalId);
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByNotebookGuid(
        const qevercloud::Guid & notebookGuid)
{
    {
        const QMutexLocker locker{&m_linkedNotebooksByNotebookGuidMutex};
        if (const auto it = m_linkedNotebooksByNotebookGuid.find(notebookGuid);
            it != m_linkedNotebooksByNotebookGuid.end())
        {
            return threading::makeReadyFuture(it.value());
        }
    }

    return findLinkedNotebookByNotebookGuidImpl(notebookGuid);
}

QFuture<std::optional<qevercloud::LinkedNotebook>>
    LinkedNotebookFinder::findLinkedNotebookByGuid(
        const qevercloud::Guid & guid)
{
    {
        const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};
        if (const auto it = m_linkedNotebooksByGuid.find(guid);
            it != m_linkedNotebooksByGuid.end())
        {
            return threading::makeReadyFuture(it.value());
        }
    }

    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::LinkedNotebook>>>();
    auto future = promise->future();
    promise->start();

    auto localStorageFuture = m_localStorage->findLinkedNotebookByGuid(guid);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(localStorageFuture), currentThread, promise,
        [this, promise, guid,
         selfWeak](std::optional<qevercloud::LinkedNotebook> linkedNotebook) {
            if (const auto self = selfWeak.lock()) {
                const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};
                m_linkedNotebooksByGuid[guid] = linkedNotebook;
            }

            promise->addResult(std::move(linkedNotebook));
            promise->finish();
        });

    return future;
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
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(notebookFuture), currentThread, promise,
        [this, selfWeak, promise, notebookLocalId](
            const std::optional<qevercloud::Notebook> & notebook) {
            if (Q_UNLIKELY(!notebook)) {
                QNDEBUG(
                    "synchronization::LinkedNotebookFinder",
                    "Could not find notebook by local id in the local "
                        << "storage: " << notebookLocalId);

                if (const auto self = selfWeak.lock()) {
                    const QMutexLocker locker{
                        &m_linkedNotebooksByNotebookLocalIdMutex};
                    m_linkedNotebooksByNotebookLocalId[notebookLocalId] =
                        std::nullopt;
                }

                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            if (!notebook->linkedNotebookGuid()) {
                QNDEBUG(
                    "synchronization::LinkedNotebookFinder",
                    "Notebook found by local id "
                        << notebookLocalId
                        << "does not have linked notebook guid: " << *notebook);

                if (const auto self = selfWeak.lock()) {
                    const QMutexLocker locker{
                        &m_linkedNotebooksByNotebookLocalIdMutex};
                    m_linkedNotebooksByNotebookLocalId[notebookLocalId] =
                        std::nullopt;
                }

                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            if (const auto self = selfWeak.lock()) {
                onNotebookFound(*notebook, promise);
            }
            else {
                promise->setException(OperationCanceled{});
                promise->finish();
            }
        });

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
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(notebookFuture), currentThread, promise,
        [this, selfWeak, promise,
         notebookGuid](const std::optional<qevercloud::Notebook> & notebook) {
            if (Q_UNLIKELY(!notebook)) {
                QNDEBUG(
                    "synchronization::LinkedNotebookFinder",
                    "Could not find notebook by guid in the local storage: "
                        << notebookGuid);

                if (const auto self = selfWeak.lock()) {
                    const QMutexLocker locker{
                        &m_linkedNotebooksByNotebookGuidMutex};
                    m_linkedNotebooksByNotebookGuid[notebookGuid] =
                        std::nullopt;
                }

                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            if (const auto self = selfWeak.lock()) {
                const QMutexLocker locker{&m_notebookGuidsByLocalIdsMutex};
                m_notebookGuidsByLocalIds[notebook->localId()] =
                    notebook->guid();
            }

            if (!notebook->linkedNotebookGuid()) {
                QNDEBUG(
                    "synchronization::LinkedNotebookFinder",
                    "Notebook found by guid "
                        << notebookGuid << "does not "
                        << "have linked notebook guid: " << *notebook);

                if (const auto self = selfWeak.lock()) {
                    const QMutexLocker locker{
                        &m_linkedNotebooksByNotebookGuidMutex};
                    m_linkedNotebooksByNotebookGuid[notebookGuid] =
                        std::nullopt;
                }

                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            if (const auto self = selfWeak.lock()) {
                onNotebookFound(*notebook, promise);
            }
            else {
                promise->setException(OperationCanceled{});
                promise->finish();
            }
        });

    return future;
}

void LinkedNotebookFinder::onNotebookFound(
    const qevercloud::Notebook & notebook,
    const std::shared_ptr<QPromise<std::optional<qevercloud::LinkedNotebook>>> &
        promise)
{
    Q_ASSERT(notebook.linkedNotebookGuid());
    auto linkedNotebookGuid = *notebook.linkedNotebookGuid();

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto linkedNotebookFuture = findLinkedNotebookByGuid(linkedNotebookGuid);

    threading::thenOrFailed(
        std::move(linkedNotebookFuture), currentThread, promise,
        [this, selfWeak, promise,
         linkedNotebookGuid = std::move(linkedNotebookGuid),
         notebookLocalId = notebook.localId(), notebookGuid = notebook.guid()](
            std::optional<qevercloud::LinkedNotebook> linkedNotebook) {
            if (const auto self = selfWeak.lock()) {
                {
                    const QMutexLocker locker{
                        &m_linkedNotebooksByNotebookLocalIdMutex};
                    m_linkedNotebooksByNotebookLocalId[notebookLocalId] =
                        linkedNotebook;
                }

                if (notebookGuid) {
                    const QMutexLocker locker{
                        &m_linkedNotebooksByNotebookGuidMutex};
                    m_linkedNotebooksByNotebookGuid[*notebookGuid] =
                        linkedNotebook;
                }

                {
                    const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};
                    m_linkedNotebooksByGuid[linkedNotebookGuid] =
                        linkedNotebook;
                }
            }

            promise->addResult(std::move(linkedNotebook));
            promise->finish();
        });
}

void LinkedNotebookFinder::removeCachedLinkedNotebookByNotebookLocalId(
    const QString & notebookLocalId)
{
    const QMutexLocker locker{&m_linkedNotebooksByNotebookLocalIdMutex};
    if (const auto it =
            m_linkedNotebooksByNotebookLocalId.constFind(notebookLocalId);
        it != m_linkedNotebooksByNotebookLocalId.constEnd())
    {
        m_linkedNotebooksByNotebookLocalId.erase(it);
    }
}

void LinkedNotebookFinder::removeCachedLinkedNotebookByNotebookGuid(
    const qevercloud::Guid & notebookGuid)
{
    const QMutexLocker locker{&m_linkedNotebooksByNotebookGuidMutex};
    if (const auto it = m_linkedNotebooksByNotebookGuid.constFind(notebookGuid);
        it != m_linkedNotebooksByNotebookGuid.constEnd())
    {
        m_linkedNotebooksByNotebookGuid.erase(it);
    }
}

void LinkedNotebookFinder::removeCachedLinkedNotebookByLinkedNotebookGuid(
    const qevercloud::Guid & linkedNotebookGuid)
{
    {
        const QMutexLocker locker{&m_linkedNotebooksByGuidMutex};
        if (const auto it =
                m_linkedNotebooksByGuid.constFind(linkedNotebookGuid);
            it != m_linkedNotebooksByGuid.constEnd())
        {
            m_linkedNotebooksByGuid.erase(it);
        }
    }

    const auto shouldKeepLinkedNotebook =
        [&linkedNotebookGuid](
            const std::optional<qevercloud::LinkedNotebook> & linkedNotebook) {
            if (!linkedNotebook) {
                return true;
            }

            return linkedNotebook->guid() != linkedNotebookGuid;
        };

    QStringList removedNotebookLocalIds;
    {
        const QMutexLocker locker{&m_linkedNotebooksByNotebookLocalIdMutex};
        for (auto it = m_linkedNotebooksByNotebookLocalId.begin();
             it != m_linkedNotebooksByNotebookLocalId.end();)
        {
            if (shouldKeepLinkedNotebook(it.value())) {
                ++it;
                continue;
            }

            removedNotebookLocalIds << it.key();
            it = m_linkedNotebooksByNotebookLocalId.erase(it);
        }
    }

    QList<qevercloud::Guid> removedNotebookGuids;
    if (!removedNotebookLocalIds.isEmpty()) {
        const QMutexLocker locker{&m_notebookGuidsByLocalIdsMutex};
        for (const auto & notebookLocalId:
             std::as_const(removedNotebookLocalIds))
        {
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

            if (shouldKeepLinkedNotebook(it.value())) {
                ++it;
                continue;
            }

            it = m_linkedNotebooksByNotebookGuid.erase(it);
        }
    }
}

} // namespace quentier::synchronization
