/*
 * Copyright 2022 Dmitry Ivanov
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

#include <QCoreApplication>

#include <gtest/gtest.h>

// clazy:excludeall=returning-void-expression

namespace quentier::threading::tests {

template <class T>
std::enable_if_t<!std::is_void_v<T>, QFuture<QList<T>>> whenAll(
    QList<QFuture<T>> futures)
{
    return threading::whenAll<T>(std::move(futures));
}

template <class T>
std::enable_if_t<std::is_void_v<T>, QFuture<void>> whenAll(
    QList<QFuture<void>> futures)
{
    return threading::whenAll(std::move(futures));
}

template <class T>
void readyOnlyWhenAllFuturesAreReady()
{
    const int futureCount = 5;

    QList<std::shared_ptr<QPromise<T>>> promises;
    promises.reserve(futureCount);

    QList<QFuture<T>> futures;
    futures.reserve(futureCount);

    for (int i = 0; i < futureCount; ++i) {
        promises << std::make_shared<QPromise<T>>();
        futures << promises.back()->future();
    }

    auto allFuture = whenAll<T>(futures);
    EXPECT_FALSE(allFuture.isFinished());

    for (int i = 0; i < futureCount; ++i) {
        auto & promise = promises[i];
        promise->start();

        if constexpr (!std::is_void_v<T>) {
            promise->addResult(T{});
        }

        promise->finish();

        QCoreApplication::processEvents();

        EXPECT_EQ(allFuture.isFinished(), i == futureCount - 1);
    }

    EXPECT_TRUE(allFuture.isFinished());
}

TEST(VoidFutureWhenAllTest, ReadyOnlyWhenAllFuturesAreReady)
{
    readyOnlyWhenAllFuturesAreReady<void>();
}

TEST(NonVoidFutureWhenAllTest, ReadyOnlyWhenAllFuturesAreReady)
{
    readyOnlyWhenAllFuturesAreReady<int>();
}

template <class T>
void trackProgressOfSourceFuturesFinishingCorrectly()
{
    const int futureCount = 5;

    QList<std::shared_ptr<QPromise<T>>> promises;
    promises.reserve(futureCount);

    QList<QFuture<T>> futures;
    futures.reserve(futureCount);

    for (int i = 0; i < futureCount; ++i) {
        promises << std::make_shared<QPromise<T>>();
        futures << promises.back()->future();
    }

    auto allFuture = whenAll<T>(futures);
    EXPECT_EQ(allFuture.progressMinimum(), 0);
    EXPECT_EQ(allFuture.progressMaximum(), futureCount);

    for (int i = 0; i < futureCount; ++i) {
        auto & promise = promises[i];
        promise->start();

        if constexpr (!std::is_void_v<T>) {
            promise->addResult(T{});
        }

        promise->finish();

        QCoreApplication::processEvents();

        EXPECT_EQ(allFuture.progressValue(), i + 1);
    }

    EXPECT_EQ(allFuture.progressValue(), futureCount);
}

TEST(VoidFutureWhenAllTest, TrackProgressOfSourceFuturesFinishingCorrectly)
{
    trackProgressOfSourceFuturesFinishingCorrectly<void>();
}

TEST(NonVoidFutureWhenAllTest, TrackProgressOfSourceFuturesFinishingCorrectly)
{
    trackProgressOfSourceFuturesFinishingCorrectly<int>();
}

} // namespace quentier::threading::tests
