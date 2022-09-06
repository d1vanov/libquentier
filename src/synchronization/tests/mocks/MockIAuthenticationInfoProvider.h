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

#include <synchronization/IAuthenticationInfoProvider.h>

#include <gmock/gmock.h>

namespace quentier::synchronization::tests::mocks {

class MockIAuthenticationInfoProvider : public IAuthenticationInfoProvider
{
public:
    MOCK_METHOD(
        QFuture<IAuthenticationInfoPtr>, authenticateNewAccount, (),
        (override));

    MOCK_METHOD(
        QFuture<IAuthenticationInfoPtr>, authenticateAccount,
        (Account account, Mode mode), (override));

    MOCK_METHOD(
        QFuture<IAuthenticationInfoPtr>, authenticateToLinkedNotebook,
        (Account account, qevercloud::LinkedNotebook linkedNotebook, Mode mode),
        (override));
};

} // namespace quentier::synchronization::tests::mocks