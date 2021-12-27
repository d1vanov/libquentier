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

#include <QtGlobal>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include "Qt5FutureHelpers.h"
#include "Qt5Promise.h"
#include "QtFutureWatcherUtils.h"
#include "ThreadingUtils.h"
#include <quentier/exception/RuntimeError.h>
#include <QRunnable>
#include <QThreadPool>
#endif

namespace quentier::utility {

// implementation for Qt6

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, Function && function)
{
    return future.then(std::forward(function));
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QtFuture::Launch policy, Function && function)
{
    return future.then(policy, std::forward(function));
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QThreadPool * pool, Function && function)
{
    return future.then(pool, std::forward(function));
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QObject * context, Function && function)
{
    return future.then(context, std::forward(function));
}

template <
    class T, class Function,
    typename =
        std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs>>
QFuture<T> onFailed(QFuture<T> && future, Function && handler)
{
    return future.onFailed(std::forward(handler));
}

template <
    class T, class Function,
    typename =
        std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs>>
QFuture<T> onFailed(
    QFuture<T> && future, QObject * context, Function && handler)
{
    return future.onFailed(context, std::forward(handler));
}

#else // QT_VERSION

// implementation for Qt5

namespace detail {

template <class T, class Function>
void processParentFuture(
    QPromise<T> && promise, QFuture<T> && future, Function && function)
{
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    promise.start();

    // If future contains exception, just forward it to the promise and
    // don't call the function at all
    try {
        future.waitForFinished();
    }
    catch (const QException & e) {
        promise.setException(e);
        promise.finish();
        return;
    }
    // NOTE: there cannot be other exception types in this context in Qt5
    // because exception store can only contain QExceptions

    // Try to run the handler, in case of success forward the result to promise
    // (unless it is void), catch possible exceptions and if caught put them
    // to the promise
    try {
        if constexpr (std::is_void_v<ResultType>) {
            if constexpr (std::is_void_v<T>) {
                function();
            }
            else {
                function(future.result());
            }
        }
        else {
            if constexpr (std::is_void_v<T>) {
                promise.addResult(function());
            }
            else {
                promise.addResult(function(future.result()));
            }
        }
    }
    catch (const QException & e) {
        promise.setException(e);
    }
    catch (const std::exception & e) {
        ErrorString error{QT_TRANSLATE_NOOP(
            "utility",
            "Unknown std::exception in then future handler")};
        error.details() = QString::fromStdString(std::string{e.what()});
        promise.setException(RuntimeError{std::move(error)});
    }
    catch (...) {
        ErrorString error{QT_TRANSLATE_NOOP(
            "utility", "Unknown exception in then future handler")};
        promise.setException(RuntimeError{std::move(error)});
    }

    promise.finish();
}

} // namespace detail

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, Function && function)
{
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        detail::processParentFuture(
            std::move(promise), std::forward(future), std::forward(function));
        return result;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [watcher = std::move(watcher), function = std::forward(function),
         promise = std::move(promise)]() mutable {
            detail::processParentFuture(
                std::move(promise), watcher->future(), std::forward(function));
        });

    return result;
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QtFuture::Launch policy, Function && function)
{
    if (policy == QtFuture::Launch::Sync) {
        return then(std::forward(future), std::forward(function));
    }

    return then(
        std::forward(future), QThreadPool::globalInstance(),
        std::forward(function));
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QThreadPool * pool, Function && function)
{
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        auto * runnable = createFunctionRunnable(
            [future = std::forward(future), promise = std::move(promise),
             function = std::forward(function)]() mutable {
                detail::processParentFuture(
                    std::move(promise), std::forward(future),
                    std::forward(function));
             });
        runnable->setAutoDelete(true);
        pool->start(runnable);
        return result;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [watcher = std::move(watcher), function = std::forward(function),
         promise = std::move(promise), pool]() mutable {
            auto * runnable = createFunctionRunnable(
                [watcher = std::move(watcher), function = std::forward(function),
                 promise = std::move(promise)]() mutable {
                    detail::processParentFuture(
                        std::move(promise), watcher->future(),
                        std::forward(function));
                 });
            runnable->setAutoDelete(true);
            pool->start(runnable);
        });

    return result;
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QObject * context, Function && function)
{
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        postToObject(
            context,
            [future = std::forward(future), promise = std::move(promise),
             function = std::forward(function)]() mutable {
                detail::processParentFuture(
                    std::move(promise), std::forward(future),
                    std::forward(function));
             });
        return result;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [watcher = std::move(watcher), function = std::forward(function),
         promise = std::move(promise), context]() mutable {
            postToObject(
                context,
                [watcher = std::move(watcher), function = std::forward(function),
                 promise = std::move(promise)]() mutable {
                    detail::processParentFuture(
                        std::move(promise), watcher->future(),
                        std::forward(function));
                 });
        });

    return result;
}

