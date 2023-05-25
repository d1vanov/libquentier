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

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/Fwd.h>

#include <QDir>

#include <optional>

namespace quentier::synchronization {

/**
 * @brief Options for synchronization process
 */
class QUENTIER_EXPORT ISyncOptions : public Printable
{
public:
    ~ISyncOptions() noexcept override;

    /**
     * Flag to enable or disable downloading of note thumbnails during the sync.
     * Note thumbnails are stored inside the local storage along with other
     * note data.
     */
    [[nodiscard]] virtual bool downloadNoteThumbnails() const = 0;

    /**
     * Directory to store the downloaded ink note images. If this method returns
     * std::nullopt, ink note images would not be downloaded during the sync.
     *
     * Ink notes images data is stored inside note's resources but the format
     * of the data is not documented, which makes it quite hard to implement
     * note editor able to fully handle ink notes. An easier option is to
     * visualize a static image corresponding to the last revision of
     * the ink note. Such images need to be downloaded separately during the
     * sync if they are required.
     *
     * Ink note images are stored right in this directory without any
     * subdirectories, file names correspond to pattern <resource guid>.png.
     */
    [[nodiscard]] virtual std::optional<QDir> inkNoteImagesStorageDir()
        const = 0;

    /**
     * Request context with settings which should be used during the sync. If
     * nullptr then request context with default settings would be used.
     */
    [[nodiscard]] virtual qevercloud::IRequestContextPtr requestContext()
        const = 0;

    /**
     * Retry policy which should be used during the sync. If nullptr then the
     * default retry policy would be used.
     */
    [[nodiscard]] virtual qevercloud::IRetryPolicyPtr retryPolicy() const = 0;
};

} // namespace quentier::synchronization
