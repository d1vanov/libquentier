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

#include <qevercloud/types/Note.h>
#include <qevercloud/types/TypeAliases.h>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace quentier::synchronization::tests::note_store {

struct NoteByGuidTag
{};

struct NoteByUSNTag
{};

struct NoteByNotebookGuidTag
{};

struct NoteByConflictSourceNoteGuidTag
{};

struct NoteDataExtractor
{
    [[nodiscard]] static qevercloud::Guid guid(const qevercloud::Note & note)
    {
        return note.guid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static qint32 updateSequenceNumber(
        const qevercloud::Note & note)
    {
        return note.updateSequenceNum().value_or(0);
    }

    [[nodiscard]] static qevercloud::Guid notebookGuid(
        const qevercloud::Note & note)
    {
        return note.notebookGuid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static qevercloud::Guid conflictSourceNoteGuid(
        const qevercloud::Note & note)
    {
        if (!note.attributes()) {
            return {};
        }

        const auto & noteAttributes = *note.attributes();
        if (!noteAttributes.conflictSourceNoteGuid()) {
            return {};
        }

        return *noteAttributes.conflictSourceNoteGuid();
    }
};

using Notes = boost::multi_index_container<
    qevercloud::Note,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<NoteByGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Note &, QString, &NoteDataExtractor::guid>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<NoteByUSNTag>,
            boost::multi_index::global_fun<
                const qevercloud::Note &, qint32,
                &NoteDataExtractor::updateSequenceNumber>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<NoteByNotebookGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Note &, QString,
                &NoteDataExtractor::notebookGuid>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<NoteByConflictSourceNoteGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Note &, QString,
                &NoteDataExtractor::conflictSourceNoteGuid>>>>;

using NotesByGuid = Notes::index<NoteByGuidTag>::type;
using NotesByUSN = Notes::index<NoteByUSNTag>::type;
using NotesByNotebookGuid = Notes::index<NoteByNotebookGuidTag>::type;
using NotesByConflictSourceNoteGuid =
    Notes::index<NoteByConflictSourceNoteGuidTag>::type;

} // namespace quentier::synchronization::tests::note_store
