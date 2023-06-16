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

#include <qevercloud/types/Tag.h>
#include <qevercloud/types/TypeAliases.h>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace quentier::synchronization::tests::note_store {

struct TagByGuidTag
{};

struct TagByUSNTag
{};

struct TagByNameUpperTag
{};

struct TagByParentTagGuidTag
{};

struct TagByLinkedNotebookGuidTag
{};

struct TagDataExtractor
{
    [[nodiscard]] static QString name(const qevercloud::Tag & tag)
    {
        return tag.name().value_or(QString{});
    }

    [[nodiscard]] static QString nameUpper(const qevercloud::Tag & tag)
    {
        return name(tag).toUpper();
    }

    [[nodiscard]] static qevercloud::Guid guid(const qevercloud::Tag & tag)
    {
        return tag.guid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static qint32 updateSequenceNumber(
        const qevercloud::Tag & tag)
    {
        return tag.updateSequenceNum().value_or(0);
    }

    [[nodiscard]] static qevercloud::Guid parentTagGuid(
        const qevercloud::Tag & tag)
    {
        return tag.parentGuid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static qevercloud::Guid linkedNotebookGuid(
        const qevercloud::Tag & tag)
    {
        return tag.linkedNotebookGuid().value_or(qevercloud::Guid{});
    }
};

using Tags = boost::multi_index_container<
    qevercloud::Tag,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<TagByGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Tag &, QString, &TagDataExtractor::guid>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<TagByUSNTag>,
            boost::multi_index::global_fun<
                const qevercloud::Tag &, qint32,
                &TagDataExtractor::updateSequenceNumber>>,
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<TagByNameUpperTag>,
            boost::multi_index::global_fun<
                const qevercloud::Tag &, QString,
                &TagDataExtractor::nameUpper>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<TagByParentTagGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Tag &, QString,
                &TagDataExtractor::parentTagGuid>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<TagByLinkedNotebookGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Tag &, QString,
                &TagDataExtractor::linkedNotebookGuid>>>>;

using TagsByGuid = Tags::index<TagByGuidTag>::type;
using TagsByUSN = Tags::index<TagByUSNTag>::type;
using TagsByNameUpper = Tags::index<TagByNameUpperTag>::type;
using TagsByParentTagGuid = Tags::index<TagByParentTagGuidTag>::type;
using TagsByLinkedNotebookGuid = Tags::index<TagByLinkedNotebookGuidTag>::type;

} // namespace quentier::synchronization::tests::note_store
