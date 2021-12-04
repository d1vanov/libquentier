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

#include <local_storage/sql/INotebooksHandler.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockINotebooksHandler : public INotebooksHandler
{
public:
    MOCK_METHOD(QFuture<quint32>, notebookCount, (), (const, override));

    MOCK_METHOD(
        QFuture<void>, putNotebook, (qevercloud::Notebook notebook),
        (override));

    MOCK_METHOD(
        QFuture<qevercloud::Notebook>, findNotebookByLocalId, (QString localId),
        (const, override));

    MOCK_METHOD(
        QFuture<qevercloud::Notebook>, findNotebookByGuid,
        (qevercloud::Guid guid), (const, override));

    MOCK_METHOD(
        QFuture<qevercloud::Notebook>, findNotebookByName,
        (QString name, std::optional<qevercloud::Guid> linkedNotebookGuid),
        (const, override));

    MOCK_METHOD(
        QFuture<qevercloud::Notebook>, findDefaultNotebook, (),
        (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeNotebookByLocalId, (QString localId), (override));

    MOCK_METHOD(
        QFuture<void>, expungeNotebookByGuid, (qevercloud::Guid guid),
        (override));

    MOCK_METHOD(
        QFuture<void>, expungeNotebookByName,
        (QString name, std::optional<qevercloud::Guid> linkedNotebookGuid),
        (override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Notebook>>, listNotebooks,
        (ListOptions<ListNotebooksOrder> options), (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::SharedNotebook>>, listSharedNotebooks,
        (qevercloud::Guid notebookGuid), (const, override));
};

} // namespace quentier::local_storage::sql::tests::mocks
