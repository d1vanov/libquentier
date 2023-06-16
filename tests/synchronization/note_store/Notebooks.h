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

#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/TypeAliases.h>

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace quentier::synchronization::tests::note_store {

struct NotebookByGuidTag
{};

struct NotebookByUSNTag
{};

struct NotebookByNameUpperTag
{};

struct NotebookByLinkedNotebookGuidTag
{};

struct NotebookDataExtractor
{
    [[nodiscard]] static QString name(const qevercloud::Notebook & notebook)
    {
        return notebook.name().value_or(QString{});
    }

    [[nodiscard]] static QString nameUpper(
        const qevercloud::Notebook & notebook)
    {
        return name(notebook).toUpper();
    }

    [[nodiscard]] static qevercloud::Guid guid(
        const qevercloud::Notebook & notebook)
    {
        return notebook.guid().value_or(qevercloud::Guid{});
    }

    [[nodiscard]] static qint32 updateSequenceNumber(
        const qevercloud::Notebook & notebook)
    {
        return notebook.updateSequenceNum().value_or(0);
    }

    [[nodiscard]] static qevercloud::Guid linkedNotebookGuid(
        const qevercloud::Notebook & notebook)
    {
        return notebook.linkedNotebookGuid().value_or(qevercloud::Guid{});
    }
};

using Notebooks = boost::multi_index_container<
    qevercloud::Notebook,
    boost::multi_index::indexed_by<
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<NotebookByGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Notebook &, QString,
                &NotebookDataExtractor::guid>>,
        boost::multi_index::ordered_non_unique<
            boost::multi_index::tag<NotebookByUSNTag>,
            boost::multi_index::global_fun<
                const qevercloud::Notebook &, qint32,
                &NotebookDataExtractor::updateSequenceNumber>>,
        boost::multi_index::hashed_unique<
            boost::multi_index::tag<NotebookByNameUpperTag>,
            boost::multi_index::global_fun<
                const qevercloud::Notebook &, QString,
                &NotebookDataExtractor::nameUpper>>,
        boost::multi_index::hashed_non_unique<
            boost::multi_index::tag<NotebookByLinkedNotebookGuidTag>,
            boost::multi_index::global_fun<
                const qevercloud::Notebook &, QString,
                &NotebookDataExtractor::linkedNotebookGuid>>>>;

using NotebooksByGuid = Notebooks::index<NotebookByGuidTag>::type;
using NotebooksByUSN = Notebooks::index<NotebookByUSNTag>::type;
using NotebooksByNameUpper = Notebooks::index<NotebookByNameUpperTag>::type;
using NotebooksByLinkedNotebookGuid =
    Notebooks::index<NotebookByLinkedNotebookGuidTag>::type;

} // namespace quentier::synchronization::tests::note_store
