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

#ifndef LIB_QUENTIER_TYPES_DATA_FAVORITABLE_DATA_ELEMENT_DATA_H
#define LIB_QUENTIER_TYPES_DATA_FAVORITABLE_DATA_ELEMENT_DATA_H

#include "NoteStoreDataElementData.h"

namespace quentier {

class Q_DECL_HIDDEN FavoritableDataElementData : public NoteStoreDataElementData
{
public:
    FavoritableDataElementData() = default;

    FavoritableDataElementData(const FavoritableDataElementData & other) =
        default;

    FavoritableDataElementData(FavoritableDataElementData && other) = default;

    FavoritableDataElementData & operator=(
        const FavoritableDataElementData & other) = delete;

    FavoritableDataElementData & operator=(
        FavoritableDataElementData && other) = delete;

    virtual ~FavoritableDataElementData() override = default;

public:
    bool m_isFavorited = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_FAVORITABLE_DATA_ELEMENT_DATA_H
