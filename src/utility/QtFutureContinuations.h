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
#include <QRunnable>
#include <QThreadPool>
#endif

namespace quentier::utility {

// declarations

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, Function && function);

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QtFuture::Launch policy, Function && function);

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QThreadPool * pool, Function && function);

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QObject * context, Function && function);

// implementation for Qt6

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, Function && function)
{
    return future.then(std::forward(function));
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QtFuture::Launch policy, Function && function)
{
    return future.then(policy, std::forward(function));
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QThreadPool * pool, Function && function)
{
    return future.then(pool, std::forward(function));
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QObject * context, Function && function)
{
    return future.then(context, std::forward(function));
}

#else // QT_VERSION

// implementation for Qt5

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, Function && function)
{
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    promise.start();

    if (future.isFinished()) {
        try {
            if constexpr (std::is_same_v<ResultType, void>) {
                future.waitForFinished();
            }
            else {
                promise.addResult(function(future.result()));
            }
        }
        catch (const QException & e) {
            promise.setException(e);
        }
        promise.finish();
        return result;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [watcher = std::move(watcher), function = std::forward(function),
         promise = std::move(promise)]() mutable {
            auto future = watcher.future();
            try {
                if constexpr (std::is_same_v<ResultType, void>) {
                    future.waitForFinished();
                }
                else {
                    promise.addResult(function(future.result()));
                }
            }
            catch (const QException & e) {
                promise.setException(e);
            }
            promise.finish();
            return result;
        });

    return result;
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QtFuture::Launch policy, Function && function)
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
    QFuture<T> & future, QThreadPool * pool, Function && function)
{
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    promise.start();

    if (future.isFinished()) {
        auto * runnable = createFunctionRunnable(
            [future = std::move(future), promise = std::move(promise),
             function = std::forward(function)]() mutable {
                 try {
                     if constexpr (std::is_same_v<ResultType, void>) {
                         future.waitForFinished();
                     }
                     else {
                         promise.addResult(function(future.result()));
                     }
                 }
                 catch (const QException & e) {
                     promise.setException(e);
                 }
                 promise.finish();
                 return result;
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
         promise = std::move(promise)]() mutable {
            auto * runnable = createFunctionRunnable(
                [watcher = std::move(watcher), function = std::forward(function),
                 promise = std::move(promise)]() mutable {
                     auto future = watcher.future();
                     try {
                         if constexpr (std::is_same_v<ResultType, void>) {
                             future.waitForFinished();
                         }
                         else {
                             promise.addResult(function(future.result()));
                         }
                     }
                     catch (const QException & e) {
                         promise.setException(e);
                     }
                     promise.finish();
                 });
            runnable->setAutoDelete(true);
            pool->start(runnable);
        });

    return result;
}

template <class T, class Function>
QFuture<typename ResultTypeHelper<Function, T>::ResultType> then(
    QFuture<T> & future, QObject * context, Function && function)
{
    using ResultType = typename ResultTypeHelper<Function, T>::ResultType;

    QPromise<ResultType> promise;
    auto result = promise.future();

    promise.start();

    if (future.isFinished()) {
        postToObject(
            context,
            [future = std::move(future), promise = std::move(promise),
             function = std::forward(function)]() mutable {
                 try {
                     if constexpr (std::is_same_v<ResultType, void>) {
                         future.waitForFinished();
                     }
                     else {
                         promise.addResult(function(future.result()));
                     }
                 }
                 catch (const QException & e) {
                     promise.setException(e);
                 }
                 promise.finish();
                 return result;
             });
        return result;
    }

    auto watcher = makeFutureWatcher<T>();
    watcher->setFuture(std::move(future));
    auto * rawWatcher = watcher.get();
    QObject::connect(
        rawWatcher, &QFutureWatcher<T>::finished, rawWatcher,
        [watcher = std::move(watcher), function = std::forward(function),
         promise = std::move(promise)]() mutable {
            postToObject(
                context,
                [watcher = std::move(watcher), function = std::forward(function),
                 promise = std::move(promise)]() mutable {
                     auto future = watcher.future();
                     try {
                         if constexpr (std::is_same_v<ResultType, void>) {
                             future.waitForFinished();
                         }
                         else {
                             promise.addResult(function(future.result()));
                         }
                     }
                     catch (const QException & e) {
                         promise.setException(e);
                     }
                     promise.finish();
                 });
        });

    return result;
}

#endif // QT_VERSION

} // namespace quentier::utility
