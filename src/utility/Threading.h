/*
 * Copyright 2021 Dmitry Ivanov
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

#pragma once

#include <QAbstractEventDispatcher>
#include <QFuture>
#include <QFutureWatcher>
#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
#include <QMetaObject>
#else
#include <QThread>
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include "Qt5Promise.h"
#endif

#include <quentier/exception/RuntimeError.h>
#include <quentier/logging/QuentierLogger.h>

#include <functional>
#include <memory>
#include <type_traits>

class QRunnable;

namespace quentier::utility {

template <typename Function>
void postToObject(QObject * pObject, Function && function)
{
    Q_ASSERT(pObject);

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QMetaObject::invokeMethod(pObject, std::forward<Function>(function));
#else
    QObject src;
    QObject::connect(
        &src, &QObject::destroyed, pObject, std::forward<Function>(function),
        Qt::QueuedConnection);
#endif
}

template <typename Function>
void postToThread(QThread * pThread, Function && function)
{
    Q_ASSERT(pThread);

    QObject * pObject = QAbstractEventDispatcher::instance(pThread);
    if (!pObject) {
        // Thread's event loop has not been started yet. Create a dummy QObject,
        // move it to the target thread, set things up so that it would be
        // destroyed after the job is done and use postToObject.
        auto pDummyObj = std::make_unique<QObject>();
        pDummyObj->moveToThread(pThread);
        auto * pObj = pDummyObj.release();
        postToObject(
            pObj,
            [pObj, function = std::forward<Function>(function)]() mutable {
                function();
                pObj->deleteLater();
            });
        return;
    }

    Q_ASSERT(pObject);

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    QMetaObject::invokeMethod(pObject, std::forward<Function>(function));
#else
    QObject src;
    QObject::connect(
        &src, &QObject::destroyed, pObject, std::forward<Function>(function),
        Qt::QueuedConnection);
#endif
}

/**
 * Create QFuture already containing the result
 */
template <
    class T,
    typename = std::enable_if_t<
        std::is_fundamental_v<std::decay_t<T>> &&
        std::negation_v<std::is_same<std::decay_t<T>, void>>>>
[[nodiscard]] QFuture<std::decay_t<T>> makeReadyFuture(T t)
{
    QPromise<std::decay_t<T>> promise;
    QFuture<std::decay_t<T>> future = promise.future();

    promise.start();
    promise.addResult(t);
    promise.finish();

    return future;
}

template <
    class T,
    typename = std::enable_if_t<
        std::negation_v<std::is_fundamental<std::decay_t<T>>> &&
        std::negation_v<std::is_same<std::decay_t<T>, void>>>>
[[nodiscard]] QFuture<std::decay_t<T>> makeReadyFuture(T && t)
{
    QPromise<std::decay_t<T>> promise;
    QFuture<std::decay_t<T>> future = promise.future();

    promise.start();
    promise.addResult(std::forward<T>(t));
    promise.finish();

    return future;
}

[[nodiscard]] QFuture<void> makeReadyFuture();

/**
 * Create QFuture already containing exception instead of the result
 */
template <class T, class E>
[[nodiscard]] QFuture<T> makeExceptionalFuture(E e)
{
    QPromise<std::decay_t<T>> promise;
    QFuture<std::decay_t<T>> future = promise.future();

    promise.start();
    promise.setException(std::move(e));
    promise.finish();

    return future;
}

/**
 * Delete later deleter function for QFutureWatchers
 */
template <class T>
void deleteFutureWatcherLater(QFutureWatcher<T> * watcher) noexcept
{
    watcher->deleteLater();
}

/**
 * Create QFutureWatcher which would be deleter through a call to deleteLater
 */
template <class T>
[[nodiscard]] std::shared_ptr<QFutureWatcher<T>> makeFutureWatcher()
{
    return std::shared_ptr<QFutureWatcher<T>>(
        new QFutureWatcher<T>, deleteFutureWatcherLater<T>);
}

/**
 * Set results or exception from future to promise when either thing
 * appears in the future. Can work with futures and promises with different
 * template types if appropriate processor function between the two is provided.
 */
template <class T, class U>
void bindPromiseToFuture(
    QPromise<U> && promise, QFuture<T> future,
    std::function<void(QPromise<U> &, QList<T> &&)> processor =
        [](QPromise<U> & promise, QList<T> && results) {
            for (auto & result: results) { // NOLINT
                promise.addResult(U(std::move(result)));
            }
        })
{
    promise.start();

    if (future.isFinished()) {
        try {
            future.waitForFinished();
        }
        catch (const QException & e) {
            promise.setException(e);
            promise.finish();
            return;
        }

        processor(promise, future.results());
        promise.finish();
        return;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(future);

    // NOTE: explicitly fetch raw pointer to watcher before calling
    // QObject::connect with a lambda because watcher is moved into the lambda
    // and due to unspecified arguments evaluation order watcher.get() might
    // return nullptr if it were QObject::connect parameter.
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [promise = std::move(promise), watcher = std::move(watcher),
         processor = std::move(processor)]() mutable {
            auto future = watcher->future();
            try {
                future.waitForFinished();
            }
            catch (const QException & e) {
                promise.setException(e);
                promise.finish();
                return;
            }

            processor(promise, future.results());
            promise.finish();
            return;
        });
}

/**
 * Create QRunnable from a function - sort of a workaround for Qt < 5.15
 * where QRunnable::create does the same job
 */
[[nodiscard]] QRunnable * createFunctionRunnable(
    std::function<void()> function);

} // namespace quentier::utility
