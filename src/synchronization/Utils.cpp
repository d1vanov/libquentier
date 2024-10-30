/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include "Utils.h"

#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/types/Account.h>

#include <synchronization/sync_chunks/Utils.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/types/SyncChunk.h>

namespace quentier::synchronization {

SyncStatePtr readLastSyncState(
    const ISyncStateStoragePtr & syncStateStorage, const Account & account)
{
    Q_ASSERT(syncStateStorage);

    const auto syncState = syncStateStorage->getSyncState(account);
    Q_ASSERT(syncState);

    return std::make_shared<SyncState>(
        syncState->userDataUpdateCount(), syncState->userDataLastSyncTime(),
        syncState->linkedNotebookUpdateCounts(),
        syncState->linkedNotebookLastSyncTimes());
}

bool isAuthenticationTokenAboutToExpire(
    const qevercloud::Timestamp authenticationTokenExpirationTimestamp)
{
    const qevercloud::Timestamp currentTimestamp =
        QDateTime::currentMSecsSinceEpoch();

    constexpr qint64 halfAnHourMsec = 1800000;

    return (authenticationTokenExpirationTimestamp - currentTimestamp) <
        halfAnHourMsec;
}

QString linkedNotebookInfo(const qevercloud::LinkedNotebook & linkedNotebook)
{
    QString res;
    QTextStream strm{&res};

    if (linkedNotebook.username()) {
        strm << *linkedNotebook.username();
    }
    else {
        strm << "<no username>";
    }

    strm << " (";
    if (linkedNotebook.guid()) {
        strm << *linkedNotebook.guid();
    }
    else {
        strm << "<no guid>";
    }

    strm << ", ";
    if (linkedNotebook.sharedNotebookGlobalId()) {
        strm << *linkedNotebook.sharedNotebookGlobalId();
    }
    else {
        strm << "<no shared notebook global id>";
    }

    strm << ")";

    return res;
}

QString linkedNotebooksInfo(
    const QList<qevercloud::LinkedNotebook> & linkedNotebooks)
{
    if (linkedNotebooks.isEmpty()) {
        return QStringLiteral("<empty>");
    }

    QString res;
    QTextStream strm{&res};

    strm << "(" << linkedNotebooks.size() << "):\n";
    for (const auto & linkedNotebook: std::as_const(linkedNotebooks)) {
        strm << "   [" << linkedNotebookInfo(linkedNotebook) << "];\n";
    }

    strm.flush();
    return res;
}

QString syncChunksUsnInfo(const QList<qevercloud::SyncChunk> & syncChunks)
{
    if (syncChunks.isEmpty()) {
        return QStringLiteral("<empty>");
    }

    QString res;
    QTextStream strm{&res};

    strm << "(" << syncChunks.size() << "):\n";

    const auto printOptNum = [](const std::optional<qint32> & num) {
        return num ? QString::number(*num) : QStringLiteral("<none>");
    };

    for (const auto & syncChunk: std::as_const(syncChunks)) {
        const auto lowUsn = utils::syncChunkLowUsn(syncChunk);
        const auto & highUsn = syncChunk.chunkHighUSN();
        strm << "    [" << printOptNum(*lowUsn) << " => "
             << printOptNum(*highUsn) << "];\n";
    }

    strm.flush();
    return res;
}

} // namespace quentier::synchronization
