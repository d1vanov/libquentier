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

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/TypeAliases.h>

#include <QException>
#include <QList>

#include <memory>
#include <utility>

namespace quentier::synchronization {

class QUENTIER_EXPORT ISendStatus : public Printable
{
public:
    using QExceptionPtr = std::shared_ptr<QException>;

    using NoteWithException = std::pair<qevercloud::Note, QExceptionPtr>;

    using NotebookWithException =
        std::pair<qevercloud::Notebook, QExceptionPtr>;

    using SavedSearchWithException =
        std::pair<qevercloud::SavedSearch, QExceptionPtr>;

    using TagWithException = std::pair<qevercloud::Tag, QExceptionPtr>;

public:
    // Total
    [[nodiscard]] virtual quint64 totalAttemptedToSendNotes() const = 0;
    [[nodiscard]] virtual quint64 totalAttemptedToSendNotebooks() const = 0;
    [[nodiscard]] virtual quint64 totalAttemptedToSendSavedSearches() const = 0;
    [[nodiscard]] virtual quint64 totalAttemptedToSendTags() const = 0;

    // Notes
    [[nodiscard]] virtual quint64 totalSuccessfullySentNotes() const = 0;
    [[nodiscard]] virtual QList<NoteWithException> failedToSendNotes()
        const = 0;

    // Notebooks
    [[nodiscard]] virtual quint64 totalSuccessfullySentNotebooks() const = 0;
    [[nodiscard]] virtual QList<NotebookWithException> failedToSendNotebooks()
        const = 0;

    // Saved searches
    [[nodiscard]] virtual quint64 totalSuccessfullySentSavedSearches()
        const = 0;

    [[nodiscard]] virtual QList<SavedSearchWithException>
        failedToSendSavedSearches() const = 0;

    // Tags
    [[nodiscard]] virtual quint64 totalSuccessfullySentTags() const = 0;
    [[nodiscard]] virtual QList<TagWithException> failedToSendTags() const = 0;

    // General

    /**
     * If during the send step of synchronization it was found out that
     * Evernote service's state of account has been updated since the last
     * download step, returns true meaning that incremental download step
     * should be repeated. Otherwise returns false.
     */
    [[nodiscard]] virtual bool needToRepeatIncrementalSync() const = 0;
};

} // namespace quentier::synchronization
