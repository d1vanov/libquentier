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

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/local_storage/ILocalStorage.h>

#include <qevercloud/types/Tag.h>

#include <optional>

namespace quentier::local_storage::sql {

class ITagsHandler
{
public:
    virtual ~ITagsHandler() = default;

    [[nodiscard]] virtual QFuture<quint32> tagCount() const = 0;
    [[nodiscard]] virtual QFuture<void> putTag(qevercloud::Tag tag) = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Tag>>
        findTagByLocalId(QString tagLocalId) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Tag>> findTagByGuid(
        qevercloud::Guid tagGuid) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Tag>> findTagByName(
        QString tagName,
        std::optional<qevercloud::Guid> linkedNotebookGuid =
            std::nullopt) const = 0;

    using ListTagsOptions = ILocalStorage::ListTagsOptions;
    using ListTagsOrder = ILocalStorage::ListTagsOrder;
    using TagNotesRelation = ILocalStorage::TagNotesRelation;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Tag>> listTags(
        ListTagsOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<QList<qevercloud::Tag>>
        listTagsPerNoteLocalId(
            QString noteLocalId, ListTagsOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeTagByLocalId(
        QString tagLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeTagByGuid(
        qevercloud::Guid tagGuid) = 0;

    [[nodiscard]] virtual QFuture<void> expungeTagByName(
        QString name,
        std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt) = 0;
};

} // namespace quentier::local_storage::sql
