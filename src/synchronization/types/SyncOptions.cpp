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

#include <quentier/synchronization/types/SyncOptions.h>

namespace quentier::synchronization {

bool operator==(const SyncOptions & lhs, const SyncOptions & rhs) noexcept
{
    return lhs.downloadNoteThumbnails == rhs.downloadNoteThumbnails &&
        lhs.inkNoteImagesStorageDir == rhs.inkNoteImagesStorageDir;
}

bool operator!=(const SyncOptions & lhs, const SyncOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

QTextStream & SyncOptions::print(QTextStream & strm) const
{
    strm << "SyncOptions: downloadNoteThumbnails = "
         << (downloadNoteThumbnails ? "true" : "false")
         << ", inkNoteImagesStorageDir = "
         << (inkNoteImagesStorageDir ? inkNoteImagesStorageDir->absolutePath()
                                     : QString::fromUtf8("<not set>"));

    return strm;
}

} // namespace quentier::synchronization