namespace detail {

template <
    class T, class Function,
    typename =
        std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs>>
void processPossibleFutureException(
    QPromise<T> && promise, QFuture<T> && future, Function && handler)
{
    using ArgType = typename QtPrivate::ArgResolver<Function>::First;
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    promise.start();

    try {
        future.waitForFinished();
    }
    catch (const ArgType & e) {
        try {
            if constexpr (std::is_void_v<ResultType>) {
                handler(e);
            }
            else {
                promise.addResult(handler(e));
            }
        }
        catch (const QException & e) {
            promise.setException(e);
        }
        catch (const std::exception & e) {
            ErrorString error{QT_TRANSLATE_NOOP(
                "utility",
                "Unknown std::exception in onFailed future handler")};
            error.details() = QString::fromStdString(std::string{e.what()});
            promise.setException(RuntimeError{std::move(error)});
        }
        catch (...) {
            ErrorString error{QT_TRANSLATE_NOOP(
                "utility", "Unknown exception in onFailed future handler")};
            promise.setException(RuntimeError{std::move(error)});
        }
    }
    // Exception doesn't match with handler's argument type, propagate
    // the exception to be handled later.
    catch (const QException & e) {
        promise.setException(e);
    }
    catch (const std::exception & e) {
        ErrorString error{QT_TRANSLATE_NOOP(
            "utility",
            "Unknown std::exception which did not match with onFailed "
            "future handler")};
        error.details() = QString::fromStdString(std::string{e.what()});
        promise.setException(RuntimeError{std::move(error)});
    }
    catch (...) {
        ErrorString error{QT_TRANSLATE_NOOP(
            "utility",
            "Unknown which did not match with onFailed "
            "future handler")};
        promise.setException(RuntimeError{std::move(error)});
    }

    promise.finish();
}

} // namespace detail

// WARNING! "Chaining" of onFailed calls would only work properly with Qt5 if
// all involved exceptions subclass QException. It is due to the way exception
// storage is implemented in Qt5. In Qt6 is was made to store std::exception_ptr
// so there's no requirement to use QExceptions in Qt6.

template <
    class T, class Function,
    typename =
        std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs>>
QFuture<T> onFailed(QFuture<T> && future, Function && handler)
{
    QPromise<T> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        detail::processPossibleFutureException(
            std::move(promise), std::forward(future), std::forward(handler));
        return result;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [watcher = std::move(watcher), promise = std::move(promise),
         future = std::forward(future),
         handler = std::forward(handler)]() mutable {
             detail::processPossibleFutureException(
                std::move(promise), std::forward(future),
                std::forward(handler));
        });

    return result;
}

template <
    class T, class Function,
    typename =
        std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs>>
QFuture<T> onFailed(
    QFuture<T> && future, QObject * context, Function && handler)
{
    QPromise<T> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        postToObject(
            context,
            [promise = std::move(promise), future = std::forward(future),
             handler = std::forward(handler)]() mutable {
                 detail::processPossibleFutureException(
                    std::move(promise), std::forward(future),
                    std::forward(handler));
            });
        return result;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [watcher = std::move(watcher), promise = std::move(promise),
         future = std::forward(future), context,
         handler = std::forward(handler)]() mutable {
            postToObject(
                context,
                [promise = std::move(promise), future = std::forward(future),
                 handler = std::forward(handler)]() mutable {
                     detail::processPossibleFutureException(
                        std::move(promise), std::forward(future),
                        std::forward(handler));
                });
        });

    return result;
}

#endif // QT_VERSION

} // namespace quentier::utility
