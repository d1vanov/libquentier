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

#include "NotebookFinder.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/QtFutureContinuations.h>

namespace quentier::synchronization {

namespace {

[[nodiscard]] bool isNotebookFutureValid(
    const QFuture<std::optional<qevercloud::Notebook>> & future)
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

            removeFutureByNoteLocalId(note.localId());

            if (note.guid()) {
                removeFutureByNoteGuid(*note.guid());
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

            removeFutureByNoteLocalId(note.localId());

            if (note.guid()) {
                removeFutureByNoteGuid(*note.guid());
            }
        });

    m_localStorageConnections << QObject::connect(
        localStorageNotifier,
        &local_storage::ILocalStorageNotifier::noteNotebookChanged,
        localStorageNotifier,
        [this, selfWeak](
            const QString & noteLocalId,
            [[maybe_unused]] const QString & previousNotebookLocalId,
            [[maybe_unused]] const QString & newNotebookLocalId) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeFutureByNoteLocalId(noteLocalId);

            // As we don't know for sure which note was changed,
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
        &local_storage::ILocalStorageNotifier::noteExpunged,
        localStorageNotifier, [this, selfWeak](const QString & noteLocalId) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            removeFutureByNoteLocalId(noteLocalId);

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

            removeFuturesByNotebookLocalId(notebook.localId());
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

            removeFuturesByNotebookLocalId(notebookLocalId);
        });
}

NotebookFinder::~NotebookFinder()
{
    for (const auto & connection: qAsConst(m_localStorageConnections)) {
        QObject::disconnect(connection);
    }
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByNoteLocalId(const QString & noteLocalId)
{
    const QMutexLocker locker{&m_notebooksByNoteLocalIdMutex};
    auto it = m_notebooksByNoteLocalId.find(noteLocalId);
    if (it != m_notebooksByNoteLocalId.end() &&
        isNotebookFutureValid(it.value())) {
        return it.value();
    }

    it = m_notebooksByNoteLocalId.insert(
        noteLocalId, findNotebookByNoteLocalIdImpl(noteLocalId));
    return it.value();
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByNoteGuid(const qevercloud::Guid & noteGuid)
{
    const QMutexLocker locker{&m_notebooksByNoteGuidMutex};
    auto it = m_notebooksByNoteGuid.find(noteGuid);
    if (it != m_notebooksByNoteGuid.end() && isNotebookFutureValid(it.value()))
    {
        return it.value();
    }

    it = m_notebooksByNoteGuid.insert(
        noteGuid, findNotebookByNoteGuidImpl(noteGuid));
    return it.value();
}

QFuture<std::optional<qevercloud::Notebook>>
    NotebookFinder::findNotebookByLocalId(const QString & notebookLocalId)
{
    const QMutexLocker locker{&m_notebooksByLocalIdMutex};
    auto it = m_notebooksByLocalId.find(notebookLocalId);
    if (it != m_notebooksByLocalId.end() && isNotebookFutureValid(it.value())) {
        return it.value();
    }

    it = m_notebooksByLocalId.insert(
        notebookLocalId,
        m_localStorage->findNotebookByLocalId(notebookLocalId));
    return it.value();
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

    threading::thenOrFailed(
        std::move(noteFuture), promise,
        [selfWeak, this, promise,
         noteLocalId](const std::optional<qevercloud::Note> & note) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (Q_UNLIKELY(!note)) {
                QNDEBUG(
                    "synchronization::NotebookFinder",
                    "Could not find note by local id in the local storage: "
                        << noteLocalId);
                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            onNoteFound(*note, promise);
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

    threading::thenOrFailed(
        std::move(noteFuture), promise,
        [selfWeak, this, promise,
         noteGuid](const std::optional<qevercloud::Note> & note) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (Q_UNLIKELY(!note)) {
                QNDEBUG(
                    "synchronization::NotebookFinder",
                    "Could not find note by guid in the local storage: "
                        << noteGuid);
                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            onNoteFound(*note, promise);
        });

    return future;
}

void NotebookFinder::onNoteFound(
    const qevercloud::Note & note,
    const std::shared_ptr<QPromise<std::optional<qevercloud::Notebook>>> &
        promise)
{
    auto notebookLocalId = note.notebookLocalId();

    QFuture<std::optional<qevercloud::Notebook>> notebookFuture = [&] {
        const QMutexLocker locker{&m_notebooksByLocalIdMutex};

        auto it = m_notebooksByLocalId.find(notebookLocalId);
        if (it != m_notebooksByLocalId.end() &&
            isNotebookFutureValid(it.value())) {
            return it.value();
        }

        it = m_notebooksByLocalId.insert(
            notebookLocalId,
            m_localStorage->findNotebookByLocalId(notebookLocalId));
        return it.value();
    }();

    const auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(notebookFuture), promise,
        [selfWeak, this, promise, notebookLocalId = std::move(notebookLocalId)](
            std::optional<qevercloud::Notebook> notebook) {
            const auto self = selfWeak.lock();
            if (!self) {
                return;
            }

            if (Q_UNLIKELY(!notebook)) {
                QNDEBUG(
                    "synchronization::NotebookFinder",
                    "Could not find notebook by local id in the "
                        << "local storage: notebook local id = "
                        << notebookLocalId);
                promise->addResult(std::nullopt);
                promise->finish();
                return;
            }

            {
                const QMutexLocker locker{&m_notebooksByLocalIdMutex};

                m_notebooksByLocalId[notebookLocalId] =
                    threading::makeReadyFuture<
                        std::optional<qevercloud::Notebook>>(notebook);
            }

            promise->addResult(std::move(notebook));
            promise->finish();
        });
}

void NotebookFinder::removeFutureByNoteLocalId(const QString & noteLocalId)
{
    const QMutexLocker locker{&m_notebooksByNoteLocalIdMutex};
    const auto it = m_notebooksByNoteLocalId.constFind(noteLocalId);
    if (it != m_notebooksByNoteLocalId.constEnd()) {
        m_notebooksByNoteLocalId.erase(it);
    }
}

void NotebookFinder::removeFutureByNoteGuid(const qevercloud::Guid & noteGuid)
{
    const QMutexLocker locker{&m_notebooksByNoteGuidMutex};
    const auto it = m_notebooksByNoteGuid.constFind(noteGuid);
    if (it != m_notebooksByNoteGuid.constEnd()) {
        m_notebooksByNoteGuid.erase(it);
    }
}

void NotebookFinder::removeFuturesByNotebookLocalId(
    const QString & notebookLocalId)
{
    {
        const QMutexLocker locker{&m_notebooksByLocalIdMutex};
        auto it = m_notebooksByLocalId.constFind(notebookLocalId);
        if (it != m_notebooksByLocalId.constEnd()) {
            m_notebooksByLocalId.erase(it);
        }
    }

    const auto shouldKeepNotebook =
        [&notebookLocalId](
            const QFuture<std::optional<qevercloud::Notebook>> & f) {
            if (!f.isFinished()) {
                return true;
            }

            if (Q_UNLIKELY(f.resultCount() != 1)) {
                return true;
            }

            std::optional<qevercloud::Notebook> notebook;
            try {
                notebook = f.result();
            }
            catch (...) {
            }

            if (!notebook) {
                return true;
            }

            if (notebook->localId() == notebookLocalId) {
                return false;
            }

            return true;
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
