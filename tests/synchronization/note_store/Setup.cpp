/*
 * Copyright 2023 Dmitry Ivanov
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

#include "../NoteStoreServer.h"
#include "Setup.h"

#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

namespace quentier::synchronization::tests::note_store {

namespace {

[[nodiscard]] qevercloud::SavedSearch generateSavedSearch(
    const int index, const quint32 updateSequenceNum)
{
    return qevercloud::SavedSearchBuilder{}
        .setGuid(UidGenerator::Generate())
        .setUpdateSequenceNum(updateSequenceNum)
        .setLocalOnly(false)
        .setLocallyModified(false)
        .setLocallyFavorited(false)
        .setName(QString::fromUtf8("Saved search #%1").arg(index))
        .setFormat(qevercloud::QueryFormat::SEXP)
        .setQuery(QStringLiteral("Saved search query %1").arg(index))
        .build();
}

[[nodiscard]] qevercloud::Tag generateTag(
    const int index, const qint32 updateSequenceNum,
    std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt)
{
    return qevercloud::TagBuilder{}
        .setGuid(UidGenerator::Generate())
        .setUpdateSequenceNum(updateSequenceNum)
        .setLinkedNotebookGuid(std::move(linkedNotebookGuid))
        .setLocalOnly(false)
        .setLocallyModified(false)
        .setLocallyFavorited(false)
        .setName(QString::fromUtf8("Tag #%1").arg(index))
        .build();
}

} // namespace

void setupNoteStoreServer(
    const DataItemTypes dataItemTypes, const GeneratorOptions generatorOptions,
    const ItemSources itemSources, NoteStoreServer & noteStoreServer)
{
    // TODO: implement
    Q_UNUSED(dataItemTypes)
    Q_UNUSED(generatorOptions)
    Q_UNUSED(itemSources)
    Q_UNUSED(noteStoreServer)
}

} // namespace quentier::synchronization::tests::note_store
