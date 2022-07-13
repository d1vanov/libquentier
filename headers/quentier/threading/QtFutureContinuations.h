/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include <QFutureWatcher>
#include <QRunnable>
#include <QThreadPool>
#include <quentier/exception/RuntimeError.h>
#include <quentier/threading/Post.h>
#include <quentier/threading/Qt5FutureHelpers.h>
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>
#include <type_traits>

namespace quentier::threading {

// implementation for Qt6

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

template <class T, class Function>
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, Function && function)
{
    return future.then(std::forward<decltype(function)>(function));
}

template <class T, class Function>
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QtFuture::Launch policy, Function && function)
{
    return future.then(policy, std::forward<decltype(function)>(function));
}

template <class T, class Function>
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QThreadPool * pool, Function && function)
{
    return future.then(pool, std::forward<decltype(function)>(function));
}

template <class T, class Function>
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QObject * context, Function && function)
{
    return future.then(context, std::forward<decltype(function)>(function));
}

template <class T, class Function>
std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs, QFuture<T>>
    onFailed(QFuture<T> && future, Function && handler)
{
    return future.onFailed(std::forward<decltype(handler)>(handler));
}

template <class T, class Function>
std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs, QFuture<T>>
    onFailed(QFuture<T> && future, QObject * context, Function && handler)
{
    return future.onFailed(context, std::forward<decltype(handler)>(handler));
}

#else // QT_VERSION

// implementation for Qt5

namespace detail {

template <class T, class Function>
void processParentFuture(
    QPromise<typename ResultTypeHelper<Function, T>::ResultType> && promise,
    QFuture<T> && future, Function && function)
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
            "utility", "Unknown std::exception in then future handler")};
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
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, Function && function)
{
    using ResultType =
        typename detail::ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        detail::processParentFuture(
            std::move(promise), std::move(future),
            std::forward<decltype(function)>(function));
        return result;
    }

    auto watcher = std::make_unique<QFutureWatcher<T>>();
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [rawWatcher, function = std::forward<decltype(function)>(function),
         promise = std::move(promise)]() mutable {
            detail::processParentFuture(
                std::move(promise), rawWatcher->future(),
                std::forward<decltype(function)>(function));
            rawWatcher->deleteLater();
        });

    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::canceled, rawWatcher,
        [rawWatcher] { rawWatcher->deleteLater(); });

    watcher->setFuture(std::move(future));

    Q_UNUSED(watcher.release())
    return result;
}

template <class T, class Function>
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QtFuture::Launch policy, Function && function)
{
    if (policy == QtFuture::Launch::Sync) {
        return then(
            std::move(future), std::forward<decltype(function)>(function));
    }

    return then(
        std::move(future), QThreadPool::globalInstance(),
        std::forward<decltype(function)>(function));
}

template <class T, class Function>
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QThreadPool * pool, Function && function)
{
    using ResultType =
        typename detail::ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        auto * runnable = createFunctionRunnable(
            [future = std::move(future), promise = std::move(promise),
             function = std::forward<decltype(function)>(function)]() mutable {
                detail::processParentFuture(
                    std::move(promise), std::move(future),
                    std::forward<decltype(function)>(function));
            });
        runnable->setAutoDelete(true);
        pool->start(runnable);
        return result;
    }

    auto watcher = std::make_unique<QFutureWatcher<T>>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [rawWatcher, function = std::forward<decltype(function)>(function),
         promise = std::move(promise), pool]() mutable {
            auto * runnable = createFunctionRunnable(
                [function = std::forward<decltype(function)>(function),
                 promise = std::move(promise),
                 future = rawWatcher->future()]() mutable {
                    detail::processParentFuture(
                        std::move(promise), std::move(future),
                        std::forward<decltype(function)>(function));
                });
            runnable->setAutoDelete(true);
            pool->start(runnable);
            rawWatcher->deleteLater();
        });

    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::canceled, rawWatcher,
        [rawWatcher] { rawWatcher->deleteLater(); });

    Q_UNUSED(watcher.release())
    return result;
}

