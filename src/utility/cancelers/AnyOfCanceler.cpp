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

#include <quentier/utility/cancelers/AnyOfCanceler.h>

#include <algorithm>

namespace quentier::utility::cancelers {

class AnyOfCanceler::Impl
{
public:
    explicit Impl(QList<ICancelerPtr> cancelers) :
        m_cancelers{std::move(cancelers)}
    {}

    QList<ICancelerPtr> m_cancelers;
};

AnyOfCanceler::AnyOfCanceler(QList<ICancelerPtr> cancelers) :
    m_impl{std::make_unique<Impl>(std::move(cancelers))}
{}

AnyOfCanceler::AnyOfCanceler(AnyOfCanceler && other) noexcept = default;

AnyOfCanceler & AnyOfCanceler::operator=(AnyOfCanceler && other) noexcept =
    default;

AnyOfCanceler::~AnyOfCanceler() noexcept = default;

bool AnyOfCanceler::isCanceled() const noexcept
{
    return std::any_of(
        m_impl->m_cancelers.constBegin(), m_impl->m_cancelers.constEnd(),
        [](const ICancelerPtr & canceler) {
            return canceler && canceler->isCanceled();
        });
}

} // namespace quentier::utility::cancelers
