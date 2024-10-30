/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include <quentier/utility/IKeychainService.h>

#include <gmock/gmock.h>

namespace quentier::utility::tests::mocks {

class MockIKeychainService : public IKeychainService
{
public:
    MOCK_METHOD(
        QFuture<void>, writePassword,
        (QString service, QString key, QString password), (override));

    MOCK_METHOD(
        QFuture<QString>, readPassword, (QString service, QString key),
        (const, override));

    MOCK_METHOD(
        QFuture<void>, deletePassword, (QString service, QString key),
        (override));
};

} // namespace quentier::utility::tests::mocks
