/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TYPES_DATA_NOTE_STORE_DATA_ELEMENT_DATA_H
#define LIB_QUENTIER_TYPES_DATA_NOTE_STORE_DATA_ELEMENT_DATA_H

#include "LocalStorageDataElementData.h"

namespace quentier {

class Q_DECL_HIDDEN NoteStoreDataElementData :
    public LocalStorageDataElementData
{
public:
    NoteStoreDataElementData() = default;

    NoteStoreDataElementData(const NoteStoreDataElementData & other) = default;
    NoteStoreDataElementData(NoteStoreDataElementData && other) = default;

    NoteStoreDataElementData & operator=(
        const NoteStoreDataElementData & other) = delete;

    NoteStoreDataElementData & operator=(NoteStoreDataElementData && other) =
        delete;

    virtual ~NoteStoreDataElementData() override = default;

public:
    bool m_isDirty = true;
    bool m_isLocal = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_NOTE_STORE_DATA_ELEMENT_DATA_H
