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

#include "ISyncChunksDownloader.h"

#include <synchronization/Fwd.h>

#include <qevercloud/Fwd.h>

namespace quentier::synchronization {

class SyncChunksDownloader final : public ISyncChunksDownloader
{
public:
    explicit SyncChunksDownloader(
        INoteStoreProviderPtr noteStoreProvider,
        qevercloud::IRetryPolicyPtr retryPolicy = {});

    [[nodiscard]] QFuture<SyncChunksResult> downloadSyncChunks(
        qint32 afterUsn, SynchronizationMode syncMode,
        qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

    [[nodiscard]] QFuture<SyncChunksResult> downloadLinkedNotebookSyncChunks(
        qevercloud::LinkedNotebook linkedNotebook, qint32 afterUsn,
        SynchronizationMode syncMode, qevercloud::IRequestContextPtr ctx,
        utility::cancelers::ICancelerPtr canceler,
        ICallbackWeakPtr callbackWeak) override;

private:
    const INoteStoreProviderPtr m_noteStoreProvider;
    const qevercloud::IRetryPolicyPtr m_retryPolicy;
};

} // namespace quentier::synchronization
