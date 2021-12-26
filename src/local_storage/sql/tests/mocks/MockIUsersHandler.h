/*
 * Copyright 2021 Dmitry Ivanov
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

#include <local_storage/sql/IUsersHandler.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockIUsersHandler : public IUsersHandler
{
public:
    MOCK_METHOD(QFuture<quint32>, userCount, (), (const, override));
    MOCK_METHOD(QFuture<void>, putUser, (qevercloud::User user), (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::User>>, findUserById,
        (qevercloud::UserID userId), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeUserById, (qevercloud::UserID userId),
        (override));
};

} // namespace quentier::local_storage::sql::tests::mocks
