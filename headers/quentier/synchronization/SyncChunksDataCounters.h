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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_SYNC_CHUNKS_DATA_COUNTERS_H
#define LIB_QUENTIER_SYNCHRONIZATION_SYNC_CHUNKS_DATA_COUNTERS_H

#include <quentier/utility/Printable.h>

#include <QtGlobal>

namespace quentier {

/**
 * @brief The SyncChunksDataCounters struct encapsulates integer counters
 * representing the current progress on processing the data from downloaded
 * sync chunks
 */
struct SyncChunksDataCounters: public Printable
{
    // ================= Saved searches =================

    /**
     * Total number of new or updated saved searches in downloaded sync chunks
     */
    quint64 totalSavedSearches = 0ul;

    /**
     * Total number of expunged saved searches in downloaded sync chunks
     */
    quint64 totalExpungedSavedSearches = 0ul;

    /**
     * Number of saved searches from sync chunks added to the local storage
     * so far
     */
    quint64 addedSavedSearches = 0ul;

    /**
     * Number of saved searches from sync chunks updated in the local storage
     * so far
     */
    quint64 updatedSavedSearches = 0ul;

    /**
     * Number of saved searches from sync chunks expunged from the local storage
     * so far
     */
    quint64 expungedSavedSearches = 0ul;

    // ================= Tags =================

    /**
     * Total number of new or updated tags in downloaded sync chunks
     */
    quint64 totalTags = 0ul;

    /**
     * Total number of expunged tags in downloaded sync chunks
     */
    quint64 totalExpungedTags = 0ul;

    /**
     * Number of tags from sync chunks added to the local storage so far
     */
    quint64 addedTags = 0ul;

    /**
     * Number of tags from sync chunks updated in the local storage so far
     */
    quint64 updatedTags = 0ul;

    /**
     * Number of tags from sync chunks expunged from the local storage so far
     */
    quint64 expungedTags = 0ul;

    // ================= Linked notebooks =================

    /**
     * Total number of new or updated linked notebooks in downloaded sync chunks
     */
    quint64 totalLinkedNotebooks = 0ul;

    /**
     * Total number of expunged saved searches in downloaded sync chunks
     */
    quint64 totalExpungedLinkedNotebooks = 0ul;

    /**
     * Number of linked notebooks from sync chunks added to the local storage
     * so far
     */
    quint64 addedLinkedNotebooks = 0ul;

    /**
     * Number of linked notebooks from sync chunks updated in the local storage
     * so far
     */
    quint64 updatedLinkedNotebooks = 0ul;

    /**
     * Number of linked notebooks from sync chunks expunged from the local
     * storage so far
     */
    quint64 expungedLinkedNotebooks = 0ul;

    // ================= Notebooks =================

    /**
     * Total number of new or updated notebooks in downloaded sync chunks
     */
    quint64 totalNotebooks = 0ul;

    /**
     * Total number of expunged notebooks in downloaded sync chunks
     */
    quint64 totalExpungedNotebooks = 0ul;

    /**
     * Number of notebooks from sync chunks added to the local storage so far
     */
    quint64 addedNotebooks = 0ul;

    /**
     * Number of notebooks from sync chunks updated in the local storage so far
     */
    quint64 updatedNotebooks = 0ul;

    /**
     * Number of notebooks from sync chunks expunged from the local storage
     * so far
     */
    quint64 expungedNotebooks = 0ul;

    QTextStream & print(QTextStream & strm) const override;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNC_CHUNKS_DATA_COUNTERS_H
