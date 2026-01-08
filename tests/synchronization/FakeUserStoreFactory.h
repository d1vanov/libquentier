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

#pragma once

#include "Fwd.h"

#include <quentier/synchronization/IUserStoreFactory.h>

namespace quentier::synchronization::tests {

class FakeUserStoreFactory : public IUserStoreFactory
{
public:
    explicit FakeUserStoreFactory(FakeUserStoreBackend * backend);

public: // IUserStoreFactory
    [[nodiscard]] qevercloud::IUserStorePtr createUserStore(
        QString userStoreUrl = {}, qevercloud::IRequestContextPtr ctx = {},
        qevercloud::IRetryPolicyPtr retryPolicy = {}) override;

private:
    FakeUserStoreBackend * m_backend;
};

} // namespace quentier::synchronization::tests
