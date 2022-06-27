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

#include <quentier/synchronization/types/ISyncOptionsBuilder.h>

namespace quentier::synchronization {

class SyncOptionsBuilder final : public ISyncOptionsBuilder
{
public:
    ISyncOptionsBuilder & setDownloadNoteThumbnails(
        bool value) noexcept override;

    ISyncOptionsBuilder & setInkNoteImagesStorageDir(
        std::optional<QDir> dir) override;

    [[nodiscard]] ISyncOptionsPtr build() override;

private:
    bool m_downloadNoteThumbnails = false;
    std::optional<QDir> m_inkNoteImagesStorageDir;
};

} // namespace quentier::synchronization
