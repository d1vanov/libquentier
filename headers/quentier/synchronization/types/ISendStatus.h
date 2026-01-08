/*
 * Copyright 2022-2025 Dmitry Ivanov
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

#include <quentier/synchronization/types/Errors.h>
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

/**
 * @brief The ISendStatus interface represents the information about the
 * attempt to send information either from user's own account or from some
 * linked notebook to Evernote.
 */
class QUENTIER_EXPORT ISendStatus : public utility::Printable
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

    /**
     * @return total number of notes attempted to be sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalAttemptedToSendNotes() const = 0;

    /**
     * @return total number of notebooks attempted to be sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalAttemptedToSendNotebooks() const = 0;

    /**
     * @return total number of saved searches attempted to be sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalAttemptedToSendSavedSearches() const = 0;

    /**
     * @return total number of tags attempted to be sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalAttemptedToSendTags() const = 0;

    // Notes

    /**
     * @return number of notes which were successfully sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalSuccessfullySentNotes() const = 0;

    /**
     * @return list with notes and exceptions representing failures to send
     *         these notes to Evernote
     */
    [[nodiscard]] virtual QList<NoteWithException> failedToSendNotes()
        const = 0;

    // Notebooks

    /**
     * @return number of notebooks which were successfully sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalSuccessfullySentNotebooks() const = 0;

    /**
     * @return list with notebooks and exceptions representing failures to send
     *         these notebooks to Evernote
     */
    [[nodiscard]] virtual QList<NotebookWithException> failedToSendNotebooks()
        const = 0;

    // Saved searches

    /**
     * @return number of saved searches which were successfully sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalSuccessfullySentSavedSearches()
        const = 0;

    /**
     * @return list with saved searches and exceptions representing failures to
     *         send these saved searches to Evernote
     */
    [[nodiscard]] virtual QList<SavedSearchWithException>
        failedToSendSavedSearches() const = 0;

    // Tags

    /**
     * @return number of tags which were successfully sent to Evernote
     */
    [[nodiscard]] virtual quint64 totalSuccessfullySentTags() const = 0;

    /**
     * @return list with tags and exceptions representing failures to send these
     *         tags to Evernote
     */
    [[nodiscard]] virtual QList<TagWithException> failedToSendTags() const = 0;

    // General

    /**
     * @return error which might have occurred during sending the data to
     *         Evernote which has prevented further attempts to send anything
     *         to Evernote or std::monostate if no such error has occurred
     */
    [[nodiscard]] virtual StopSynchronizationError stopSynchronizationError()
        const = 0;

    /**
     * If during the send step of synchronization it was found out that
     * Evernote service's state of account has been updated since the last
     * download step, returns true meaning that incremental download step
     * should be repeated. Otherwise returns false.
     */
    [[nodiscard]] virtual bool needToRepeatIncrementalSync() const = 0;
};

} // namespace quentier::synchronization
