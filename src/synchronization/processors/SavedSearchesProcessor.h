/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include "ISavedSearchesProcessor.h"

#include <synchronization/Fwd.h>

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>
#include <quentier/threading/Fwd.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <qevercloud/types/TypeAliases.h>

#include <optional>

namespace quentier::synchronization {

class SavedSearchesProcessor final :
    public ISavedSearchesProcessor,
    public std::enable_shared_from_this<SavedSearchesProcessor>
{
public:
    explicit SavedSearchesProcessor(
        local_storage::ILocalStoragePtr localStorage,
        ISyncConflictResolverPtr syncConflictResolver);

    [[nodiscard]] QFuture<void> processSavedSearches(
        const QList<qevercloud::SyncChunk> & syncChunks,
        ICallbackWeakPtr callbackWeak) override;

private:
    class SavedSearchCounters;

    void tryToFindDuplicateByName(
        const std::shared_ptr<QPromise<void>> & savedSearchPromise,
        const std::shared_ptr<SavedSearchCounters> & savedSearchCounters,
        qevercloud::SavedSearch updatedSavedSearch);

    void onFoundDuplicate(
        const std::shared_ptr<QPromise<void>> & savedSearchPromise,
        const std::shared_ptr<SavedSearchCounters> & savedSearchCounters,
        qevercloud::SavedSearch updatedSavedSearch,
        qevercloud::SavedSearch localSavedSearch);

    void renameLocalConflictingSavedSearch(
        const std::shared_ptr<QPromise<void>> & savedSearchPromise,
        const std::shared_ptr<SavedSearchCounters> & savedSearchCounters,
        qevercloud::SavedSearch updatedSavedSearch,
        qevercloud::SavedSearch renamedLocalSavedSearch,
        const QString & localConflictingSavedSearchLocalId);

private:
    const local_storage::ILocalStoragePtr m_localStorage;
    const ISyncConflictResolverPtr m_syncConflictResolver;
};

} // namespace quentier::synchronization
