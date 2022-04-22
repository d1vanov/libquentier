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

#include <quentier/utility/Linkage.h>

#include <QAbstractEventDispatcher>
#include <QFuture>
#include <QFutureWatcher>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QPointer>

#include <quentier/threading/QtFutureContinuations.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#include <exception>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <cmath>
#include <memory>
#include <type_traits>

namespace quentier::threading {

/**
 * Create QFuture already containing the result
 */
template <class T>
[[nodiscard]] std::enable_if_t<
    std::is_fundamental_v<std::decay_t<T>> &&
        std::negation_v<std::is_same<std::decay_t<T>, void>>,
    QFuture<std::decay_t<T>>>
    makeReadyFuture(T t)
{
    QPromise<std::decay_t<T>> promise;
    QFuture<std::decay_t<T>> future = promise.future();

    promise.start();
    promise.addResult(t);
    promise.finish();

    return future;
}

template <class T>
[[nodiscard]] std::enable_if_t<
    std::negation_v<std::is_fundamental<std::decay_t<T>>> &&
        std::negation_v<std::is_same<std::decay_t<T>, void>>,
    QFuture<std::decay_t<T>>>
    makeReadyFuture(T && t)
{
    QPromise<std::decay_t<T>> promise;
    QFuture<std::decay_t<T>> future = promise.future();

    promise.start();
    promise.addResult(std::forward<T>(t));
    promise.finish();

    return future;
}

[[nodiscard]] QFuture<void> QUENTIER_EXPORT makeReadyFuture();

/**
 * Create QFuture already containing exception instead of the result -
 * version accepting const reference to QException subclass
 */
template <class T, class E>
[[nodiscard]] std::enable_if_t<std::is_base_of_v<QException, E>, QFuture<T>>
    makeExceptionalFuture(const E & e)
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

/**
 * Create QFuture<void> which would only become finished when either all passed
 * in futures are finished successfully (without exception) or at least one of
 * passed in futures is finished unsuccessfully (with exception) in which case
 * "all future" is considered unsuccessful as well and would contain the first
 * occurred exception inside it.
 */
[[nodiscard]] QFuture<void> QUENTIER_EXPORT
    whenAll(QList<QFuture<void>> futures);

/**
 * Create non-void QFuture which would only become finished when either all
 * passed in futures are finished successfully (without exception) or at least
 * one of passed in futures is finished unsuccessfully (with exception) in which
 * case "all future" is considered unsuccessful as well and would contain the
 * first occurred exception inside it. In case of success of all passed in
 * futures the result future would contain the list with results of all
 * passed in futures.
 */
template <class T>
[[nodiscard]] std::enable_if_t<
    !std::is_void_v<T>, QFuture<QList<std::decay_t<T>>>>
    whenAll(QList<QFuture<std::decay_t<T>>> futures)
{
    if (Q_UNLIKELY(futures.isEmpty())) {
        return makeReadyFuture<QList<T>>({});
    }

    auto promise = std::make_shared<QPromise<QList<std::decay_t<T>>>>();
    auto future = promise->future();

    const int totalItemCount = futures.size();
    promise->setProgressRange(0, totalItemCount);
    promise->setProgressValue(0);

    promise->start();

    auto resultList = std::make_shared<QList<std::decay_t<T>>>();
    auto processedItemsCount = std::make_shared<int>(0);
    auto exceptionFlag = std::make_shared<bool>(false);
    auto mutex = std::make_shared<QMutex>();

    for (auto & future: futures) {
        auto thenFuture = then(
            std::move(future),
            [promise, processedItemsCount, totalItemCount, exceptionFlag, mutex,
             resultList](auto result) {
                int count = 0;
                {
                    const QMutexLocker locker{mutex.get()};

                    if (*exceptionFlag) {
                        return;
                    }

                    ++(*processedItemsCount);
                    count = *processedItemsCount;
                    promise->setProgressValue(count);

                    resultList->append(std::move(result));
                }

                if (count == totalItemCount) {
                    promise->addResult(*resultList);
                    promise->finish();
                }
            });

        onFailed(
            std::move(thenFuture),
            [promise, mutex, exceptionFlag](const QException & e) {
                {
                    const QMutexLocker locker{mutex.get()};

                    if (*exceptionFlag) {
                        return;
                    }

                    *exceptionFlag = true;
                }

                promise->setException(e);
                promise->finish();
            });
    }

    return future;
}

/**
 * Maps updates of progress values from the passed in future into the passed in
 * promise. Takes into account that progress minimum and maximum might be
 * different for the future and the promise.
 */
template <class T, class U>
void mapFutureProgress(
    const QFuture<T> & future, const std::shared_ptr<QPromise<U>> & promise)
{
    const auto futureProgressMinimum = future.progressMinimum();
    const auto futureProgressRange =
        future.progressMaximum() - futureProgressMinimum;

    Q_ASSERT(futureProgressRange >= 0);

    const auto promiseFuture = promise->future();
    const auto promiseProgressMinimum = promiseFuture.progressMinimum();
    const auto promiseProgressMaximum = promiseFuture.progressMaximum();

    const auto promiseProgressRange =
        promiseProgressMaximum - promiseProgressMinimum;

    Q_ASSERT(promiseProgressRange >= 0);

    auto futureWatcher = std::make_unique<QFutureWatcher<T>>();

    QObject::connect(
        futureWatcher.get(), &QFutureWatcher<T>::progressValueChanged,
        futureWatcher.get(),
        [promise, futureProgressMinimum, futureProgressRange,
         promiseProgressRange, promiseProgressMinimum,
         promiseProgressMaximum](int progressValue) {
            if (Q_UNLIKELY(futureProgressRange == 0)) {
                promise->setProgressValue(0);
                return;
            }

            const auto progressPart =
                static_cast<double>(progressValue - futureProgressMinimum) /
                static_cast<double>(futureProgressRange);

            const auto mappedProgressValue = static_cast<int>(
                std::round(progressPart * promiseProgressRange));

            promise->setProgressValue(std::clamp(
                promiseProgressMinimum + mappedProgressValue,
                promiseProgressMinimum, promiseProgressMaximum));
        });

    QObject::connect(
        futureWatcher.get(), &QFutureWatcher<T>::finished, futureWatcher.get(),
        [futureWatcherWeak = QPointer<QFutureWatcher<T>>(futureWatcher.get())] {
            if (!futureWatcherWeak.isNull()) {
                futureWatcherWeak->deleteLater();
            }
        });

    QObject::connect(
        futureWatcher.get(), &QFutureWatcher<T>::canceled, futureWatcher.get(),
        [futureWatcherWeak = QPointer<QFutureWatcher<T>>(futureWatcher.get())] {
            if (!futureWatcherWeak.isNull()) {
                futureWatcherWeak->deleteLater();
            }
        });

    futureWatcher->setFuture(future);
    Q_UNUSED(futureWatcher.release());
}

} // namespace quentier::threading