/*
 * Copyright 2024 Dmitry Ivanov
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

#include "FakeNoteStore.h"
#include "FakeNoteStoreBackend.h"

#include <quentier/exception/RuntimeError.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/Unreachable.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/RequestContextBuilder.h>

#include <QTimer>

#include <memory>

// clazy:excludeall=connect-3arg-lambda
// clazy:excludeall=lambda-in-connect

namespace quentier::synchronization::tests {

namespace {

// 10 minutes in milliseconds
constexpr int gSyncMethodCallTimeout = 600000;

[[nodiscard]] ErrorString exceptionMessage(const std::exception_ptr & e)
{
    try {
        std::rethrow_exception(e);
    }
    catch (const IQuentierException & exc) {
        return exc.errorMessage();
    }
    catch (const std::exception & exc) {
        return ErrorString{QString::fromUtf8(exc.what())};
    }
    catch (...) {
        return ErrorString{
            QT_TRANSLATE_NOOP("exception::Utils", "Unknown exception")};
    }

    UNREACHABLE;
}

} // namespace

FakeNoteStore::FakeNoteStore(
    FakeNoteStoreBackend * backend, QString noteStoreUrl,
    std::optional<qevercloud::Guid> linkedNotebookGuid,
    qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy) :
    m_backend{backend}, m_noteStoreUrl{std::move(noteStoreUrl)},
    m_linkedNotebookGuid{std::move(linkedNotebookGuid)}, m_ctx{std::move(ctx)},
    m_retryPolicy{std::move(retryPolicy)}
{
    Q_ASSERT(m_backend);
}

qevercloud::IRequestContextPtr FakeNoteStore::defaultRequestContext()
    const noexcept
{
    return m_ctx;
}

void FakeNoteStore::setDefaultRequestContext(
    qevercloud::IRequestContextPtr ctx) noexcept
{
    m_ctx = std::move(ctx);
}

QString FakeNoteStore::noteStoreUrl() const
{
    return m_noteStoreUrl;
}

void FakeNoteStore::setNoteStoreUrl(QString url)
{
    m_noteStoreUrl = std::move(url);
}

const std::optional<qevercloud::Guid> & FakeNoteStore::linkedNotebookGuid()
    const noexcept
{
    return m_linkedNotebookGuid;
}

void FakeNoteStore::setLinkedNotebookGuid(
    std::optional<qevercloud::Guid> linkedNotebookGuid)
{
    m_linkedNotebookGuid = std::move(linkedNotebookGuid);
}

qevercloud::SyncState FakeNoteStore::getSyncState(
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::SyncState result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::getSyncStateRequestReady,
            [&](qevercloud::SyncState syncState, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(syncState);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(0, [ctx = std::move(ctx), backend = m_backend] {
            backend->onGetSyncStateRequest(ctx);
        });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to get sync state in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::SyncState> FakeNoteStore::getSyncStateAsync(
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::SyncState>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::getSyncStateRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::SyncState syncState, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(syncState));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), backend = m_backend] {
        backend->onGetSyncStateRequest(ctx);
    });

    return future;
}

qevercloud::SyncChunk FakeNoteStore::getFilteredSyncChunk(
    const qint32 afterUSN, const qint32 maxEntries,
    const qevercloud::SyncChunkFilter & filter,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::SyncChunk result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::getFilteredSyncChunkRequestReady,
            [&](qevercloud::SyncChunk syncChunk, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(syncChunk);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0,
            [ctx = std::move(ctx), afterUSN, maxEntries, filter,
             backend = m_backend] {
                backend->onGetFilteredSyncChunkRequest(
                    afterUSN, maxEntries, filter, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to get filtered sync chunk in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::SyncChunk> FakeNoteStore::getFilteredSyncChunkAsync(
    qint32 afterUSN, qint32 maxEntries,
    const qevercloud::SyncChunkFilter & filter,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::SyncChunk>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::getFilteredSyncChunkRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::SyncChunk syncChunk, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(syncChunk));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });

    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0,
        [ctx = std::move(ctx), backend = m_backend, afterUSN, maxEntries,
         filter] {
            backend->onGetFilteredSyncChunkRequest(
                afterUSN, maxEntries, filter, ctx);
        });

    return future;
}

qevercloud::SyncState FakeNoteStore::getLinkedNotebookSyncState(
    const qevercloud::LinkedNotebook & linkedNotebook,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::SyncState result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend,
            &FakeNoteStoreBackend::getLinkedNotebookSyncStateRequestReady,
            [&](qevercloud::SyncState syncState, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(syncState);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), backend = m_backend, linkedNotebook] {
                backend->onGetLinkedNotebookSyncStateRequest(
                    linkedNotebook, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{QStringLiteral(
            "Failed to get linked notebook sync state in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::SyncState> FakeNoteStore::getLinkedNotebookSyncStateAsync(
    const qevercloud::LinkedNotebook & linkedNotebook,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::SyncState>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend,
        &FakeNoteStoreBackend::getLinkedNotebookSyncStateRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::SyncState syncState, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(syncState));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0,
        [ctx = std::move(ctx), backend = m_backend, linkedNotebook]() mutable {
            backend->onGetLinkedNotebookSyncStateRequest(linkedNotebook, ctx);
        });

    return future;
}

qevercloud::SyncChunk FakeNoteStore::getLinkedNotebookSyncChunk(
    const qevercloud::LinkedNotebook & linkedNotebook, const qint32 afterUSN,
    const qint32 maxEntries, const bool fullSyncOnly,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::SyncChunk result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend,
            &FakeNoteStoreBackend::getLinkedNotebookSyncChunkRequestReady,
            [&](qevercloud::SyncChunk syncChunk, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(syncChunk);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0,
            [ctx = std::move(ctx), backend = m_backend, linkedNotebook,
             afterUSN, maxEntries, fullSyncOnly] {
                backend->onGetLinkedNotebookSyncChunkRequest(
                    linkedNotebook, afterUSN, maxEntries, fullSyncOnly, ctx);
            });
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to get filtered sync chunk in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::SyncChunk> FakeNoteStore::getLinkedNotebookSyncChunkAsync(
    const qevercloud::LinkedNotebook & linkedNotebook, const qint32 afterUSN,
    const qint32 maxEntries, const bool fullSyncOnly,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::SyncChunk>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend,
        &FakeNoteStoreBackend::getLinkedNotebookSyncChunkRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::SyncChunk syncChunk, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(syncChunk));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });

    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0,
        [ctx = std::move(ctx), backend = m_backend, linkedNotebook, afterUSN,
         maxEntries, fullSyncOnly] {
            backend->onGetLinkedNotebookSyncChunkRequest(
                linkedNotebook, afterUSN, maxEntries, fullSyncOnly, ctx);
        });

    return future;
}

QList<qevercloud::Notebook> FakeNoteStore::listNotebooks(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listNotebooks not implemented")}};
}

QFuture<QList<qevercloud::Notebook>> FakeNoteStore::listNotebooksAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listNotebooksAsync not implemented")}};
}

QList<qevercloud::Notebook> FakeNoteStore::listAccessibleBusinessNotebooks(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("listAccessibleBusinessNotebooks not implemented")}};
}

QFuture<QList<qevercloud::Notebook>>
    FakeNoteStore::listAccessibleBusinessNotebooksAsync(
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral(
        "listAccessibleBusinessNotebooksAsync not implemented")}};
}

qevercloud::Notebook FakeNoteStore::getNotebook(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNotebook not implemented")}};
}

QFuture<qevercloud::Notebook> FakeNoteStore::getNotebookAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNotebookAsync not implemented")}};
}

qevercloud::Notebook FakeNoteStore::getDefaultNotebook(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getDefaultNotebook not implemented")}};
}

QFuture<qevercloud::Notebook> FakeNoteStore::getDefaultNotebookAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getDefaultNotebookAsync not implemented")}};
}

qevercloud::Notebook FakeNoteStore::createNotebook(
    const qevercloud::Notebook & notebook, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::Notebook result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::createNotebookRequestReady,
            [&](qevercloud::Notebook n, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(n);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), notebook, backend = m_backend] {
                backend->onCreateNotebookRequest(notebook, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to create notebook in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::Notebook> FakeNoteStore::createNotebookAsync(
    const qevercloud::Notebook & notebook, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::Notebook>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::createNotebookRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::Notebook n, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(n));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });

    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0, [ctx = std::move(ctx), backend = m_backend, notebook] {
            backend->onCreateNotebookRequest(notebook, ctx);
        });

    return future;
}

qint32 FakeNoteStore::updateNotebook(
    const qevercloud::Notebook & notebook, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qint32 result = 0;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::updateNotebookRequestReady,
            [&](const qint32 usn, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = usn;
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), notebook, backend = m_backend] {
                backend->onUpdateNotebookRequest(notebook, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to update notebook in due time")}};
    }

    UNREACHABLE;
}

QFuture<qint32> FakeNoteStore::updateNotebookAsync(
    const qevercloud::Notebook & notebook, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qint32>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::updateNotebookRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            const qint32 usn, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(usn);
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0, [ctx = std::move(ctx), notebook, backend = m_backend] {
            backend->onUpdateNotebookRequest(notebook, ctx);
        });

    return future;
}

qint32 FakeNoteStore::expungeNotebook(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeNotebook not implemented")}};
}

QFuture<qint32> FakeNoteStore::expungeNotebookAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeNotebookAsync not implemented")}};
}

QList<qevercloud::Tag> FakeNoteStore::listTags(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral("listTags not implemented")}};
}

QFuture<QList<qevercloud::Tag>> FakeNoteStore::listTagsAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listTagsAsync not implemented")}};
}

QList<qevercloud::Tag> FakeNoteStore::listTagsByNotebook(
    [[maybe_unused]] qevercloud::Guid notebookGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listTagsByNotebook not implemented")}};
}

QFuture<QList<qevercloud::Tag>> FakeNoteStore::listTagsByNotebookAsync(
    [[maybe_unused]] qevercloud::Guid notebookGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listTagsByNotebookAsync not implemented")}};
}

qevercloud::Tag FakeNoteStore::getTag(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral("getTag not implemented")}};
}

QFuture<qevercloud::Tag> FakeNoteStore::getTagAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getTagAsync not implemented")}};
}

qevercloud::Tag FakeNoteStore::createTag(
    const qevercloud::Tag & tag, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::Tag result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::createTagRequestReady,
            [&](qevercloud::Tag t, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(t);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(0, [ctx = std::move(ctx), tag, backend = m_backend] {
            backend->onCreateTagRequest(tag, ctx);
        });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{
            ErrorString{QStringLiteral("Failed to create tag in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::Tag> FakeNoteStore::createTagAsync(
    const qevercloud::Tag & tag, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::Tag>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::createTagRequestReady, dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::Tag t, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(t));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), tag, backend = m_backend] {
        backend->onCreateTagRequest(tag, ctx);
    });

    return future;
}

qint32 FakeNoteStore::updateTag(
    const qevercloud::Tag & tag, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qint32 result = 0;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::updateTagRequestReady,
            [&](const qint32 usn, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = usn;
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(0, [ctx = std::move(ctx), tag, backend = m_backend] {
            backend->onUpdateTagRequest(tag, ctx);
        });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{
            ErrorString{QStringLiteral("Failed to update tag in due time")}};
    }

    UNREACHABLE;
}

QFuture<qint32> FakeNoteStore::updateTagAsync(
    const qevercloud::Tag & tag, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qint32>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::updateTagRequestReady, dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            const qint32 usn, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(usn);
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), tag, backend = m_backend] {
        backend->onUpdateTagRequest(tag, ctx);
    });

    return future;
}

void FakeNoteStore::untagAll(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral("untagAll not implemented")}};
}

QFuture<void> FakeNoteStore::untagAllAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("untagAllAsync not implemented")}};
}

qint32 FakeNoteStore::expungeTag(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeTag not implemented")}};
}

QFuture<qint32> FakeNoteStore::expungeTagAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeTagAsync not implemented")}};
}

QList<qevercloud::SavedSearch> FakeNoteStore::listSearches(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listSearches not implemented")}};
}

QFuture<QList<qevercloud::SavedSearch>> FakeNoteStore::listSearchesAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listSearchesAsync not implemented")}};
}

qevercloud::SavedSearch FakeNoteStore::getSearch(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getSearch not implemented")}};
}

QFuture<qevercloud::SavedSearch> FakeNoteStore::getSearchAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getSearchAsync not implemented")}};
}

qevercloud::SavedSearch FakeNoteStore::createSearch(
    const qevercloud::SavedSearch & search, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::SavedSearch result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::createSavedSearchRequestReady,
            [&](qevercloud::SavedSearch s, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(s);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), search, backend = m_backend] {
                backend->onCreateSavedSearchRequest(search, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to create saved search in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::SavedSearch> FakeNoteStore::createSearchAsync(
    const qevercloud::SavedSearch & search, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::SavedSearch>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::createSavedSearchRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::SavedSearch s, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(s));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), search, backend = m_backend] {
        backend->onCreateSavedSearchRequest(search, ctx);
    });

    return future;
}

qint32 FakeNoteStore::updateSearch(
    const qevercloud::SavedSearch & search, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qint32 result = 0;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::updateSavedSearchRequestReady,
            [&](const qint32 usn, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = usn;
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), search, backend = m_backend] {
                backend->onUpdateSavedSearchRequest(search, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to update saved search in due time")}};
    }

    UNREACHABLE;
}

QFuture<qint32> FakeNoteStore::updateSearchAsync(
    const qevercloud::SavedSearch & search, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qint32>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::updateSavedSearchRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            const qint32 usn, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(usn);
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), search, backend = m_backend] {
        backend->onUpdateSavedSearchRequest(search, ctx);
    });

    return future;
}

qint32 FakeNoteStore::expungeSearch(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeSearch not implemented")}};
}

QFuture<qint32> FakeNoteStore::expungeSearchAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeSearchAsync not implemented")}};
}

qint32 FakeNoteStore::findNoteOffset(
    [[maybe_unused]] const qevercloud::NoteFilter & filter,
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findNoteOffset not implemented")}};
}

QFuture<qint32> FakeNoteStore::findNoteOffsetAsync(
    [[maybe_unused]] const qevercloud::NoteFilter & filter,
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findNoteOffsetAsync not implemented")}};
}

qevercloud::NotesMetadataList FakeNoteStore::findNotesMetadata(
    [[maybe_unused]] const qevercloud::NoteFilter & filter,
    [[maybe_unused]] qint32 offset, [[maybe_unused]] qint32 maxNotes,
    [[maybe_unused]] const qevercloud::NotesMetadataResultSpec & resultSpec,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findNotesMetadata not implemented")}};
}

QFuture<qevercloud::NotesMetadataList> FakeNoteStore::findNotesMetadataAsync(
    [[maybe_unused]] const qevercloud::NoteFilter & filter,
    [[maybe_unused]] qint32 offset, [[maybe_unused]] qint32 maxNotes,
    [[maybe_unused]] const qevercloud::NotesMetadataResultSpec & resultSpec,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findNotesMetadataAsync not implemented")}};
}

qevercloud::NoteCollectionCounts FakeNoteStore::findNoteCounts(
    [[maybe_unused]] const qevercloud::NoteFilter & filter,
    [[maybe_unused]] bool withTrash,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findNoteCounts not implemented")}};
}

QFuture<qevercloud::NoteCollectionCounts> FakeNoteStore::findNoteCountsAsync(
    [[maybe_unused]] const qevercloud::NoteFilter & filter,
    [[maybe_unused]] bool withTrash,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findNoteCountsAsync not implemented")}};
}

qevercloud::Note FakeNoteStore::getNoteWithResultSpec(
    qevercloud::Guid guid, const qevercloud::NoteResultSpec & resultSpec,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::Note result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::getNoteWithResultSpecRequestReady,
            [&](qevercloud::Note n, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(n);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), guid, resultSpec, backend = m_backend] {
                backend->onGetNoteWithResultSpecRequest(guid, resultSpec, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{
            QStringLiteral("Failed to get note with result spec in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::Note> FakeNoteStore::getNoteWithResultSpecAsync(
    qevercloud::Guid guid, const qevercloud::NoteResultSpec & resultSpec,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::Note>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::getNoteWithResultSpecRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::Note n, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(n));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0, [ctx = std::move(ctx), guid, resultSpec, backend = m_backend] {
            backend->onGetNoteWithResultSpecRequest(guid, resultSpec, ctx);
        });

    return future;
}

qevercloud::Note FakeNoteStore::getNote(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] bool withContent,
    [[maybe_unused]] bool withResourcesData,
    [[maybe_unused]] bool withResourcesRecognition,
    [[maybe_unused]] bool withResourcesAlternateData,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral("getNote not implemented")}};
}

QFuture<qevercloud::Note> FakeNoteStore::getNoteAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] bool withContent,
    [[maybe_unused]] bool withResourcesData,
    [[maybe_unused]] bool withResourcesRecognition,
    [[maybe_unused]] bool withResourcesAlternateData,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteAsync not implemented")}};
}

qevercloud::LazyMap FakeNoteStore::getNoteApplicationData(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteApplicationData not implemented")}};
}

QFuture<qevercloud::LazyMap> FakeNoteStore::getNoteApplicationDataAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getNoteApplicationDataAsync not implemented")}};
}

QString FakeNoteStore::getNoteApplicationDataEntry(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getNoteApplicationDataEntry not implemented")}};
}

QFuture<QString> FakeNoteStore::getNoteApplicationDataEntryAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getNoteApplicationDataEntryAsync not implemented")}};
}

qint32 FakeNoteStore::setNoteApplicationDataEntry(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] QString value,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("setNoteApplicationDataEntry not implemented")}};
}

QFuture<qint32> FakeNoteStore::setNoteApplicationDataEntryAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] QString value,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("setNoteApplicationDataEntryAsync not implemented")}};
}

qint32 FakeNoteStore::unsetNoteApplicationDataEntry(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("unsetNoteApplicationDataEntry not implemented")}};
}

QFuture<qint32> FakeNoteStore::unsetNoteApplicationDataEntryAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("unsetNoteApplicationDataEntryAsync not implemented")}};
}

QString FakeNoteStore::getNoteContent(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteContent not implemented")}};
}

QFuture<QString> FakeNoteStore::getNoteContentAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteContentAsync not implemented")}};
}

QString FakeNoteStore::getNoteSearchText(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] bool noteOnly,
    [[maybe_unused]] bool tokenizeForIndexing,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteSearchText not implemented")}};
}

QFuture<QString> FakeNoteStore::getNoteSearchTextAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] bool noteOnly,
    [[maybe_unused]] bool tokenizeForIndexing,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteSearchTextAsync not implemented")}};
}

QString FakeNoteStore::getResourceSearchText(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getResourceSearchText not implemented")}};
}

QFuture<QString> FakeNoteStore::getResourceSearchTextAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceSearchTextAsync not implemented")}};
}

QStringList FakeNoteStore::getNoteTagNames(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteTagNames not implemented")}};
}

QFuture<QStringList> FakeNoteStore::getNoteTagNamesAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteTagNamesAsync not implemented")}};
}

qevercloud::Note FakeNoteStore::createNote(
    const qevercloud::Note & note, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::Note result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::createNoteRequestReady,
            [&](qevercloud::Note n, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(n);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), note, backend = m_backend] {
                backend->onCreateNoteRequest(note, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{
            ErrorString{QStringLiteral("Failed to create note in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::Note> FakeNoteStore::createNoteAsync(
    const qevercloud::Note & note, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::Note>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::createNoteRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::Note n, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(n));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), note, backend = m_backend] {
        backend->onCreateNoteRequest(note, ctx);
    });

    return future;
}

qevercloud::Note FakeNoteStore::updateNote(
    const qevercloud::Note & note, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::Note result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::updateNoteRequestReady,
            [&](qevercloud::Note n, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(n);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0, [ctx = std::move(ctx), note, backend = m_backend] {
                backend->onUpdateNoteRequest(note, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{
            ErrorString{QStringLiteral("Failed to update note in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::Note> FakeNoteStore::updateNoteAsync(
    const qevercloud::Note & note, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::Note>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::updateNoteRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::Note n, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(n));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(0, [ctx = std::move(ctx), note, backend = m_backend] {
        backend->onUpdateNoteRequest(note, ctx);
    });

    return future;
}

qint32 FakeNoteStore::deleteNote(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("deleteNote not implemented")}};
}

QFuture<qint32> FakeNoteStore::deleteNoteAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("deleteNoteAsync not implemented")}};
}

qint32 FakeNoteStore::expungeNote(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeNote not implemented")}};
}

QFuture<qint32> FakeNoteStore::expungeNoteAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeNoteAsync not implemented")}};
}

qevercloud::Note FakeNoteStore::copyNote(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] qevercloud::Guid toNotebookGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral("copyNote not implemented")}};
}

QFuture<qevercloud::Note> FakeNoteStore::copyNoteAsync(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] qevercloud::Guid toNotebookGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("copyNoteAsync not implemented")}};
}

QList<qevercloud::NoteVersionId> FakeNoteStore::listNoteVersions(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listNoteVersions not implemented")}};
}

QFuture<QList<qevercloud::NoteVersionId>> FakeNoteStore::listNoteVersionsAsync(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listNoteVersionsAsync not implemented")}};
}

qevercloud::Note FakeNoteStore::getNoteVersion(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] qint32 updateSequenceNum,
    [[maybe_unused]] bool withResourcesData,
    [[maybe_unused]] bool withResourcesRecognition,
    [[maybe_unused]] bool withResourcesAlternateData,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteVersion not implemented")}};
}

QFuture<qevercloud::Note> FakeNoteStore::getNoteVersionAsync(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] qint32 updateSequenceNum,
    [[maybe_unused]] bool withResourcesData,
    [[maybe_unused]] bool withResourcesRecognition,
    [[maybe_unused]] bool withResourcesAlternateData,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNoteVersionAsync not implemented")}};
}

qevercloud::Resource FakeNoteStore::getResource(
    qevercloud::Guid guid, const bool withData, const bool withRecognition,
    const bool withAttributes, const bool withAlternateData,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::Resource result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend, &FakeNoteStoreBackend::getResourceRequestReady,
            [&](qevercloud::Resource r, const std::exception_ptr & e,
                const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(r);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0,
            [ctx = std::move(ctx), guid = std::move(guid), withData,
             withRecognition, withAttributes, withAlternateData,
             backend = m_backend] {
                backend->onGetResourceRequest(
                    guid, withData, withRecognition, withAttributes,
                    withAlternateData, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{
            ErrorString{QStringLiteral("Failed to get resource in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::Resource> FakeNoteStore::getResourceAsync(
    qevercloud::Guid guid, const bool withData, const bool withRecognition,
    const bool withAttributes, const bool withAlternateData,
    qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise = std::make_shared<QPromise<qevercloud::Resource>>();
    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend, &FakeNoteStoreBackend::getResourceRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::Resource r, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(r));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0,
        [ctx = std::move(ctx), guid = std::move(guid), withData,
         withRecognition, withAttributes, withAlternateData,
         backend = m_backend] {
            backend->onGetResourceRequest(
                guid, withData, withRecognition, withAttributes,
                withAlternateData, ctx);
        });

    return future;
}

qevercloud::LazyMap FakeNoteStore::getResourceApplicationData(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceApplicationData not implemented")}};
}

QFuture<qevercloud::LazyMap> FakeNoteStore::getResourceApplicationDataAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceApplicationDataAsync not implemented")}};
}

QString FakeNoteStore::getResourceApplicationDataEntry(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceApplicationDataEntry not implemented")}};
}

QFuture<QString> FakeNoteStore::getResourceApplicationDataEntryAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral(
        "getResourceApplicationDataEntryAsync not implemented")}};
}

qint32 FakeNoteStore::setResourceApplicationDataEntry(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] QString value,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("setResourceApplicationDataEntry not implemented")}};
}

QFuture<qint32> FakeNoteStore::setResourceApplicationDataEntryAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] QString value,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral(
        "setResourceApplicationDataEntryAsync not implemented")}};
}

qint32 FakeNoteStore::unsetResourceApplicationDataEntry(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("unsetResourceApplicationDataEntry not implemented")}};
}

QFuture<qint32> FakeNoteStore::unsetResourceApplicationDataEntryAsync(
    [[maybe_unused]] qevercloud::Guid guid, [[maybe_unused]] QString key,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{QStringLiteral(
        "unsetResourceApplicationDataEntryAsync not implemented")}};
}

qint32 FakeNoteStore::updateResource(
    [[maybe_unused]] const qevercloud::Resource & resource,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("updateResource not implemented")}};
}

QFuture<qint32> FakeNoteStore::updateResourceAsync(
    [[maybe_unused]] const qevercloud::Resource & resource,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("updateResourceAsync not implemented")}};
}

QByteArray FakeNoteStore::getResourceData(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getResourceData not implemented")}};
}

QFuture<QByteArray> FakeNoteStore::getResourceDataAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getResourceDataAsync not implemented")}};
}

qevercloud::Resource FakeNoteStore::getResourceByHash(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] QByteArray contentHash, [[maybe_unused]] bool withData,
    [[maybe_unused]] bool withRecognition,
    [[maybe_unused]] bool withAlternateData,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getResourceByHash not implemented")}};
}

QFuture<qevercloud::Resource> FakeNoteStore::getResourceByHashAsync(
    [[maybe_unused]] qevercloud::Guid noteGuid,
    [[maybe_unused]] QByteArray contentHash, [[maybe_unused]] bool withData,
    [[maybe_unused]] bool withRecognition,
    [[maybe_unused]] bool withAlternateData,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getResourceByHashAsync not implemented")}};
}

QByteArray FakeNoteStore::getResourceRecognition(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getResourceRecognition not implemented")}};
}

QFuture<QByteArray> FakeNoteStore::getResourceRecognitionAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceRecognitionAsync not implemented")}};
}

QByteArray FakeNoteStore::getResourceAlternateData(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceAlternateData not implemented")}};
}

QFuture<QByteArray> FakeNoteStore::getResourceAlternateDataAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceAlternateDataAsync not implemented")}};
}

qevercloud::ResourceAttributes FakeNoteStore::getResourceAttributes(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getResourceAttributes not implemented")}};
}

QFuture<qevercloud::ResourceAttributes>
    FakeNoteStore::getResourceAttributesAsync(
        [[maybe_unused]] qevercloud::Guid guid,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getResourceAttributesAsync not implemented")}};
}

qevercloud::Notebook FakeNoteStore::getPublicNotebook(
    [[maybe_unused]] qevercloud::UserID userId,
    [[maybe_unused]] QString publicUri,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getPublicNotebook not implemented")}};
}

QFuture<qevercloud::Notebook> FakeNoteStore::getPublicNotebookAsync(
    [[maybe_unused]] qevercloud::UserID userId,
    [[maybe_unused]] QString publicUri,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getPublicNotebookAsync not implemented")}};
}

qevercloud::SharedNotebook FakeNoteStore::shareNotebook(
    [[maybe_unused]] const qevercloud::SharedNotebook & sharedNotebook,
    [[maybe_unused]] QString message,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("shareNotebook not implemented")}};
}

QFuture<qevercloud::SharedNotebook> FakeNoteStore::shareNotebookAsync(
    [[maybe_unused]] const qevercloud::SharedNotebook & sharedNotebook,
    [[maybe_unused]] QString message,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("shareNotebookAsync not implemented")}};
}

qevercloud::CreateOrUpdateNotebookSharesResult
    FakeNoteStore::createOrUpdateNotebookShares(
        [[maybe_unused]] const qevercloud::NotebookShareTemplate &
            shareTemplate,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("createOrUpdateNotebookShares not implemented")}};
}

QFuture<qevercloud::CreateOrUpdateNotebookSharesResult>
    FakeNoteStore::createOrUpdateNotebookSharesAsync(
        [[maybe_unused]] const qevercloud::NotebookShareTemplate &
            shareTemplate,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("createOrUpdateNotebookSharesAsync not implemented")}};
}

qint32 FakeNoteStore::updateSharedNotebook(
    [[maybe_unused]] const qevercloud::SharedNotebook & sharedNotebook,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("updateSharedNotebook not implemented")}};
}

QFuture<qint32> FakeNoteStore::updateSharedNotebookAsync(
    [[maybe_unused]] const qevercloud::SharedNotebook & sharedNotebook,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("updateSharedNotebookAsync not implemented")}};
}

qevercloud::Notebook FakeNoteStore::setNotebookRecipientSettings(
    [[maybe_unused]] QString notebookGuid,
    [[maybe_unused]] const qevercloud::NotebookRecipientSettings &
        recipientSettings,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("setNotebookRecipientSettings not implemented")}};
}

QFuture<qevercloud::Notebook> FakeNoteStore::setNotebookRecipientSettingsAsync(
    [[maybe_unused]] QString notebookGuid,
    [[maybe_unused]] const qevercloud::NotebookRecipientSettings &
        recipientSettings,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("setNotebookRecipientSettingsAsync not implemented")}};
}

QList<qevercloud::SharedNotebook> FakeNoteStore::listSharedNotebooks(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listSharedNotebooks not implemented")}};
}

QFuture<QList<qevercloud::SharedNotebook>>
    FakeNoteStore::listSharedNotebooksAsync(
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("listSharedNotebooksAsync not implemented")}};
}

qevercloud::LinkedNotebook FakeNoteStore::createLinkedNotebook(
    [[maybe_unused]] const qevercloud::LinkedNotebook & linkedNotebook,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("createLinkedNotebook not implemented")}};
}

QFuture<qevercloud::LinkedNotebook> FakeNoteStore::createLinkedNotebookAsync(
    [[maybe_unused]] const qevercloud::LinkedNotebook & linkedNotebook,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("createLinkedNotebookAsync not implemented")}};
}

qint32 FakeNoteStore::updateLinkedNotebook(
    [[maybe_unused]] const qevercloud::LinkedNotebook & linkedNotebook,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("updateLinkedNotebook not implemented")}};
}

QFuture<qint32> FakeNoteStore::updateLinkedNotebookAsync(
    [[maybe_unused]] const qevercloud::LinkedNotebook & linkedNotebook,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("updateLinkedNotebookAsync not implemented")}};
}

QList<qevercloud::LinkedNotebook> FakeNoteStore::listLinkedNotebooks(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("listLinkedNotebooks not implemented")}};
}

QFuture<QList<qevercloud::LinkedNotebook>>
    FakeNoteStore::listLinkedNotebooksAsync(
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("listLinkedNotebooksAsync not implemented")}};
}

qint32 FakeNoteStore::expungeLinkedNotebook(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("expungeLinkedNotebook not implemented")}};
}

QFuture<qint32> FakeNoteStore::expungeLinkedNotebookAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("expungeLinkedNotebookAsync not implemented")}};
}

qevercloud::AuthenticationResult FakeNoteStore::authenticateToSharedNotebook(
    QString shareKeyOrGlobalId, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    qevercloud::AuthenticationResult result;
    EventLoopWithExitStatus::ExitStatus status =
        EventLoopWithExitStatus::ExitStatus::Failure;
    ErrorString error;
    {
        QTimer timer;
        timer.setInterval(gSyncMethodCallTimeout);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        auto connection = QObject::connect(
            m_backend,
            &FakeNoteStoreBackend::authenticateToSharedNotebookRequestReady,
            [&](qevercloud::AuthenticationResult r,
                const std::exception_ptr & e, const QUuid requestId) {
                if (requestId != ctx->requestId()) {
                    return;
                }

                if (!e) {
                    result = std::move(r);
                    QTimer::singleShot(0, [&loop] { loop.exitAsSuccess(); });
                    return;
                }

                ErrorString errorMessage = exceptionMessage(e);
                QTimer::singleShot(
                    0,
                    [&loop, errorMessage = std::move(errorMessage)]() mutable {
                        loop.exitAsFailureWithErrorString(
                            std::move(errorMessage));
                    });
            });

        QTimer::singleShot(
            0,
            [ctx = std::move(ctx),
             shareKeyOrGlobalId = std::move(shareKeyOrGlobalId),
             backend = m_backend] {
                backend->onAuthenticateToSharedNotebookRequest(
                    shareKeyOrGlobalId, ctx);
            });

        timer.start();
        loop.exec();

        QObject::disconnect(connection);
        status = loop.exitStatus();
        error = loop.errorDescription();
    }

    switch (status) {
    case EventLoopWithExitStatus::ExitStatus::Success:
        return result;
    case EventLoopWithExitStatus::ExitStatus::Failure:
        throw RuntimeError{std::move(error)};
    case EventLoopWithExitStatus::ExitStatus::Timeout:
        throw RuntimeError{ErrorString{QStringLiteral(
            "Failed to authenticate to shared notebook in due time")}};
    }

    UNREACHABLE;
}

QFuture<qevercloud::AuthenticationResult>
    FakeNoteStore::authenticateToSharedNotebookAsync(
        QString shareKeyOrGlobalId, qevercloud::IRequestContextPtr ctx)
{
    ensureRequestContext(ctx);

    auto promise =
        std::make_shared<QPromise<qevercloud::AuthenticationResult>>();

    auto future = promise->future();
    promise->start();

    auto dummyObject = std::make_unique<QObject>();
    auto * dummyObjectRaw = dummyObject.get();
    QObject::connect(
        m_backend,
        &FakeNoteStoreBackend::authenticateToSharedNotebookRequestReady,
        dummyObjectRaw,
        [ctx, promise = std::move(promise), dummyObjectRaw](
            qevercloud::AuthenticationResult r, const std::exception_ptr & e,
            const QUuid requestId) {
            if (requestId != ctx->requestId()) {
                return;
            }

            dummyObjectRaw->deleteLater();

            if (!e) {
                promise->addResult(std::move(r));
                promise->finish();
                return;
            }

            ErrorString errorMessage = exceptionMessage(e);
            promise->setException(RuntimeError{std::move(errorMessage)});
            promise->finish();
        });
    Q_UNUSED(dummyObject.release()); // NOLINT

    QTimer::singleShot(
        0,
        [ctx = std::move(ctx),
         shareKeyOrGlobalId = std::move(shareKeyOrGlobalId),
         backend = m_backend] {
            backend->onAuthenticateToSharedNotebookRequest(
                shareKeyOrGlobalId, ctx);
        });

    return future;
}

qevercloud::SharedNotebook FakeNoteStore::getSharedNotebookByAuth(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getSharedNotebookByAuth not implemented")}};
}

QFuture<qevercloud::SharedNotebook> FakeNoteStore::getSharedNotebookByAuthAsync(
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("getSharedNotebookByAuthAsync not implemented")}};
}

void FakeNoteStore::emailNote(
    [[maybe_unused]] const qevercloud::NoteEmailParameters & parameters,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("emailNote not implemented")}};
}

QFuture<void> FakeNoteStore::emailNoteAsync(
    [[maybe_unused]] const qevercloud::NoteEmailParameters & parameters,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("emailNoteAsync not implemented")}};
}

QString FakeNoteStore::shareNote(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("shareNote not implemented")}};
}

QFuture<QString> FakeNoteStore::shareNoteAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("shareNoteAsync not implemented")}};
}

void FakeNoteStore::stopSharingNote(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("stopSharingNote not implemented")}};
}

QFuture<void> FakeNoteStore::stopSharingNoteAsync(
    [[maybe_unused]] qevercloud::Guid guid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("stopSharingNoteAsync not implemented")}};
}

qevercloud::AuthenticationResult FakeNoteStore::authenticateToSharedNote(
    [[maybe_unused]] QString guid, [[maybe_unused]] QString noteKey,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("authenticateToSharedNote not implemented")}};
}

QFuture<qevercloud::AuthenticationResult>
    FakeNoteStore::authenticateToSharedNoteAsync(
        [[maybe_unused]] QString guid, [[maybe_unused]] QString noteKey,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("authenticateToSharedNoteAsync not implemented")}};
}

qevercloud::RelatedResult FakeNoteStore::findRelated(
    [[maybe_unused]] const qevercloud::RelatedQuery & query,
    [[maybe_unused]] const qevercloud::RelatedResultSpec & resultSpec,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findRelated not implemented")}};
}

QFuture<qevercloud::RelatedResult> FakeNoteStore::findRelatedAsync(
    [[maybe_unused]] const qevercloud::RelatedQuery & query,
    [[maybe_unused]] const qevercloud::RelatedResultSpec & resultSpec,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("findRelatedAsync not implemented")}};
}

qevercloud::UpdateNoteIfUsnMatchesResult FakeNoteStore::updateNoteIfUsnMatches(
    [[maybe_unused]] const qevercloud::Note & note,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("updateNoteIfUsnMatches not implemented")}};
}

QFuture<qevercloud::UpdateNoteIfUsnMatchesResult>
    FakeNoteStore::updateNoteIfUsnMatchesAsync(
        [[maybe_unused]] const qevercloud::Note & note,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("updateNoteIfUsnMatchesAsync not implemented")}};
}

qevercloud::ManageNotebookSharesResult FakeNoteStore::manageNotebookShares(
    [[maybe_unused]] const qevercloud::ManageNotebookSharesParameters &
        parameters,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("manageNotebookShares not implemented")}};
}

QFuture<qevercloud::ManageNotebookSharesResult>
    FakeNoteStore::manageNotebookSharesAsync(
        [[maybe_unused]] const qevercloud::ManageNotebookSharesParameters &
            parameters,
        [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{ErrorString{
        QStringLiteral("manageNotebookSharesAsync not implemented")}};
}

qevercloud::ShareRelationships FakeNoteStore::getNotebookShares(
    [[maybe_unused]] QString notebookGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNotebookShares not implemented")}};
}

QFuture<qevercloud::ShareRelationships> FakeNoteStore::getNotebookSharesAsync(
    [[maybe_unused]] QString notebookGuid,
    [[maybe_unused]] qevercloud::IRequestContextPtr ctx)
{
    throw RuntimeError{
        ErrorString{QStringLiteral("getNotebookSharesAsync not implemented")}};
}

void FakeNoteStore::ensureRequestContext(
    qevercloud::IRequestContextPtr & ctx) const
{
    if (ctx) {
        return;
    }

    qevercloud::RequestContextBuilder builder;
    builder.setRequestId(QUuid::createUuid());

    if (m_ctx) {
        builder.setAuthenticationToken(m_ctx->authenticationToken());
        builder.setConnectionTimeout(m_ctx->connectionTimeout());
        builder.setIncreaseConnectionTimeoutExponentially(
            m_ctx->increaseConnectionTimeoutExponentially());
        builder.setMaxRetryCount(m_ctx->maxRequestRetryCount());
        builder.setCookies(m_ctx->cookies());
    }

    ctx = builder.build();
}

} // namespace quentier::synchronization::tests
