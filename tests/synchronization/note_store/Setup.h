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

#pragma once

#include <QFlags>

namespace quentier::synchronization::tests {

class NoteStoreServer;

} // namespace quentier::synchronization::tests

namespace quentier::synchronization::tests::note_store {

enum class DataItemType
{
    SavedSearch = 1 << 0,
    Tag = 1 << 1,
    Notebook = 1 << 2,
    Note = 1 << 3,
    Resource = 1 << 4,
};

Q_DECLARE_FLAGS(DataItemTypes, DataItemType);
Q_DECLARE_OPERATORS_FOR_FLAGS(DataItemTypes);

enum class GeneratorOption
{
    IncludeBaseItems = 1 << 0,
    IncludeNewItems = 1 << 1,
    IncludeModifiedItems = 1 << 2,
};

Q_DECLARE_FLAGS(GeneratorOptions, GeneratorOption);
Q_DECLARE_OPERATORS_FOR_FLAGS(GeneratorOptions);

enum class ItemSource
{
    UserOwnAccount = 1 << 0,
    LinkedNotebook = 1 << 1,
};

Q_DECLARE_FLAGS(ItemSources, ItemSource);
Q_DECLARE_OPERATORS_FOR_FLAGS(ItemSources);

void setupNoteStoreServer(
    DataItemTypes dataItemTypes,
    GeneratorOptions generatorOptions,
    ItemSources itemSources,
    NoteStoreServer & noteStoreServer);

} // namespace quentier::synchronization::tests::note_store
