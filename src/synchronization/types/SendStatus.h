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

#include <quentier/synchronization/types/ISendStatus.h>

namespace quentier::synchronization {

struct SendStatus final : public ISendStatus
{
    // Total
    [[nodiscard]] quint64 totalAttemptedToSendNotes() const noexcept override;
    [[nodiscard]] quint64 totalAttemptedToSendNotebooks()
        const noexcept override;
    [[nodiscard]] quint64 totalAttemptedToSendSavedSearches()
        const noexcept override;
    [[nodiscard]] quint64 totalAttemptedToSendTags() const noexcept override;

    // Notes
    [[nodiscard]] quint64 totalSuccessfullySentNotes() const noexcept override;
    [[nodiscard]] QList<NoteWithException> failedToSendNotes() const override;

    // Notebooks
    [[nodiscard]] quint64 totalSuccessfullySentNotebooks()
        const noexcept override;

    [[nodiscard]] QList<NotebookWithException> failedToSendNotebooks()
        const override;

    // Saved searches
    [[nodiscard]] quint64 totalSuccessfullySentSavedSearches()
        const noexcept override;

    [[nodiscard]] QList<SavedSearchWithException> failedToSendSavedSearches()
        const override;

    // Tags
    [[nodiscard]] quint64 totalSuccessfullySentTags() const noexcept override;
    [[nodiscard]] QList<TagWithException> failedToSendTags() const override;

    // General
    [[nodiscard]] StopSynchronizationError stopSynchronizationError()
        const override;
    [[nodiscard]] bool needToRepeatIncrementalSync() const noexcept override;

    QTextStream & print(QTextStream & strm) const override;

    quint64 m_totalAttemptedToSendNotes = 0UL;
    quint64 m_totalAttemptedToSendNotebooks = 0UL;
    quint64 m_totalAttemptedToSendSavedSearches = 0UL;
    quint64 m_totalAttemptedToSendTags = 0UL;

    quint64 m_totalSuccessfullySentNotes = 0UL;
    QList<NoteWithException> m_failedToSendNotes;

    quint64 m_totalSuccessfullySentNotebooks = 0UL;
    QList<NotebookWithException> m_failedToSendNotebooks;

    quint64 m_totalSuccessfullySentSavedSearches = 0UL;
    QList<SavedSearchWithException> m_failedToSendSavedSearches;

    quint64 m_totalSuccessfullySentTags = 0UL;
    QList<TagWithException> m_failedToSendTags;

    StopSynchronizationError m_stopSynchronizationError{std::monostate{}};

    bool m_needToRepeatIncrementalSync = false;
};

} // namespace quentier::synchronization
