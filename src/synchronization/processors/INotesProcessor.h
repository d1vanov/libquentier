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

#include <qevercloud/types/Fwd.h>
#include <qevercloud/types/Note.h>

#include <QException>
#include <QFuture>
#include <QList>

#include <memory>
#include <utility>

namespace quentier::synchronization {

class INotesProcessor
{
public:
    virtual ~INotesProcessor() = default;

    struct ProcessNotesStatus
    {
        quint64 m_totalNewNotes = 0;
        quint64 m_totalUpdatedNotes = 0;
        quint64 m_totalExpungedNotes = 0;

        using Errors =
            QList<std::pair<qevercloud::Note, std::shared_ptr<QException>>>;

        Errors m_notesWhichFailedToDownload;
        Errors m_notesWhichFailedToProcess;
        Errors m_notesWhichFailedToExpunge;
    };

    [[nodiscard]] virtual QFuture<ProcessNotesStatus> processNotes(
        const QList<qevercloud::SyncChunk> & syncChunks) = 0;
};

} // namespace quentier::synchronization
