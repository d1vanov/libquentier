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

#ifndef LIB_QUENTIER_TYPES_DATA_SHARED_NOTE_DATA_H
#define LIB_QUENTIER_TYPES_DATA_SHARED_NOTE_DATA_H

#include <qt5qevercloud/QEverCloud.h>

#include <QSharedData>

namespace quentier {

class Q_DECL_HIDDEN SharedNoteData final : public QSharedData
{
public:
    SharedNoteData() = default;
    SharedNoteData(const SharedNoteData & other) = default;
    SharedNoteData(SharedNoteData && other) = default;

    SharedNoteData(const qevercloud::SharedNote & other);
    SharedNoteData(qevercloud::SharedNote && other);

    SharedNoteData & operator=(const SharedNoteData & other) = delete;
    SharedNoteData & operator=(SharedNoteData && other) = delete;

    virtual ~SharedNoteData() = default;

public:
    QString m_noteGuid;
    qevercloud::SharedNote m_qecSharedNote;
    int m_indexInNote = -1;
};

} // namespace quentier

#endif // LIB_QUENTIER_TYPES_DATA_SHARED_NOTE_DATA_H
