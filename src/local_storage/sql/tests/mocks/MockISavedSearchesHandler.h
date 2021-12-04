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

#include <local_storage/sql/ISavedSearchesHandler.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockISavedSearchesHandler : public ISavedSearchesHandler
{
public:
    MOCK_METHOD(QFuture<quint32>, savedSearchCount, (), (const, override));

    MOCK_METHOD(
        QFuture<void>, putSavedSearch, (qevercloud::SavedSearch search),
        (override));

    MOCK_METHOD(
        QFuture<qevercloud::SavedSearch>, findSavedSearchByLocalId,
        (QString localId), (const, override));

    MOCK_METHOD(
        QFuture<qevercloud::SavedSearch>, findSavedSearchByGuid,
        (qevercloud::Guid guid), (const, override));

    MOCK_METHOD(
        QFuture<qevercloud::SavedSearch>, findSavedSearchByName,
        (QString name), (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::SavedSearch>>, listSavedSearches,
        (ListOptions<ListSavedSearchesOrder> options), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeSavedSearchByLocalId, (QString localId),
        (override));

    MOCK_METHOD(
        QFuture<void>, expungeSavedSearchByGuid, (qevercloud::Guid guid),
        (override));
};

} // namespace quentier::local_storage::sql::tests::mocks
