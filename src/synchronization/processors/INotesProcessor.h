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

#include <quentier/synchronization/ISynchronizer.h>

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/TypeAliases.h>

#include <QException>
#include <QFuture>
#include <QHash>
#include <QList>

#include <memory>
#include <utility>

namespace quentier::synchronization {

class INotesProcessor
{
public:
    virtual ~INotesProcessor() = default;

    using DownloadNotesStatus = ISynchronizer::DownloadNotesStatus;

    struct ICallback
    {
        virtual ~ICallback() = default;

        virtual void onProcessedNote(
            const qevercloud::Guid & noteGuid,
            qint32 noteUpdateSequenceNum) noexcept = 0;

        virtual void onExpungedNote(
            const qevercloud::Guid & noteGuid) noexcept = 0;

        virtual void onFailedToExpungeNote(
            const qevercloud::Guid & noteGuid,
            const QException & e) noexcept = 0;

        virtual void onNoteFailedToDownload(
            const qevercloud::Note & note,
            const QException & e) noexcept = 0;

        virtual void onNoteFailedToProcess(
            const qevercloud::Note & note,
            const QException & e) noexcept = 0;

        virtual void onNoteProcessingCancelled(
            const qevercloud::Note & note) noexcept = 0;
    };

    using ICallbackWeakPtr = std::weak_ptr<ICallback>;

    [[nodiscard]] virtual QFuture<DownloadNotesStatus> processNotes(
        const QList<qevercloud::SyncChunk> & syncChunks,
        ICallbackWeakPtr callbackWeak = {}) = 0;
};

} // namespace quentier::synchronization
