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

#include "../Fwd.h"

#include <quentier/synchronization/types/DownloadNotesStatus.h>

#include <qevercloud/types/Fwd.h>

#include <QFuture>
#include <QList>

namespace quentier::synchronization {

/**
 * @brief The IDurableNotesProcessor interface represents a notes processor
 * which retries downloading and processing of notes which for some reason
 * failed during the previous sync attempt.
 */
class IDurableNotesProcessor
{
public:
    virtual ~IDurableNotesProcessor() = default;

    [[nodiscard]] virtual QFuture<DownloadNotesStatus> processNotes(
        const QList<qevercloud::SyncChunk> & syncChunks) = 0;
};

} // namespace quentier::synchronization
