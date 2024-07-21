/*
 * Copyright 2021-2023 Dmitry Ivanov
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

#include "SyncChunksDataCounters.h"

namespace quentier::synchronization {

QTextStream & SyncChunksDataCounters::print(QTextStream & strm) const
{
    strm << "SyncChunksDataCounters: {\n"
        << "  total saved searches = " << m_totalSavedSearches << "\n"
        << "  total expunged saved searches = " << m_totalExpungedSavedSearches
        << "\n  added saved searches = " << m_addedSavedSearches << "\n"
        << "  updated saved searches = " << m_updatedSavedSearches << "\n"
        << "  expunged saved searches = " << m_expungedSavedSearches << "\n"
        << "  total tags = " << m_totalTags << "\n"
        << "  total expunged tags = " << m_totalExpungedTags << "\n"
        << "  added tags = " << m_addedTags << "\n"
        << "  updated tags = " << m_updatedTags << "\n"
        << "  expunged tags = " << m_expungedTags << "\n"
        << "  total linked notebooks = " << m_totalLinkedNotebooks << "\n"
        << "  total expunged linked notebooks = "
        << m_totalExpungedLinkedNotebooks << "\n"
        << "  added linked notebooks = " << m_addedLinkedNotebooks << "\n"
        << "  updated linked notebooks = " << m_updatedLinkedNotebooks << "\n"
        << "  expunged linked notebooks = " << m_expungedLinkedNotebooks << "\n"
        << "  total notebooks = " << m_totalNotebooks << "\n"
        << "  total expunged notebooks = " << m_totalExpungedNotebooks << "\n"
        << "  added notebooks = " << m_addedNotebooks << "\n"
        << "  updated notebooks = " << m_updatedNotebooks << "\n"
        << "  expunged notebooks = " << m_expungedNotebooks << "\n"
        << "}\n";

    return strm;
}

bool operator==(
    const SyncChunksDataCounters & lhs,
    const SyncChunksDataCounters & rhs) noexcept
{
    return lhs.m_totalSavedSearches == rhs.m_totalSavedSearches &&
        lhs.m_totalExpungedSavedSearches == rhs.m_totalExpungedSavedSearches &&
        lhs.m_addedSavedSearches == rhs.m_addedSavedSearches &&
        lhs.m_updatedSavedSearches == rhs.m_updatedSavedSearches &&
        lhs.m_expungedSavedSearches == rhs.m_expungedSavedSearches &&
        lhs.m_totalTags == rhs.m_totalTags &&
        lhs.m_totalExpungedTags == rhs.m_totalExpungedTags &&
        lhs.m_addedTags == rhs.m_addedTags &&
        lhs.m_updatedTags == rhs.m_updatedTags &&
        lhs.m_expungedTags == rhs.m_expungedTags &&
        lhs.m_totalLinkedNotebooks == rhs.m_totalLinkedNotebooks &&
        lhs.m_totalExpungedLinkedNotebooks ==
        rhs.m_totalExpungedLinkedNotebooks &&
        lhs.m_addedLinkedNotebooks == rhs.m_addedLinkedNotebooks &&
        lhs.m_updatedLinkedNotebooks == rhs.m_updatedLinkedNotebooks &&
        lhs.m_expungedLinkedNotebooks == rhs.m_expungedLinkedNotebooks &&
        lhs.m_totalNotebooks == rhs.m_totalNotebooks &&
        lhs.m_totalExpungedNotebooks == rhs.m_totalExpungedNotebooks &&
        lhs.m_addedNotebooks == rhs.m_addedNotebooks &&
        lhs.m_updatedNotebooks == rhs.m_updatedNotebooks &&
        lhs.m_expungedNotebooks == rhs.m_expungedNotebooks;
}

bool operator!=(
    const SyncChunksDataCounters & lhs,
    const SyncChunksDataCounters & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
