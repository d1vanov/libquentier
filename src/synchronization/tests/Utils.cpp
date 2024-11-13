/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <qevercloud/IRequestContext.h>

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

void checkRequestContext(
    const qevercloud::IRequestContextPtr & ctx,
    const qevercloud::IRequestContextPtr & expectedCtx)
{
    ASSERT_TRUE(ctx);
    ASSERT_TRUE(expectedCtx);

    EXPECT_NE(ctx, expectedCtx);
    EXPECT_NE(ctx->requestId(), expectedCtx->requestId());
    EXPECT_EQ(ctx->authenticationToken(), expectedCtx->authenticationToken());
    EXPECT_EQ(ctx->cookies(), expectedCtx->cookies());
    EXPECT_EQ(ctx->connectionTimeout(), expectedCtx->connectionTimeout());
    EXPECT_EQ(ctx->maxConnectionTimeout(), expectedCtx->maxConnectionTimeout());
    EXPECT_EQ(ctx->maxRequestRetryCount(), expectedCtx->maxRequestRetryCount());
    EXPECT_EQ(
        ctx->increaseConnectionTimeoutExponentially(),
        expectedCtx->increaseConnectionTimeoutExponentially());
}


} // namespace quentier::synchronization::tests
