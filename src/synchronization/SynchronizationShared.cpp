/*
 * Copyright 2017-2019 Dmitry Ivanov
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

LinkedNotebookAuthData::LinkedNotebookAuthData() :
    m_guid(),
    m_shardId(),
    m_sharedNotebookGlobalId(),
    m_uri(),
    m_noteStoreUrl()
{}

LinkedNotebookAuthData::LinkedNotebookAuthData(
        const QString & guid,
        const QString & shardId,
        const QString & sharedNotebookGlobalId,
        const QString & uri,
        const QString & noteStoreUrl) :
    m_guid(guid),
    m_shardId(shardId),
    m_sharedNotebookGlobalId(sharedNotebookGlobalId),
    m_uri(uri),
    m_noteStoreUrl(noteStoreUrl)
{}

QTextStream & LinkedNotebookAuthData::print(QTextStream & strm) const
{
    strm << "LinkedNotebookAuthData: {\n"
         << "    guid = " << m_guid << "\n"
         << "    shard id = " << m_shardId << "\n"
         << "    shared notebook global id = "
         << m_sharedNotebookGlobalId << "\n"
         << "    uri = " << m_uri << "\n"
         << "    note store url = " << m_noteStoreUrl
         << "\n" << "};\n";

    return strm;
}

} // namespace quentier
