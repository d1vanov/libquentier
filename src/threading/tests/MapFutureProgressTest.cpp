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
#include <QThread>

#include <gtest/gtest.h>

// clazy:excludeall=returning-void-expression

namespace quentier::threading::tests {

TEST(MapFutureProgressTest, MapFutureProgress)
{
    QPromise<void> sourcePromise;
    sourcePromise.setProgressRange(0, 20);
    sourcePromise.setProgressValue(0);
    sourcePromise.start();
    auto sourceFuture = sourcePromise.future();

    auto targetPromise = std::make_shared<QPromise<int>>();
    targetPromise->setProgressRange(0, 100);
    targetPromise->setProgressValue(0);
    auto targetFuture = targetPromise->future();

    mapFutureProgress(sourceFuture, targetPromise);

    EXPECT_EQ(targetFuture.progressValue(), 0);

    // QFutureWatcher's doc
    // (https://doc.qt.io/qt-5/qfuturewatcher.html#progressValueChanged) says:
    // "In order to avoid overloading the GUI event loop, QFutureWatcher limits
    // the progress signal emission rate. This means that listeners connected to
    // this slot might not get all progress reports the future makes. The last
    // progress update (where progressValue equals the maximum value) will
    // always be delivered". I've just wasted an hour of my life trying to
    // figure out why the damn subscription doesn't work as it should. Duh.
    // Ok, as a workaround, will check for only one value before max as it seems
    // to work that way. But the test is, of course, much less useful in such
    // conditions.
    sourcePromise.setProgressValue(5);
    QCoreApplication::processEvents();
    EXPECT_EQ(targetFuture.progressValue(), 25);

    sourcePromise.setProgressValue(20);
    QCoreApplication::processEvents();
    EXPECT_EQ(targetFuture.progressValue(), 100);
}

} // namespace quentier::threading::tests
