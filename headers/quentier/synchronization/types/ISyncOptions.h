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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <QDir>

#include <optional>

namespace quentier::synchronization {

/**
 * @brief Options for synchronization process
 */
class QUENTIER_EXPORT ISyncOptions : public Printable
{
public:
    /**
     * Flag to enable or disable downloading of note thumbnails during the sync.
     * Note thumbnails are stored inside the local storage along with other
     * note data.
     */
    [[nodiscard]] virtual bool downloadNoteThumbnails() const = 0;

    /**
     * Directory to store the downloaded ink note images in. If not set, ink
     * note images would not be downloaded during the sync.
     *
     * Ink notes images data is stored inside note's resources but the format
     * of the data is not documented, which makes it nearly impossible to
     * implement note editor able to fully handle ink notes. What is possible
     * is to visualize a static image corresponding to the last revision of
     * the ink note. Such images need to be downloaded separately during the
     * sync if they are required.
     *
     * TODO: clarify the storage layout within this directory.
     */
    [[nodiscard]] virtual std::optional<QDir> inkNoteImagesStorageDir()
        const = 0;
};

} // namespace quentier::synchronization
