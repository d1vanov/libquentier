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

#include <qevercloud/types/Resource.h>

#include <QFuture>

#include <optional>

namespace quentier::local_storage::sql {

class IResourcesHandler
{
public:
    virtual ~IResourcesHandler() = default;

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    [[nodiscard]] virtual QFuture<quint32> resourceCount(
        NoteCountOptions options = NoteCountOptions{
            NoteCountOption::IncludeNonDeletedNotes}) const = 0;

    [[nodiscard]] virtual QFuture<quint32> resourceCountPerNoteLocalId(
        QString noteLocalId) const = 0;

    [[nodiscard]] virtual QFuture<void> putResource(
        qevercloud::Resource resource, int indexInNote) = 0;

    [[nodiscard]] virtual QFuture<void> putResourceMetadata(
        qevercloud::Resource resource, int indexInNote) = 0;

    using FetchResourceOption = ILocalStorage::FetchResourceOption;
    using FetchResourceOptions = ILocalStorage::FetchResourceOptions;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Resource>>
        findResourceByLocalId(
            QString resourceLocalId,
            FetchResourceOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<std::optional<qevercloud::Resource>>
        findResourceByGuid(
            qevercloud::Guid resourceGuid,
            FetchResourceOptions options = {}) const = 0;

    [[nodiscard]] virtual QFuture<void> expungeResourceByLocalId(
        QString resourceLocalId) = 0;

    [[nodiscard]] virtual QFuture<void> expungeResourceByGuid(
        qevercloud::Guid resourceGuid) = 0;
};

} // namespace quentier::local_storage::sql
