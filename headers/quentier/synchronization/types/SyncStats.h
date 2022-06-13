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

#include <QtGlobal>

namespace quentier::synchronization {

/**
 * @brief The SyncStats structure encapsulates counters for various data items
 * representing the result of synchronization process.
 */
struct QUENTIER_EXPORT SyncStats : public Printable
{
    QTextStream & print(QTextStream & strm) const override;

    /**
     * The amount of downloaded sync chunks - containers with other Evernote
     * data items.
     */
    quint64 syncChunksDownloaded = 0;

    /**
     * The amount of downloaded linked notebooks - "pointers" to notebooks
     * belonging to other users which were shared with the current user.
     */
    quint64 linkedNotebooksDownloaded = 0;

    /**
     * The amount of downloaded user's own notebooks.
     */
    quint64 notebooksDownloaded = 0;

    /**
     * The amount of downloaded saved searches.
     */
    quint64 savedSearchesDownloaded = 0;

    /**
     * The amount of downloaded tags.
     */
    quint64 tagsDownloaded = 0;

    /**
     * The amount of downloaded notes.
     */
    quint64 notesDownloaded = 0;

    /**
     * The amount of downloaded resources (attachments to notes).
     */
    quint64 resourcesDownloaded = 0;

    /**
     * The amount of linked notebooks which were expunged during sync.
     */
    quint64 linkedNotebooksExpunged = 0;

    /**
     * The amount of user's own notebooks which were expunged during sync.
     */
    quint64 notebooksExpunged = 0;

    /**
     * The amount of saved searches which were expunged during sync.
     */
    quint64 savedSearchesExpunged = 0;

    /**
     * The amount of tags which were expunged during sync.
     */
    quint64 tagsExpunged = 0;

    /**
     * The amount of notes which were expunged during sync.
     */
    quint64 notesExpunged = 0;

    /**
     * The amount of resources which were expunged during sync
     */
    quint64 resourcesExpunged = 0;

    /**
     * The amount of new or locally updated notebooks which were sent
     * to Evernote during sync.
     */
    quint64 notebooksSent = 0;

    /**
     * The amount of new or locally updated saved searches which were sent
     * to Evernote during sync.
     */
    quint64 savedSearchesSent = 0;

    /**
     * The amount of new or locally updated tags which were sent to Evernote
     * during sync.
     */
    quint64 tagsSent = 0;

    /**
     * The amount of new or locally updated notes which were sent to Evernote
     * during sync.
     */
    quint64 notesSent = 0;
};

[[nodiscard]] QUENTIER_EXPORT bool operator==(
    const SyncStats & lhs, const SyncStats & rhs) noexcept;

[[nodiscard]] QUENTIER_EXPORT bool operator!=(
    const SyncStats & lhs, const SyncStats & rhs) noexcept;

} // namespace quentier::synchronization
