/*
 * Copyright 2021-2022 Dmitry Ivanov
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

#include <local_storage/sql/ITagsHandler.h>

#include <gmock/gmock.h>

namespace quentier::local_storage::sql::tests::mocks {

class MockITagsHandler : public ITagsHandler
{
public:
    MOCK_METHOD(QFuture<quint32>, tagCount, (), (const, override));
    MOCK_METHOD(QFuture<void>, putTag, (qevercloud::Tag tag), (override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Tag>>, findTagByLocalId,
        (QString tagLocalId), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Tag>>, findTagByGuid,
        (qevercloud::Guid tagGuid), (const, override));

    MOCK_METHOD(
        QFuture<std::optional<qevercloud::Tag>>, findTagByName,
        (QString tagName, std::optional<QString> linkedNotebookGuid),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Tag>>, listTags, (ListTagsOptions options),
        (const, override));

    MOCK_METHOD(
        QFuture<QList<qevercloud::Tag>>, listTagsPerNoteLocalId,
        (QString noteLocalId, ListTagsOptions options), (const, override));

    MOCK_METHOD(
        QFuture<QSet<qevercloud::Guid>>, listTagGuids,
        (ListGuidsFilters filters,
         std::optional<qevercloud::Guid> linkedNotebookGuid),
        (const, override));

    MOCK_METHOD(
        QFuture<void>, expungeTagByLocalId, (QString tagLocalId), (override));

    MOCK_METHOD(
        QFuture<void>, expungeTagByGuid, (qevercloud::Guid tagGuid),
        (override));

    MOCK_METHOD(
        QFuture<void>, expungeTagByName,
        (QString name, std::optional<qevercloud::Guid> linkedNotebookGuid),
        (override));
};

} // namespace quentier::local_storage::sql::tests::mocks
