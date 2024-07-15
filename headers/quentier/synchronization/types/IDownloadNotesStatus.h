/*
 * Copyright 2022-2024 Dmitry Ivanov
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
#include <quentier/synchronization/types/Fwd.h>
#include <quentier/utility/Linkage.h>
#include <quentier/utility/Printable.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/TypeAliases.h>

#include <QException>
#include <QJsonObject>
#include <QList>

#include <memory>
#include <utility>

namespace quentier::synchronization {

/**
 * @brief The IDownloadNotesStatus interface presents information about the
 * status of notes downloading process
 */
class QUENTIER_EXPORT IDownloadNotesStatus : public Printable
{
public:
    ~IDownloadNotesStatus() noexcept override;

    using QExceptionPtr = std::shared_ptr<QException>;
    using NoteWithException = std::pair<qevercloud::Note, QExceptionPtr>;
    using GuidWithException = std::pair<qevercloud::Guid, QExceptionPtr>;
    using UpdateSequenceNumbersByGuid = QHash<qevercloud::Guid, qint32>;

    [[nodiscard]] virtual quint64 totalNewNotes() const = 0;
    [[nodiscard]] virtual quint64 totalUpdatedNotes() const = 0;
    [[nodiscard]] virtual quint64 totalExpungedNotes() const = 0;

    [[nodiscard]] virtual QList<NoteWithException> notesWhichFailedToDownload()
        const = 0;

    [[nodiscard]] virtual QList<NoteWithException> notesWhichFailedToProcess()
        const = 0;

    [[nodiscard]] virtual QList<GuidWithException>
        noteGuidsWhichFailedToExpunge() const = 0;

    [[nodiscard]] virtual UpdateSequenceNumbersByGuid
        processedNoteGuidsAndUsns() const = 0;

    [[nodiscard]] virtual UpdateSequenceNumbersByGuid
        cancelledNoteGuidsAndUsns() const = 0;

    [[nodiscard]] virtual QList<qevercloud::Guid> expungedNoteGuids() const = 0;

    [[nodiscard]] virtual StopSynchronizationError stopSynchronizationError()
        const = 0;

    /**
     * Serialize IDownloadNotesStatus instance to json object.
     */
    [[nodiscard]] QJsonObject serializeToJson() const;

    /**
     * Factory method to create IDownloadNotesStatus instance from serialized
     * json object.
     * @return nonnull pointer to IDownloadNotesStatus in case of success or
     *         null pointer in case of deserialization failure.
     */
    [[nodiscard]] static IDownloadNotesStatusPtr deserializeFromJson(
        const QJsonObject & json);
};

} // namespace quentier::synchronization
