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

#include <QFutureWatcher>

#include <memory>

namespace quentier::threading {

/**
 * Delete later deleter function for QFutureWatchers
 */
template <class T>
void deleteFutureWatcherLater(QFutureWatcher<T> * watcher) noexcept
{
    watcher->deleteLater();
}

/**
 * Create QFutureWatcher which would be deleter through a call to deleteLater
 */
template <class T>
[[nodiscard]] std::shared_ptr<QFutureWatcher<T>> makeFutureWatcher()
{
    return std::shared_ptr<QFutureWatcher<T>>(
        new QFutureWatcher<T>, deleteFutureWatcherLater<T>);
}

} // namespace quentier::threading
