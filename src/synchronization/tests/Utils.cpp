/*
 * Copyright 2023 Dmitry Ivanov
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

#include "Utils.h"

#include <gtest/gtest.h>

#include <utility>

// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

void compareGuidLists(
    const QList<qevercloud::Guid> & lhs, const QList<qevercloud::Guid> & rhs)
{
    ASSERT_EQ(lhs.size(), rhs.size());
    for (const auto & l: std::as_const(lhs)) {
        EXPECT_TRUE(rhs.contains(l));
    }
}

} // namespace quentier::synchronization::tests
