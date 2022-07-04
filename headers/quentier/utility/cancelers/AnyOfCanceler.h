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

#include <quentier/utility/cancelers/Fwd.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <QList>

namespace quentier::utility::cancelers {

class QUENTIER_EXPORT AnyOfCanceler : public ICanceler
{
public:
    explicit AnyOfCanceler(QList<ICancelerPtr> cancelers);
    AnyOfCanceler(AnyOfCanceler && other) noexcept;
    AnyOfCanceler & operator=(AnyOfCanceler && other) noexcept;
    ~AnyOfCanceler() noexcept override;

    [[nodiscard]] bool isCanceled() const noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace quentier::utility::cancelers
