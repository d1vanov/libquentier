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

#ifndef LIB_QUENTIER_TYPES_DATA_RESOURCE_DATA_H
#define LIB_QUENTIER_TYPES_DATA_RESOURCE_DATA_H

#include "NoteStoreDataElementData.h"

#include <qt5qevercloud/QEverCloud.h>

namespace quentier {

class Q_DECL_HIDDEN ResourceData final : public NoteStoreDataElementData
{
public:
    ResourceData() = default;
    ResourceData(const ResourceData & other) = default;
    ResourceData(ResourceData && other) = default;

    ResourceData(const qevercloud::Resource & other);
    ResourceData(qevercloud::Resource && other);

    ResourceData & operator=(const ResourceData & other) = delete;
    ResourceData & operator=(ResourceData && other) = delete;

    virtual ~ResourceData() override;

public:
    qevercloud::Resource m_qecResource;
    int m_indexInNote = -1;
    qevercloud::Optional<QString> m_noteLocalUid;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_RESOURCE_DATA_H
