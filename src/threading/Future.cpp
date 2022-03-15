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

#include <quentier/threading/Future.h>

namespace quentier::threading {

QFuture<void> makeReadyFuture()
{
    QPromise<void> promise;
    QFuture<void> future = promise.future();

    promise.start();
    promise.finish();

    return future;
}

QFuture<void> whenAll(QList<QFuture<void>> futures)
{
    if (Q_UNLIKELY(futures.isEmpty())) {
        return makeReadyFuture();
    }

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    const int totalItemCount = futures.size();
    promise->setProgressRange(0, totalItemCount);
    promise->setProgressValue(0);

    promise->start();

    auto processedItemsCount = std::make_shared<int>(0);
    auto exceptionFlag = std::make_shared<bool>(false);
    auto mutex = std::make_shared<QMutex>();

    for (auto & future: futures) {
        auto thenFuture = threading::then(
            std::move(future),
            [promise, processedItemsCount, totalItemCount, exceptionFlag,
             mutex] {
                int count = 0;
                {
                    const QMutexLocker locker{mutex.get()};

                    if (*exceptionFlag) {
                        return;
                    }

                    ++(*processedItemsCount);
                    count = *processedItemsCount;
                    promise->setProgressValue(count);
                }

                if (count == totalItemCount) {
                    promise->finish();
                }
            });

        threading::onFailed(
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

} // namespace quentier::threading
