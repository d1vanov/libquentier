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

#include "Fwd.h"
#include "IFullSyncStaleDataExpunger.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/utility/cancelers/Fwd.h>

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/TypeAliases.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QHash>

#include <memory>
#include <utility>

namespace quentier::synchronization {

class FullSyncStaleDataExpunger final :
    public IFullSyncStaleDataExpunger,
    public std::enable_shared_from_this<FullSyncStaleDataExpunger>
{
public:
    explicit FullSyncStaleDataExpunger(
        local_storage::ILocalStoragePtr localStorage);

    [[nodiscard]] QFuture<void> expungeStaleData(
        PreservedGuids preservedGuids,
        utility::cancelers::ICancelerPtr canceler,
        std::optional<qevercloud::Guid> linkedNotebookGuid = {}) override;

private:
    struct Guids
    {
        QSet<qevercloud::Guid> locallyModifiedNotebookGuids;
        QSet<qevercloud::Guid> unmodifiedNotebookGuids;

        QSet<qevercloud::Guid> locallyModifiedTagGuids;
        QSet<qevercloud::Guid> unmodifiedTagGuids;

        QSet<qevercloud::Guid> locallyModifiedNoteGuids;
        QSet<qevercloud::Guid> unmodifiedNoteGuids;

        QSet<qevercloud::Guid> locallyModifiedSavedSearchGuids;
        QSet<qevercloud::Guid> unmodifiedSavedSearchGuids;
    };

    using GuidWithLocalId = std::pair<qevercloud::Guid, QString>;
    using GuidToLocalIdHash = QHash<qevercloud::Guid, QString>;

    struct TagData
    {
        QString oldLocalId;
        QString newLocalId;
    };

    using GuidWithTagData = std::pair<qevercloud::Guid, TagData>;
    using GuidToTagDataHash = QHash<qevercloud::Guid, TagData>;

    void onGuidsListed(
        const Guids & guids, const PreservedGuids & preservedGuids,
        utility::cancelers::ICancelerPtr canceler,
        std::optional<qevercloud::Guid> linkedNotebookGuid,
        const std::shared_ptr<QPromise<void>> & promise);

    // returns a map from notebook guids passed into the method to notebook
    // local ids corresponding to newly created local notebooks
    [[nodiscard]] QFuture<GuidToLocalIdHash> processModifiedNotebooks(
        const QSet<qevercloud::Guid> & notebookGuids,
        utility::cancelers::ICancelerPtr canceler,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid);

    // returns a map from tag guids passed into the method to tag local ids
    // corresponding to newly created local tags
    [[nodiscard]] QFuture<GuidToTagDataHash> processModifiedTags(
        const QSet<qevercloud::Guid> & tagGuids,
        utility::cancelers::ICancelerPtr canceler,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid);

    [[nodiscard]] QFuture<void> processModifiedSavedSearches(
        const QSet<qevercloud::Guid> & savedSearchGuids,
        const utility::cancelers::ICancelerPtr & canceler);

    [[nodiscard]] QFuture<void> processModifiedNotes(
        const QSet<qevercloud::Guid> & noteGuids,
        const utility::cancelers::ICancelerPtr & canceler,
        const std::shared_ptr<const GuidToLocalIdHash> & newNotebooksMap,
        const std::shared_ptr<const GuidToTagDataHash> & newTagsMap);

    void processModifiedNotebook(
        std::optional<qevercloud::Notebook> notebook,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        const qevercloud::Guid & guid,
        const std::shared_ptr<QPromise<GuidWithLocalId>> & promise,
        const utility::cancelers::ICancelerPtr & canceler);

    void processModifiedTag(
        std::optional<qevercloud::Tag> tag,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid,
        const qevercloud::Guid & guid,
        const std::shared_ptr<QPromise<GuidWithTagData>> & promise,
        const utility::cancelers::ICancelerPtr & canceler);

    void processModifiedSavedSearch(
        std::optional<qevercloud::SavedSearch> savedSearch,
        const qevercloud::Guid & guid,
        const std::shared_ptr<QPromise<void>> & promise,
        const utility::cancelers::ICancelerPtr & canceler);

    void processModifiedNote(
        std::optional<qevercloud::Note> note,
        const qevercloud::Guid & guid,
        const std::shared_ptr<const GuidToLocalIdHash> & newNotebooksMap,
        const std::shared_ptr<const GuidToTagDataHash> & newTagsMap,
        const std::shared_ptr<QPromise<void>> & promise,
        const utility::cancelers::ICancelerPtr & canceler);

private:
    const local_storage::ILocalStoragePtr m_localStorage;
};

} // namespace quentier::synchronization
