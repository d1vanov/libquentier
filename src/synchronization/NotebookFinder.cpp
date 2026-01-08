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

#include "NotebookFinder.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/OperationCanceled.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

#include <QThread>

#include <utility>

namespace quentier::synchronization {

NotebookFinder::NotebookFinder(local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{
            QStringLiteral("NotebookFinder ctor: local storage is null")}};
    }
}

void NotebookFinder::init()
{
    const auto selfWeak = weak_from_this();
    const auto * localStorageNotifier = m_localStorage->notifier();

    m_localStorageConnections << QObject::connect(
        localStorageNotifier, &local_storage::ILocalStorageNotifier::notePut,
        localStorageNotifier, [this, selfWeak](const qevercloud::Note & note) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeCachedNotebookByNoteLocalId(note.localId());

            if (note.guid()) {
                removeCachedNotebookByNoteGuid(*note.guid());
            }
        });

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::noteUpdated,
        localStorageNotifier,
        [this, selfWeak](
            const qevercloud::Note & note,
            [[maybe_unused]] const local_storage::ILocalStorage::
                UpdateNoteOptions & options) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeCachedNotebookByNoteLocalId(note.localId());

            if (note.guid()) {
                removeCachedNotebookByNoteGuid(*note.guid());
            }
        });

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::noteExpunged,
        localStorageNotifier, [this, selfWeak](const QString & noteLocalId) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeCachedNotebookByNoteLocalId(noteLocalId);

            // As we don't know for sure which note was expunged,
            // will clear the cache of notebook searches by note guid
            // just in case
            // FIXME: think of some more efficient approach
            {
                QMutexLocker locker{&m_notebooksByNoteGuidMutex};
                m_notebooksByNoteGuid.clear();
            }
        });

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::notebookPut,
        localStorageNotifier,
        [this, selfWeak](const qevercloud::Notebook & notebook) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeCachedNotebooksByNotebookLocalId(notebook.localId());
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

            removeCachedNotebooksByNotebookLocalId(notebookLocalId);
        });
}

