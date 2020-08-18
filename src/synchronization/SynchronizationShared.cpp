/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#include "SynchronizationShared.h"

namespace quentier {

LinkedNotebookAuthData::LinkedNotebookAuthData() = default;

LinkedNotebookAuthData::LinkedNotebookAuthData(
    QString guid, QString shardId, QString sharedNotebookGlobalId, QString uri,
    QString noteStoreUrl) :
    m_guid(std::move(guid)),
    m_shardId(std::move(shardId)),
    m_sharedNotebookGlobalId(std::move(sharedNotebookGlobalId)),
    m_uri(std::move(uri)), m_noteStoreUrl(std::move(noteStoreUrl))
{}

QTextStream & LinkedNotebookAuthData::print(QTextStream & strm) const
{
    strm << "LinkedNotebookAuthData: {\n"
         << "    guid = " << m_guid << "\n"
         << "    shard id = " << m_shardId << "\n"
         << "    shared notebook global id = " << m_sharedNotebookGlobalId
         << "\n"
         << "    uri = " << m_uri << "\n"
         << "    note store url = " << m_noteStoreUrl << "\n"
         << "};\n";

    return strm;
}

} // namespace quentier
