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

#include <atomic>
#include <memory>

namespace quentier::utility::cancelers {

/**
 * ICanceler which allows one to manually call cancel method to cancel
 * some task
 */
class QUENTIER_EXPORT ManualCanceler : public ICanceler
{
public:
    ManualCanceler();
    ManualCanceler(ManualCanceler && other) noexcept;
    ManualCanceler & operator=(ManualCanceler && other) noexcept;
    ~ManualCanceler() noexcept override;

    /**
     * Manually cancel a task
     */
    void cancel() noexcept;

    [[nodiscard]] bool isCanceled() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace quentier::utility::cancelers
