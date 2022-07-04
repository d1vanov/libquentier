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

#pragma once

#include <quentier/utility/cancelers/ICanceler.h>

#include <QFuture>

namespace quentier::utility::cancelers {

/**
 * ICanceler implementation which tracks the canceled status of a future.
 */
template <class T>
class FutureCanceler : public ICanceler
{
public:
    explicit FutureCanceler(QFuture<T> future) : m_future{std::move(future)} {}

    [[nodiscard]] bool isCanceled() const noexcept override
    {
        return m_future.isCanceled();
    }

private:
    QFuture<T> m_future;
};

} // namespace quentier::utility::cancelers
