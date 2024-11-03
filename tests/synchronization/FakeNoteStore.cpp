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
    throw RuntimeError{
        ErrorString{QStringLiteral("untagAll not implemented")}};
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

// TODO: continue from here

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
