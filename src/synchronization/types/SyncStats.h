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

#include <quentier/synchronization/types/ISyncStats.h>

namespace quentier::synchronization {

struct SyncStats final : public ISyncStats
{
    [[nodiscard]] quint64 syncChunksDownloaded() const noexcept override;
    [[nodiscard]] quint64 linkedNotebooksDownloaded() const noexcept override;
    [[nodiscard]] quint64 notebooksDownloaded() const noexcept override;
    [[nodiscard]] quint64 savedSearchesDownloaded() const noexcept override;
    [[nodiscard]] quint64 tagsDownloaded() const noexcept override;
    [[nodiscard]] quint64 notesDownloaded() const noexcept override;
    [[nodiscard]] quint64 resourcesDownloaded() const noexcept override;
    [[nodiscard]] quint64 linkedNotebooksExpunged() const noexcept override;
    [[nodiscard]] quint64 notebooksExpunged() const noexcept override;
    [[nodiscard]] quint64 savedSearchesExpunged() const noexcept override;
    [[nodiscard]] quint64 tagsExpunged() const noexcept override;
    [[nodiscard]] quint64 notesExpunged() const noexcept override;
    [[nodiscard]] quint64 resourcesExpunged() const noexcept override;
    [[nodiscard]] quint64 notebooksSent() const noexcept override;
    [[nodiscard]] quint64 savedSearchesSent() const noexcept override;
    [[nodiscard]] quint64 tagsSent() const noexcept override;
    [[nodiscard]] quint64 notesSent() const noexcept override;

    QTextStream & print(QTextStream & strm) const override;

    quint64 m_syncChunksDownloaded = 0;
    quint64 m_linkedNotebooksDownloaded = 0;
    quint64 m_notebooksDownloaded = 0;
    quint64 m_savedSearchesDownloaded = 0;
    quint64 m_tagsDownloaded = 0;
    quint64 m_notesDownloaded = 0;
    quint64 m_resourcesDownloaded = 0;
    quint64 m_linkedNotebooksExpunged = 0;
    quint64 m_notebooksExpunged = 0;
    quint64 m_savedSearchesExpunged = 0;
    quint64 m_tagsExpunged = 0;
    quint64 m_notesExpunged = 0;
    quint64 m_resourcesExpunged = 0;
    quint64 m_notebooksSent = 0;
    quint64 m_savedSearchesSent = 0;
    quint64 m_tagsSent = 0;
    quint64 m_notesSent = 0;
};

[[nodiscard]] bool operator==(
    const SyncStats & lhs, const SyncStats & rhs) noexcept;

[[nodiscard]] bool operator!=(
    const SyncStats & lhs, const SyncStats & rhs) noexcept;

} // namespace quentier::synchronization
