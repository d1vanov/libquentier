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

#include "SyncOptionsBuilder.h"
#include "SyncOptions.h"

#include <memory>

namespace quentier::synchronization {

ISyncOptionsBuilder & SyncOptionsBuilder::setDownloadNoteThumbnails(
    bool value) noexcept
{
    m_downloadNoteThumbnails = value;
    return *this;
}

ISyncOptionsBuilder & SyncOptionsBuilder::setInkNoteImagesStorageDir(
    std::optional<QDir> dir)
{
    m_inkNoteImagesStorageDir = dir;
    return *this;
}

ISyncOptionsPtr SyncOptionsBuilder::build()
{
    auto options = std::make_shared<SyncOptions>();
    options->m_downloadNoteThumbnails = m_downloadNoteThumbnails;
    options->m_inkNoteImagesStorageDir = m_inkNoteImagesStorageDir;

    m_downloadNoteThumbnails = false;
    m_inkNoteImagesStorageDir.reset();

    return options;
}

} // namespace quentier::synchronization
