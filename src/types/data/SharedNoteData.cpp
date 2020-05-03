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

#include "SharedNoteData.h"

namespace quentier {

SharedNoteData::SharedNoteData() :
    QSharedData(),
    m_noteGuid(),
    m_qecSharedNote(),
    m_indexInNote(-1)
{}

SharedNoteData::SharedNoteData(const SharedNoteData & other) :
    QSharedData(),
    m_noteGuid(other.m_noteGuid),
    m_qecSharedNote(other.m_qecSharedNote),
    m_indexInNote(other.m_indexInNote)
{}

SharedNoteData::SharedNoteData(SharedNoteData && other) :
    QSharedData(),
    m_noteGuid(std::move(other.m_noteGuid)),
    m_qecSharedNote(std::move(other.m_qecSharedNote)),
    m_indexInNote(std::move(other.m_indexInNote))
{}

SharedNoteData::SharedNoteData(const qevercloud::SharedNote & sharedNote) :
    QSharedData(),
    m_noteGuid(),
    m_qecSharedNote(sharedNote),
    m_indexInNote(-1)
{}

SharedNoteData::~SharedNoteData()
{}

} // namespace quentier
