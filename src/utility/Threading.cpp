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

#include "Threading.h"

#include <QFuture>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include "Qt5Promise.h"
#else
#include <QPromise>
#endif

namespace quentier::utility {

[[nodiscard]] QFuture<void> makeReadyFuture()
{
    QPromise<void> promise;
    QFuture<void> future = promise.future();

    promise.start();
    promise.finish();

    return future;
}

} // namespace quentier::utility
