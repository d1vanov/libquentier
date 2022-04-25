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

#include <quentier/utility/cancelers/ManualCanceler.h>

namespace quentier::utility::cancelers {

void ManualCanceler::cancel()
{
    m_canceled.store(true, std::memory_order_release);
}

bool ManualCanceler::isCanceled() const noexcept
{
    return m_canceled.load(std::memory_order_acquire);
}

} // namespace quentier::utility::cancelers
