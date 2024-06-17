/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include "ISyncChunksDownloader.h"
#include "Utils.h"

#include <QDebug>
#include <QTextStream>

#include <optional>
#include <utility>

namespace quentier::synchronization {

namespace {

template <class T>
void printSyncChunksResult(
    T & t, const ISyncChunksDownloader::SyncChunksResult & result)
{
    if (result.m_exception) {
        t << "Exception: " << result.m_exception->what();
        return;
    }

    std::optional<qint32> chunksLowUsn;
    std::optional<qint32> chunksHighUsn;
    for (const auto & syncChunk: std::as_const(result.m_syncChunks)) {
        const auto lowUsn = utils::syncChunkLowUsn(syncChunk);
        if (lowUsn && (!chunksLowUsn || *chunksLowUsn > *lowUsn)) {
            chunksLowUsn = lowUsn;
        }

        const auto highUsn = syncChunk.chunkHighUSN();
        if (highUsn && (!chunksHighUsn || *chunksHighUsn < *highUsn)) {
            chunksHighUsn = highUsn;
        }
    }

    t << result.m_syncChunks.size() << " sync chunks, low usn = "
      << (chunksLowUsn ? QString::number(*chunksLowUsn)
                       : QStringLiteral("<none>"))
      << ", high usn = "
      << (chunksHighUsn ? QString::number(*chunksHighUsn)
                        : QStringLiteral("<none>"));

    for (const auto & syncChunk: std::as_const(result.m_syncChunks)) {
        t << "SyncChunk: " << utils::briefSyncChunkInfo(syncChunk) << "\n\n";
    }
}

} // namespace

QDebug & operator<<(
    QDebug & dbg, const ISyncChunksDownloader::SyncChunksResult & result)
{
    printSyncChunksResult(dbg, result);
    return dbg;
}

QTextStream & operator<<(
    QTextStream & strm, const ISyncChunksDownloader::SyncChunksResult & result)
{
    printSyncChunksResult(strm, result);
    return strm;
}

} // namespace quentier::synchronization
