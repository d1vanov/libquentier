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

#include <local_storage/sql/IVersionHandler.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockIVersionHandler : public IVersionHandler
{
public:
    MOCK_METHOD(QFuture<bool>, isVersionTooHigh, (), (const, override));
    MOCK_METHOD(QFuture<bool>, requiresUpgrade, (), (const, override));

    MOCK_METHOD(
        QFuture<QList<IPatchPtr>>, requiredPatches, (), (const, override));

    MOCK_METHOD(QFuture<qint32>, version, (), (const, override));

    MOCK_METHOD(
        QFuture<qint32>, highestSupportedVersion, (), (const, override));
};

} // namespace quentier::local_storage::sql::tests::mocks
