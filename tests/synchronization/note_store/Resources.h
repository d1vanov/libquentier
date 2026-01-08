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

#include <qevercloud/types/Resource.h>
#include <qevercloud/types/TypeAliases.h>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace quentier::synchronization::tests::note_store {

struct ResourceByGuidTag
{};

struct ResourceByUSNTag
{};

struct ResourceByNoteGuidTag
{};

struct ResourceDataExtractor
{
    [[nodiscard]] static qevercloud::Guid guid(
        const qevercloud::Resource & resource)
    {
        return resource.guid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static qint32 updateSequenceNumber(
        const qevercloud::Resource & resource)
    {
        return resource.updateSequenceNum().value_or(0);
    }

    [[nodiscard]] static qevercloud::Guid noteGuid(
        const qevercloud::Resource & resource)
    {
        return resource.noteGuid().value_or(qevercloud::Guid{});
    }
};

using Resources = boost::multi_index_container<
    qevercloud::Resource,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<ResourceByGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Resource &, QString,
                &ResourceDataExtractor::guid>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<ResourceByUSNTag>,
            boost::multi_index::global_fun<
                const qevercloud::Resource &, qint32,
                &ResourceDataExtractor::updateSequenceNumber>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<ResourceByNoteGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Resource &, QString,
                &ResourceDataExtractor::noteGuid>>>>;

using ResourcesByGuid = Resources::index<ResourceByGuidTag>::type;
using ResourcesByUSN = Resources::index<ResourceByUSNTag>::type;
using ResourcesByNoteGuid = Resources::index<ResourceByNoteGuidTag>::type;

} // namespace quentier::synchronization::tests::note_store
