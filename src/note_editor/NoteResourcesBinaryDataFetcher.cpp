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

#include "NoteResourcesBinaryDataFetcher.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

NoteResourcesBinaryDataFetcher::NoteResourcesBinaryDataFetcher(
    LocalStorageManagerAsync & localStorageManagerAsync,
    QObject * parent) :
    QObject(parent)
{
    createConnections(localStorageManagerAsync);
}

void NoteResourcesBinaryDataFetcher::onFetchResourceBinaryData(
    Note note, QUuid requestId)
{
    QNDEBUG(
        "note_editor",
        "NoteResourcesBinaryDataFetcher::onFetchResourceBinaryData: note "
            << note.localUid() << ", request id " << requestId);

    if (!note.hasResources()) {
        QNDEBUG("note_editor", "Note has no resources");
        Q_EMIT finished(note, requestId);
        return;
    }

    QList<Resource> resourcesWithoutBinaryData;
    for (const auto & resource: note.resources()) {
        const bool dataBodyOk =
            (!resource.hasData() || resource.hasDataBody());

        const bool alternateDataBodyOk =
            (!resource.hasAlternateData() ||
             resource.hasAlternateDataBody());

        if (dataBodyOk && alternateDataBodyOk) {
            continue;
        }

        resourcesWithoutBinaryData << resource;
    }

    if (resourcesWithoutBinaryData.isEmpty()) {
        QNDEBUG("note_editor", "Note has no resources lacking binary data");
        Q_EMIT finished(note, requestId);
        return;
    }

    // TODO: continue from here
}

} // namespace quentier
