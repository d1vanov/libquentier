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

#include "INotebooksProcessor.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/synchronization/Fwd.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>
#include <optional>

namespace quentier::synchronization {

class NotebooksProcessor final :
    public INotebooksProcessor,
    public std::enable_shared_from_this<NotebooksProcessor>
{
public:
    explicit NotebooksProcessor(
        local_storage::ILocalStoragePtr localStorage,
        ISyncConflictResolverPtr syncConflictResolver);

    [[nodiscard]] QFuture<void> processNotebooks(
        const QList<qevercloud::SyncChunk> & syncChunks) override;

private:
    void onFoundDuplicateByGuid(
        const std::shared_ptr<QPromise<void>> & notebookPromise,
        qevercloud::Notebook updatedNotebook,
        qevercloud::Notebook localNotebook);

private:
    const local_storage::ILocalStoragePtr m_localStorage;
    const ISyncConflictResolverPtr m_syncConflictResolver;
};

} // namespace quentier::synchronization
