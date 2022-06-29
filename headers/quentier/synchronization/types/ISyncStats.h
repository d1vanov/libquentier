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
 * @brief The ISyncStats interface provides counters for various data items
 * representing the result of synchronization process.
 */
class QUENTIER_EXPORT ISyncStats : public Printable
{
    /**
     * The amount of downloaded sync chunks - containers with other Evernote
     * data items.
     */
    [[nodiscard]] virtual quint64 syncChunksDownloaded() const = 0;

    /**
     * The amount of downloaded linked notebooks - "pointers" to notebooks
     * belonging to other users which were shared with the current user.
     */
    [[nodiscard]] virtual quint64 linkedNotebooksDownloaded() const = 0;

    /**
     * The amount of downloaded user's own notebooks.
     */
    [[nodiscard]] virtual quint64 notebooksDownloaded() const = 0;

    /**
     * The amount of downloaded saved searches.
     */
    [[nodiscard]] virtual quint64 savedSearchesDownloaded() const = 0;

    /**
     * The amount of downloaded tags.
     */
    [[nodiscard]] virtual quint64 tagsDownloaded() const = 0;

    /**
     * The amount of downloaded notes.
     */
    [[nodiscard]] virtual quint64 notesDownloaded() const = 0;

    /**
     * The amount of downloaded resources (attachments to notes).
     */
    [[nodiscard]] virtual quint64 resourcesDownloaded() const = 0;

    /**
     * The amount of linked notebooks which were expunged during sync.
     */
    [[nodiscard]] virtual quint64 linkedNotebooksExpunged() const = 0;

    /**
     * The amount of user's own notebooks which were expunged during sync.
     */
    [[nodiscard]] virtual quint64 notebooksExpunged() const = 0;

    /**
     * The amount of saved searches which were expunged during sync.
     */
    [[nodiscard]] virtual quint64 savedSearchesExpunged() const = 0;

    /**
     * The amount of tags which were expunged during sync.
     */
    [[nodiscard]] virtual quint64 tagsExpunged() const = 0;

    /**
     * The amount of notes which were expunged during sync.
     */
    [[nodiscard]] virtual quint64 notesExpunged() const = 0;

    /**
     * The amount of resources which were expunged during sync
     */
    [[nodiscard]] virtual quint64 resourcesExpunged() const = 0;

    /**
     * The amount of new or locally updated notebooks which were sent
     * to Evernote during sync.
     */
    [[nodiscard]] virtual quint64 notebooksSent() const = 0;

    /**
     * The amount of new or locally updated saved searches which were sent
     * to Evernote during sync.
     */
    [[nodiscard]] virtual quint64 savedSearchesSent() const = 0;

    /**
     * The amount of new or locally updated tags which were sent to Evernote
     * during sync.
     */
    [[nodiscard]] virtual quint64 tagsSent() const = 0;

    /**
     * The amount of new or locally updated notes which were sent to Evernote
     * during sync.
     */
    [[nodiscard]] virtual quint64 notesSent() const = 0;
};

} // namespace quentier::synchronization
