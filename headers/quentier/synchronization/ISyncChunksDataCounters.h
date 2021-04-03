/*
 * Copyright 2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_I_SYNC_CHUNKS_DATA_COUNTERS_H
#define LIB_QUENTIER_SYNCHRONIZATION_I_SYNC_CHUNKS_DATA_COUNTERS_H

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <QtGlobal>

namespace quentier {

/**
 * @brief The ISyncChunksDataCounters interface provides integer counters
 * representing the current progress on processing the data from downloaded
 * sync chunks
 */
struct QUENTIER_EXPORT ISyncChunksDataCounters: public Printable
{
    // ================= Saved searches =================

    /**
     * Total number of new or updated saved searches in downloaded sync chunks
     */
    virtual quint64 totalSavedSearches() const noexcept = 0;

    /**
     * Total number of expunged saved searches in downloaded sync chunks
     */
    virtual quint64 totalExpungedSavedSearches() const noexcept = 0;

    /**
     * Number of saved searches from sync chunks added to the local storage
     * so far
     */
    virtual quint64 addedSavedSearches() const noexcept = 0;

    /**
     * Number of saved searches from sync chunks updated in the local storage
     * so far
     */
    virtual quint64 updatedSavedSearches() const noexcept = 0;

    /**
     * Number of saved searches from sync chunks expunged from the local storage
     * so far
     */
    virtual quint64 expungedSavedSearches() const noexcept = 0;

    // ================= Tags =================

    /**
     * Total number of new or updated tags in downloaded sync chunks
     */
    virtual quint64 totalTags() const noexcept = 0;

    /**
     * Total number of expunged tags in downloaded sync chunks
     */
    virtual quint64 totalExpungedTags() const noexcept = 0;

    /**
     * Number of tags from sync chunks added to the local storage so far
     */
    virtual quint64 addedTags() const noexcept = 0;

    /**
     * Number of tags from sync chunks updated in the local storage so far
     */
    virtual quint64 updatedTags() const noexcept = 0;

    /**
     * Number of tags from sync chunks expunged from the local storage so far
     */
    virtual quint64 expungedTags() const noexcept = 0;

    // ================= Linked notebooks =================

    /**
     * Total number of new or updated linked notebooks in downloaded sync chunks
     */
    virtual quint64 totalLinkedNotebooks() const noexcept = 0;

    /**
     * Total number of expunged saved searches in downloaded sync chunks
     */
    virtual quint64 totalExpungedLinkedNotebooks() const noexcept = 0;

    /**
     * Number of linked notebooks from sync chunks added to the local storage
     * so far
     */
    virtual quint64 addedLinkedNotebooks() const noexcept = 0;

    /**
     * Number of linked notebooks from sync chunks updated in the local storage
     * so far
     */
    virtual quint64 updatedLinkedNotebooks() const noexcept = 0;

    /**
     * Number of linked notebooks from sync chunks expunged from the local
     * storage so far
     */
    virtual quint64 expungedLinkedNotebooks() const noexcept = 0;

    // ================= Notebooks =================

    /**
     * Total number of new or updated notebooks in downloaded sync chunks
     */
    virtual quint64 totalNotebooks() const noexcept = 0;

    /**
     * Total number of expunged notebooks in downloaded sync chunks
     */
    virtual quint64 totalExpungedNotebooks() const noexcept = 0;

    /**
     * Number of notebooks from sync chunks added to the local storage so far
     */
    virtual quint64 addedNotebooks() const noexcept = 0;

    /**
     * Number of notebooks from sync chunks updated in the local storage so far
     */
    virtual quint64 updatedNotebooks() const noexcept = 0;

    /**
     * Number of notebooks from sync chunks expunged from the local storage
     * so far
     */
    virtual quint64 expungedNotebooks() const noexcept = 0;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_I_SYNC_CHUNKS_DATA_COUNTERS_H
