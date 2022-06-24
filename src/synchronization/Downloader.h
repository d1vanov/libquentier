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

#include "IDownloader.h"
#include "Fwd.h"

#include <QDir>

namespace quentier::synchronization {

class Downloader final :
    public IDownloader,
    public std::enable_shared_from_this<Downloader>
{
public:
    Downloader(
        ISyncChunksProviderPtr syncChunksProvider,
        ILinkedNotebooksProcessorPtr linkedNotebooksProcessor,
        INotebooksProcessorPtr notebooksProcessor,
        INotesProcessorPtr notesProcessor,
        IResourcesProcessorPtr resourcesProcessor,
        ISavedSearchesProcessorPtr savedSearchesProcessor,
        ITagsProcessorPtr tagsProcessor,
        const QDir & syncPersistentStorageDir);

    [[nodiscard]] QFuture<Result> download() override;

private:
    ISyncChunksProviderPtr m_syncChunksProvider;
    ILinkedNotebooksProcessorPtr m_linkedNotebooksProcessor;
    INotebooksProcessorPtr m_notebooksProcessor;
    INotesProcessorPtr m_notesProcessor;
    IResourcesProcessorPtr m_resourcesProcessor;
    ISavedSearchesProcessorPtr m_savedSearchesProcessor;
    ITagsProcessorPtr m_tagsProcessor;
    QDir m_syncPersistentStorageDir;
};

} // namespace quentier::synchronization
