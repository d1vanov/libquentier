/*
 * Copyright 2024 Dmitry Ivanov
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

#include "FakeUserStoreFactory.h"
#include "FakeUserStore.h"

namespace quentier::synchronization::tests {

FakeUserStoreFactory::FakeUserStoreFactory(FakeUserStoreBackend * backend) :
    m_backend{backend}
{}

qevercloud::IUserStorePtr FakeUserStoreFactory::createUserStore(
    QString userStoreUrl, qevercloud::IRequestContextPtr ctx,
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    return std::make_shared<FakeUserStore>(
        m_backend, std::move(userStoreUrl), std::move(ctx),
        std::move(retryPolicy));
}

} // namespace quentier::synchronization::tests
