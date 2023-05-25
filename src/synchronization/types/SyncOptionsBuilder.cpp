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

#include "SyncOptionsBuilder.h"
#include "SyncOptions.h"

#include <memory>

namespace quentier::synchronization {

ISyncOptionsBuilder & SyncOptionsBuilder::setDownloadNoteThumbnails(
    const bool value) noexcept
{
    m_downloadNoteThumbnails = value;
    return *this;
}

ISyncOptionsBuilder & SyncOptionsBuilder::setInkNoteImagesStorageDir(
    const std::optional<QDir> dir)
{
    m_inkNoteImagesStorageDir = dir;
    return *this;
}

ISyncOptionsBuilder & SyncOptionsBuilder::setRequestContext(
    qevercloud::IRequestContextPtr ctx)
{
    m_ctx = std::move(ctx);
    return *this;
}

ISyncOptionsBuilder & SyncOptionsBuilder::setRetryPolicy(
    qevercloud::IRetryPolicyPtr retryPolicy)
{
    m_retryPolicy = std::move(retryPolicy);
    return *this;
}

ISyncOptionsPtr SyncOptionsBuilder::build()
{
    auto options = std::make_shared<SyncOptions>();
    options->m_downloadNoteThumbnails = m_downloadNoteThumbnails;
    options->m_inkNoteImagesStorageDir = m_inkNoteImagesStorageDir;
    options->m_ctx = std::move(m_ctx);
    options->m_retryPolicy = std::move(m_retryPolicy);

    m_downloadNoteThumbnails = false;
    m_inkNoteImagesStorageDir.reset();
    m_ctx.reset();
    m_retryPolicy.reset();

    return options;
}

} // namespace quentier::synchronization