template <class T, class Function>
QFuture<typename detail::ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> && future, QObject * context, Function && function)
{
    using ResultType =
        typename detail::ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        postToObject(
            context,
            [future = std::move(future), promise = std::move(promise),
             function = std::forward<decltype(function)>(function)]() mutable {
                detail::processParentFuture(
                    std::move(promise), std::move(future),
                    std::forward<decltype(function)>(function));
            });
        return result;
    }

    auto watcher = std::make_unique<QFutureWatcher<T>>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [rawWatcher, function = std::forward<decltype(function)>(function),
         promise = std::move(promise), context]() mutable {
            postToObject(
                context,
                [function = std::forward<decltype(function)>(function),
                 promise = std::move(promise),
                 future = rawWatcher->future()]() mutable {
                    detail::processParentFuture(
                        std::move(promise), std::move(future),
                        std::forward<decltype(function)>(function));
                });
            rawWatcher->deleteLater();
        });

    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::canceled, rawWatcher,
        [rawWatcher] { rawWatcher->deleteLater(); });

    Q_UNUSED(watcher.release())
    return result;
}

namespace detail {

template <class T, class Function>
std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs, void>
    processPossibleFutureException(
        QPromise<T> && promise, QFuture<T> && future, Function && handler)
{
    using ArgType = typename QtPrivate::ArgResolver<Function>::First;
    using ResultType =
        typename ResultTypeHelper<Function, std::decay_t<ArgType>>::ResultType;
    static_assert(std::is_convertible_v<ResultType, T>);

    promise.start();

    try {
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

template <class T, class Function>
std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs, QFuture<T>>
    onFailed(QFuture<T> && future, Function && handler)
{
    QPromise<T> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        detail::processPossibleFutureException(
            std::move(promise), std::move(future),
            std::forward<decltype(handler)>(handler));
        return result;
    }

    auto watcher = std::make_unique<QFutureWatcher<T>>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [rawWatcher, promise = std::move(promise), future = std::move(future),
         handler = std::forward<decltype(handler)>(handler)]() mutable {
            rawWatcher->deleteLater();
            detail::processPossibleFutureException(
                std::move(promise), std::move(future),
                std::forward<decltype(handler)>(handler));
        });

    Q_UNUSED(watcher.release())
    return result;
}

template <class T, class Function>
std::enable_if_t<!QtPrivate::ArgResolver<Function>::HasExtraArgs, QFuture<T>>
    onFailed(QFuture<T> && future, QObject * context, Function && handler)
{
    QPromise<T> promise;
    auto result = promise.future();

    if (future.isFinished()) {
        postToObject(
            context,
            [promise = std::move(promise), future = std::move(future),
             handler = std::forward<decltype(handler)>(handler)]() mutable {
                detail::processPossibleFutureException(
                    std::move(promise), std::move(future),
                    std::forward<decltype(handler)>(handler));
            });
        return result;
    }

    auto watcher = std::make_unique<QFutureWatcher<T>>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [rawWatcher, promise = std::move(promise), future = std::move(future),
         context,
         handler = std::forward<decltype(handler)>(handler)]() mutable {
            rawWatcher->deleteLater();
            postToObject(
                context,
                [promise = std::move(promise), future = std::move(future),
                 handler = std::forward<decltype(handler)>(handler)]() mutable {
                    detail::processPossibleFutureException(
                        std::move(promise), std::move(future),
                        std::forward<decltype(handler)>(handler));
                });
        });

    Q_UNUSED(watcher.release())
    return result;
}

#endif // QT_VERSION

// Convenience functions for both Qt versions

template <class T, class U, class Function>
void thenOrFailed(
    QFuture<T> && future, std::shared_ptr<QPromise<U>> promise,
    Function && function)
{
    auto thenFuture =
        then(std::move(future), std::forward<decltype(function)>(function));

    onFailed(std::move(thenFuture), [promise](const QException & e) {
        promise->setException(e);
        promise->finish();
    });
}

template <class T, class U>
void thenOrFailed(QFuture<T> && future, std::shared_ptr<QPromise<U>> promise)
{
    thenOrFailed(std::move(future), promise, [promise] { promise->finish(); });
}

} // namespace quentier::threading
