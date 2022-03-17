/*
 * Copyright 2021-2022 Dmitry Ivanov
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

#include <quentier/synchronization/ISyncChunksDataCounters.h>

namespace quentier {

/**
 * @brief The ISyncChunksDataCounters interface provides integer counters
 * representing the current progress on processing the data from downloaded
 * sync chunks
 */
struct SyncChunksDataCounters final : public ISyncChunksDataCounters
{
    // ISyncChunksDataCounters
    [[nodiscard]] quint64 totalSavedSearches() const noexcept override
    {
        return m_totalSavedSearches;
    }

    [[nodiscard]] quint64 totalExpungedSavedSearches() const noexcept override
    {
        return m_totalExpungedSavedSearches;
    }

    [[nodiscard]] quint64 addedSavedSearches() const noexcept override
    {
        return m_addedSavedSearches;
    }

    [[nodiscard]] quint64 updatedSavedSearches() const noexcept override
    {
        return m_updatedSavedSearches;
    }

    [[nodiscard]] quint64 expungedSavedSearches() const noexcept override
    {
        return m_expungedSavedSearches;
    }

    [[nodiscard]] quint64 totalTags() const noexcept override
    {
        return m_totalTags;
    }

    [[nodiscard]] quint64 totalExpungedTags() const noexcept override
    {
        return m_totalExpungedTags;
    }

    [[nodiscard]] quint64 addedTags() const noexcept override
    {
        return m_addedTags;
    }

    [[nodiscard]] quint64 updatedTags() const noexcept override
    {
        return m_updatedTags;
    }

    [[nodiscard]] quint64 expungedTags() const noexcept override
    {
        return m_expungedTags;
    }

    [[nodiscard]] quint64 totalLinkedNotebooks() const noexcept override
    {
        return m_totalLinkedNotebooks;
    }

    [[nodiscard]] quint64 totalExpungedLinkedNotebooks() const noexcept override
    {
        return m_totalExpungedLinkedNotebooks;
    }

    [[nodiscard]] quint64 addedLinkedNotebooks() const noexcept override
    {
        return m_addedLinkedNotebooks;
    }

    [[nodiscard]] quint64 updatedLinkedNotebooks() const noexcept override
    {
        return m_updatedLinkedNotebooks;
    }

    [[nodiscard]] quint64 expungedLinkedNotebooks() const noexcept override
    {
        return m_expungedLinkedNotebooks;
    }

    [[nodiscard]] quint64 totalNotebooks() const noexcept override
    {
        return m_totalNotebooks;
    }

    [[nodiscard]] quint64 totalExpungedNotebooks() const noexcept override
    {
        return m_totalExpungedNotebooks;
    }

    [[nodiscard]] quint64 addedNotebooks() const noexcept override
    {
        return m_addedNotebooks;
    }

    [[nodiscard]] quint64 updatedNotebooks() const noexcept override
    {
        return m_updatedNotebooks;
    }

    [[nodiscard]] quint64 expungedNotebooks() const noexcept override
    {
        return m_expungedNotebooks;
    }

    quint64 m_totalSavedSearches = 0UL;
    quint64 m_totalExpungedSavedSearches = 0UL;
    quint64 m_addedSavedSearches = 0UL;
    quint64 m_updatedSavedSearches = 0UL;
    quint64 m_expungedSavedSearches = 0UL;

    quint64 m_totalTags = 0UL;
    quint64 m_totalExpungedTags = 0UL;
    quint64 m_addedTags = 0UL;
    quint64 m_updatedTags = 0UL;
    quint64 m_expungedTags = 0UL;

    quint64 m_totalLinkedNotebooks = 0UL;
    quint64 m_totalExpungedLinkedNotebooks = 0UL;
    quint64 m_addedLinkedNotebooks = 0UL;
    quint64 m_updatedLinkedNotebooks = 0UL;
    quint64 m_expungedLinkedNotebooks = 0UL;

    quint64 m_totalNotebooks = 0UL;
    quint64 m_totalExpungedNotebooks = 0UL;
    quint64 m_addedNotebooks = 0UL;
    quint64 m_updatedNotebooks = 0UL;
    quint64 m_expungedNotebooks = 0UL;

    QTextStream & print(QTextStream & strm) const override;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_SYNC_CHUNKS_DATA_COUNTERS_H
