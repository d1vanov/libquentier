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

#include "QtFutureWatcherUtils.h"
#include "ThreadingUtils.h"

#include <QAbstractEventDispatcher>
#include <QFuture>
#include <QFutureWatcher>
#include <QObject>

#include <QFuture>
#include <QFutureWatcher>

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

namespace quentier::utility {

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
 * Checks whether the passed future contains exception, if yes, sets this
 * exception to the promise.
 * @note future must be already finished, otherwise the execution would block
 *       until it is finished
 * @return true if future contains exception, false otherwise
 */
template <class T, class U>
bool checkFutureException(QPromise<U> & promise, QFuture<T> & future)
{
    try {
        future.waitForFinished();
    }
    catch (const QException & e) {
        promise.setException(e);
        promise.finish();
        return true;
    }

    return false;
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
        if (!checkFutureException(promise, future))
        {
            processor(promise, future.results());
            promise.finish();
        }

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
            if (checkFutureException(promise, future)) {
                return;
            }

            processor(promise, future.results());
            promise.finish();
            return;
        });
}

} // namespace quentier::utility
