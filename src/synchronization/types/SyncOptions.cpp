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

#include "SyncOptions.h"

namespace quentier::synchronization {

bool SyncOptions::downloadNoteThumbnails() const noexcept
{
    return m_downloadNoteThumbnails;
}

std::optional<QDir> SyncOptions::inkNoteImagesStorageDir() const
{
    return m_inkNoteImagesStorageDir;
}

QTextStream & SyncOptions::print(QTextStream & strm) const
{
    strm << "SyncOptions: downloadNoteThumbnails = "
         << (m_downloadNoteThumbnails ? "true" : "false")
         << ", inkNoteImagesStorageDir = "
         << (m_inkNoteImagesStorageDir
                 ? m_inkNoteImagesStorageDir->absolutePath()
                 : QString::fromUtf8("<not set>"));

    return strm;
}

bool operator==(const SyncOptions & lhs, const SyncOptions & rhs) noexcept
{
    return lhs.m_downloadNoteThumbnails == rhs.m_downloadNoteThumbnails &&
        lhs.m_inkNoteImagesStorageDir == rhs.m_inkNoteImagesStorageDir;
}

bool operator!=(const SyncOptions & lhs, const SyncOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
