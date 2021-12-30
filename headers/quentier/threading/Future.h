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
#include <QObject>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#include <exception>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <type_traits>

namespace quentier::threading {

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
 * Create QFuture already containing exception instead of the result -
 * version accepting const reference to QException subclass
 */
template <
    class T, class E,
    typename = std::enable_if_t<std::is_base_of_v<QException, E>>>
[[nodiscard]] QFuture<T> makeExceptionalFuture(const E & e)
{
    QPromise<std::decay_t<T>> promise;
    QFuture<std::decay_t<T>> future = promise.future();

    promise.start();
    promise.setException(e);
    promise.finish();

    return future;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
/**
 * Create QFuture already containing exception instead of the result -
 * version accepting std::exception_ptr
 */
template <class T>
[[nodiscard]] QFuture<T> makeExceptionalFuture(std::exception_ptr e)
{
    QPromise<std::decay_t<T>> promise;
    QFuture<std::decay_t<T>> future = promise.future();

    promise.start();
    promise.setException(std::move(e));
    promise.finish();

    return future;
}
#endif // QT_VERSION

} // namespace quentier::threading
