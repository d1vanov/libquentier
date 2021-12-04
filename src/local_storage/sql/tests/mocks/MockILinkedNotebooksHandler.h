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

#include <local_storage/sql/ILinkedNotebooksHandler.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockILinkedNotebooksHandler : public ILinkedNotebooksHandler
{
public:
    MOCK_METHOD(QFuture<quint32>, linkedNotebookCount, (), (const, override));

    MOCK_METHOD(
        QFuture<void>, putLinkedNotebook,
        (qevercloud::LinkedNotebook linkedNotebook), (override));

    MOCK_METHOD(
        QFuture<qevercloud::LinkedNotebook>, findLinkedNotebookByGuid,
        (qevercloud::Guid guid), (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeLinkedNotebookByGuid,
        (qevercloud::Guid guid), (override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::LinkedNotebook>>, listLinkedNotebooks,
        (ListOptions<ListLinkedNotebooksOrder> options), (const, override));
};

} // namespace quentier::local_storage::sql::tests::mocks