NotebookFinder::~NotebookFinder()
{
    for (const auto & connection: std::as_const(m_localStorageConnections)) {
        QObject::disconnect(connection);
    }
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByNoteLocalId(const QString & noteLocalId)
{
    {
        const QMutexLocker locker{&m_notebooksByNoteLocalIdMutex};
        if (const auto it = m_notebooksByNoteLocalId.find(noteLocalId);
            it != m_notebooksByNoteLocalId.end())
        {
            return threading::makeReadyFuture(it.value());
        }
    }

    return findNotebookByNoteLocalIdImpl(noteLocalId);
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByNoteGuid(const qevercloud::Guid & noteGuid)
{
    {
        const QMutexLocker locker{&m_notebooksByNoteGuidMutex};
        if (const auto it = m_notebooksByNoteGuid.find(noteGuid);
            it != m_notebooksByNoteGuid.end())
        {
            return threading::makeReadyFuture(it.value());
        }
    }

    return findNotebookByNoteGuidImpl(noteGuid);
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByLocalId(const QString & notebookLocalId)
{
    {
        const QMutexLocker locker{&m_notebooksByLocalIdMutex};
        if (const auto it = m_notebooksByLocalId.find(notebookLocalId);
            it != m_notebooksByLocalId.end())
        {
            return threading::makeReadyFuture(it.value());
        }
    }

    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::Notebook>>>();
    auto future = promise->future();
    promise->start();

    auto localStorageFuture =
        m_localStorage->findNotebookByLocalId(notebookLocalId);

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(localStorageFuture), currentThread, promise,
        [this, promise, notebookLocalId,
         selfWeak](std::optional<qevercloud::Notebook> notebook) {
            if (const auto self = selfWeak.lock()) {
                const QMutexLocker locker{&m_notebooksByLocalIdMutex};
                m_notebooksByLocalId[notebookLocalId] = notebook;
            }

            promise->addResult(std::move(notebook));
            promise->finish();
        });

    return future;
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByNoteLocalIdImpl(const QString & noteLocalId)
{
    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::Notebook>>>();

    auto future = promise->future();
    promise->start();

    auto noteFuture = m_localStorage->findNoteByLocalId(
        noteLocalId, local_storage::ILocalStorage::FetchNoteOptions{});

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(noteFuture), currentThread, promise,
        [this, selfWeak, promise,
         noteLocalId](const std::optional<qevercloud::Note> & note) {
            if (Q_UNLIKELY(!note)) {
                QNDEBUG(
                    "synchronization::NotebookFinder",
                    "Could not find note by local id in the local storage: "
                        << noteLocalId);

                if (const auto self = selfWeak.lock()) {
                    const QMutexLocker locker{&m_notebooksByNoteLocalIdMutex};
                    m_notebooksByNoteLocalId[noteLocalId] = std::nullopt;
                }

                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            if (const auto self = selfWeak.lock()) {
                onNoteFound(*note, promise);
            }
            else {
                promise->setException(OperationCanceled{});
                promise->finish();
            }
        });

    return future;
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByNoteGuidImpl(
        const qevercloud::Guid & noteGuid)
{
    auto promise =
        std::make_shared<QPromise<std::optional<qevercloud::Notebook>>>();

    auto future = promise->future();
    promise->start();

    auto noteFuture = m_localStorage->findNoteByGuid(
        noteGuid, local_storage::ILocalStorage::FetchNoteOptions{});

    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    threading::thenOrFailed(
        std::move(noteFuture), currentThread, promise,
        [this, selfWeak, promise,
         noteGuid](const std::optional<qevercloud::Note> & note) {
            if (Q_UNLIKELY(!note)) {
                QNDEBUG(
                    "synchronization::NotebookFinder",
                    "Could not find note by guid in the local storage: "
                        << noteGuid);

                if (const auto self = selfWeak.lock()) {
                    const QMutexLocker locker{&m_notebooksByNoteGuidMutex};
                    m_notebooksByNoteGuid[noteGuid] = std::nullopt;
                }

                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            if (const auto self = selfWeak.lock()) {
                onNoteFound(*note, promise);
            }
            else {
                promise->setException(OperationCanceled{});
                promise->finish();
            }
        });

    return future;
}

void NotebookFinder::onNoteFound(
    const qevercloud::Note & note,
    const std::shared_ptr<QPromise<std::optional<qevercloud::Notebook>>> &
        promise)
{
    const auto selfWeak = weak_from_this();
    auto * currentThread = QThread::currentThread();

    auto notebookFuture = findNotebookByLocalId(note.notebookLocalId());
    threading::thenOrFailed(
        std::move(notebookFuture), currentThread, promise,
        [this, promise, selfWeak, noteLocalId = note.localId(),
         noteGuid = note.guid(), notebookLocalId = note.notebookLocalId()](
            std::optional<qevercloud::Notebook> notebook) {
            if (const auto self = selfWeak.lock()) {
                {
                    const QMutexLocker locker{&m_notebooksByNoteLocalIdMutex};
                    m_notebooksByNoteLocalId[noteLocalId] = notebook;
                }

                if (noteGuid) {
                    const QMutexLocker locker{&m_notebooksByNoteGuidMutex};
                    m_notebooksByNoteGuid[*noteGuid] = notebook;
                }

                {
                    const QMutexLocker locker{&m_notebooksByLocalIdMutex};
                    m_notebooksByLocalId[notebookLocalId] = notebook;
                }
            }

            promise->addResult(std::move(notebook));
            promise->finish();
        });
}

void NotebookFinder::removeCachedNotebookByNoteLocalId(
    const QString & noteLocalId)
{
    const QMutexLocker locker{&m_notebooksByNoteLocalIdMutex};
    if (const auto it = m_notebooksByNoteLocalId.constFind(noteLocalId);
        it != m_notebooksByNoteLocalId.constEnd())
    {
        m_notebooksByNoteLocalId.erase(it);
    }
}

void NotebookFinder::removeCachedNotebookByNoteGuid(
    const qevercloud::Guid & noteGuid)
{
    const QMutexLocker locker{&m_notebooksByNoteGuidMutex};
    if (const auto it = m_notebooksByNoteGuid.constFind(noteGuid);
        it != m_notebooksByNoteGuid.constEnd())
    {
        m_notebooksByNoteGuid.erase(it);
    }
}

void NotebookFinder::removeCachedNotebooksByNotebookLocalId(
    const QString & notebookLocalId)
{
    {
        const QMutexLocker locker{&m_notebooksByLocalIdMutex};
        if (const auto it = m_notebooksByLocalId.constFind(notebookLocalId);
            it != m_notebooksByLocalId.constEnd())
        {
            m_notebooksByLocalId.erase(it);
        }
    }

    const auto shouldKeepNotebook =
        [&notebookLocalId](
            const std::optional<qevercloud::Notebook> & notebook) {
            if (!notebook) {
                return true;
            }

            return notebook->localId() != notebookLocalId;
        };

    {
        const QMutexLocker locker{&m_notebooksByNoteLocalIdMutex};
        for (auto it = m_notebooksByNoteLocalId.begin();
             it != m_notebooksByNoteLocalId.end();)
        {
            if (shouldKeepNotebook(it.value())) {
                ++it;
                continue;
            }

            it = m_notebooksByNoteLocalId.erase(it);
        }
    }

    {
        const QMutexLocker locker{&m_notebooksByNoteGuidMutex};
        for (auto it = m_notebooksByNoteGuid.begin();
             it != m_notebooksByNoteGuid.end();)
        {
            if (shouldKeepNotebook(it.value())) {
                ++it;
                continue;
            }

            it = m_notebooksByNoteGuid.erase(it);
        }
    }
}

} // namespace quentier::synchronization
