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

#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/TypeAliases.h>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace quentier::synchronization::tests::note_store {

struct LinkedNotebookByGuidTag
{};

struct LinkedNotebookByUSNTag
{};

struct LinkedNotebookByShardIdTag
{};

struct LinkedNotebookByUriTag
{};

struct LinkedNotebookByUsernameTag
{};

struct LinkedNotebookBySharedNotebookGlobalIdTag
{};

struct LinkedNotebookDataExtractor
{
    [[nodiscard]] static qevercloud::Guid guid(
        const qevercloud::LinkedNotebook & linkedNotebook)
    {
        return linkedNotebook.guid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static QString shardId(
        const qevercloud::LinkedNotebook & linkedNotebook)
    {
        return linkedNotebook.shardId().value_or(QString{});
    }

    [[nodiscard]] static QString uri(
        const qevercloud::LinkedNotebook & linkedNotebook)
    {
        return linkedNotebook.uri().value_or(QString{});
    }

    [[nodiscard]] static QString username(
        const qevercloud::LinkedNotebook & linkedNotebook)
    {
        return linkedNotebook.username().value_or(QString{});
    }

    [[nodiscard]] static qint32 updateSequenceNumber(
        const qevercloud::LinkedNotebook & linkedNotebook)
    {
        return linkedNotebook.updateSequenceNum().value_or(0);
    }

    [[nodiscard]] static QString sharedNotebookGlobalId(
        const qevercloud::LinkedNotebook & linkedNotebook)
    {
        return linkedNotebook.sharedNotebookGlobalId().value_or(QString{});
    }
};

using LinkedNotebooks = boost::multi_index_container<
    qevercloud::LinkedNotebook,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<LinkedNotebookByGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::LinkedNotebook &, QString,
                &LinkedNotebookDataExtractor::guid>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<LinkedNotebookByShardIdTag>,
            boost::multi_index::global_fun<
                const qevercloud::LinkedNotebook &, QString,
                &LinkedNotebookDataExtractor::shardId>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<LinkedNotebookByUriTag>,
            boost::multi_index::global_fun<
                const qevercloud::LinkedNotebook &, QString,
                &LinkedNotebookDataExtractor::uri>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<LinkedNotebookByUsernameTag>,
            boost::multi_index::global_fun<
                const qevercloud::LinkedNotebook &, QString,
                &LinkedNotebookDataExtractor::username>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<LinkedNotebookByUSNTag>,
            boost::multi_index::global_fun<
                const qevercloud::LinkedNotebook &, qint32,
                &LinkedNotebookDataExtractor::updateSequenceNumber>>,
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<LinkedNotebookBySharedNotebookGlobalIdTag>,
            boost::multi_index::global_fun<
                const qevercloud::LinkedNotebook &, QString,
                &LinkedNotebookDataExtractor::sharedNotebookGlobalId>>>>;

using LinkedNotebooksByGuid =
    LinkedNotebooks::index<LinkedNotebookByGuidTag>::type;

using LinkedNotebooksByUSN =
    LinkedNotebooks::index<LinkedNotebookByUSNTag>::type;

using LinkedNotebooksByShardId =
    LinkedNotebooks::index<LinkedNotebookByShardIdTag>::type;

using LinkedNotebooksByUri =
    LinkedNotebooks::index<LinkedNotebookByUriTag>::type;

using LinkedNotebooksByUsername =
    LinkedNotebooks::index<LinkedNotebookByUsernameTag>::type;

using LinkedNotebooksBySharedNotebookGlobalId =
    LinkedNotebooks::index<LinkedNotebookBySharedNotebookGlobalIdTag>::type;

} // namespace quentier::synchronization::tests::note_store
