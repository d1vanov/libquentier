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

#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/TypeAliases.h>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace quentier::synchronization::tests::note_store {

struct SavedSearchByGuidTag
{};

struct SavedSearchByUSNTag
{};

struct SavedSearchByNameUpperTag
{};

struct SavedSearchDataExtractor
{
    [[nodiscard]] static QString name(const qevercloud::SavedSearch & search)
    {
        return search.name().value_or(QString{});
    }

    [[nodiscard]] static QString nameUpper(
        const qevercloud::SavedSearch & search)
    {
        return name(search).toUpper();
    }

    [[nodiscard]] static qevercloud::Guid guid(
        const qevercloud::SavedSearch & search)
    {
        return search.guid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static qint32 updateSequenceNumber(
        const qevercloud::SavedSearch & search)
    {
        return search.updateSequenceNum().value_or(0);
    }
};

using SavedSearches = boost::multi_index_container<
    qevercloud::SavedSearch,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<SavedSearchByGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::SavedSearch &, QString,
                &SavedSearchDataExtractor::guid>>,
        boost::multi_index::ordered_unique<
            boost::multi_index::tag<SavedSearchByUSNTag>,
            boost::multi_index::global_fun<
                const qevercloud::SavedSearch &, qint32,
                &SavedSearchDataExtractor::updateSequenceNumber>>,
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<SavedSearchByNameUpperTag>,
            boost::multi_index::global_fun<
                const qevercloud::SavedSearch &, QString,
                &SavedSearchDataExtractor::nameUpper>>>>;

using SavedSearchesByGuid = SavedSearches::index<SavedSearchByGuidTag>::type;
using SavedSearchesByUSN = SavedSearches::index<SavedSearchByUSNTag>::type;
using SavedSearchesByNameUpper =
    SavedSearches::index<SavedSearchByNameUpperTag>::type;

} // namespace quentier::synchronization::tests::note_store
