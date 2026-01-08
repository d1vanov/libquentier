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

#include "SyncResult.h"

#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncChunksDataCounters.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/utility/ToRange.h>

#include <utility>

namespace quentier::synchronization {

ISyncStatePtr SyncResult::syncState() const noexcept
{
    return m_syncState;
}

ISyncChunksDataCountersPtr SyncResult::userAccountSyncChunksDataCounters()
    const noexcept
{
    return m_userAccountSyncChunksDataCounters;
}

QHash<qevercloud::Guid, ISyncChunksDataCountersPtr>
    SyncResult::linkedNotebookSyncChunksDataCounters() const
{
    QHash<qevercloud::Guid, ISyncChunksDataCountersPtr> result;
    result.reserve(m_linkedNotebookSyncChunksDataCounters.size());
    for (const auto it: qevercloud::toRange(
             std::as_const(m_linkedNotebookSyncChunksDataCounters)))
    {
        result[it.key()] = it.value();
    }
    return result;
}

bool SyncResult::userAccountSyncChunksDownloaded() const noexcept
{
    return m_userAccountSyncChunksDownloaded;
}

QSet<qevercloud::Guid> SyncResult::linkedNotebookGuidsWithSyncChunksDownloaded()
    const
{
    return m_linkedNotebookGuidsWithSyncChunksDownloaded;
}

IDownloadNotesStatusPtr SyncResult::userAccountDownloadNotesStatus()
    const noexcept
{
    return m_userAccountDownloadNotesStatus;
}

QHash<qevercloud::Guid, IDownloadNotesStatusPtr>
    SyncResult::linkedNotebookDownloadNotesStatuses() const
{
    QHash<qevercloud::Guid, IDownloadNotesStatusPtr> result;
    result.reserve(m_linkedNotebookDownloadNotesStatuses.size());
    for (const auto it: qevercloud::toRange(
             std::as_const(m_linkedNotebookDownloadNotesStatuses)))
    {
        result[it.key()] = it.value();
    }

    return result;
}

IDownloadResourcesStatusPtr SyncResult::userAccountDownloadResourcesStatus()
    const noexcept
{
    return m_userAccountDownloadResourcesStatus;
}

QHash<qevercloud::Guid, IDownloadResourcesStatusPtr>
    SyncResult::linkedNotebookDownloadResourcesStatuses() const
{
    QHash<qevercloud::Guid, IDownloadResourcesStatusPtr> result;
    result.reserve(m_linkedNotebookDownloadResourcesStatuses.size());
    for (const auto it: qevercloud::toRange(
             std::as_const(m_linkedNotebookDownloadResourcesStatuses)))
    {
        result[it.key()] = it.value();
    }
    return result;
}

ISendStatusPtr SyncResult::userAccountSendStatus() const
{
    return m_userAccountSendStatus;
}

QHash<qevercloud::Guid, ISendStatusPtr> SyncResult::linkedNotebookSendStatuses()
    const
{
    QHash<qevercloud::Guid, ISendStatusPtr> result;
    result.reserve(m_linkedNotebookSendStatuses.size());
    for (const auto it:
         qevercloud::toRange(std::as_const(m_linkedNotebookSendStatuses)))
    {
        result[it.key()] = it.value();
    }
    return result;
}

StopSynchronizationError SyncResult::stopSynchronizationError() const
{
    return m_stopSynchronizationError;
}

QTextStream & SyncResult::print(QTextStream & strm) const
{
    strm << "SyncResult:\n";

    if (m_syncState) {
        strm << "  Sync state = ";
        m_syncState->print(strm);
        strm << "\n";
    }

    if (m_userAccountSyncChunksDataCounters) {
        strm << "  User account sync chunks data counters = ";
        m_userAccountSyncChunksDataCounters->print(strm);
        strm << "\n";
    }

    strm << "  Linked notebook sync chunks data counters ("
         << m_linkedNotebookSyncChunksDataCounters.size() << ") = ";
    if (m_linkedNotebookSyncChunksDataCounters.isEmpty()) {
        strm << "<empty>\n";
    }
    else {
        for (const auto it:
             qevercloud::toRange(m_linkedNotebookSyncChunksDataCounters))
        {
            if (Q_UNLIKELY(!it.value())) {
                continue;
            }

            strm << "{" << it.key() << ": ";
            it.value()->print(strm);
            strm << "};";
        }
        strm << "\n";
    }

    strm << "  User account sync chunks downloaded = "
         << (m_userAccountSyncChunksDownloaded ? "true" : "false") << "\n";

    strm << "  Linked notebook guids with sync chunks downloaded ("
         << m_linkedNotebookGuidsWithSyncChunksDownloaded.size() << ") = ";
    for (const auto & guid:
         std::as_const(m_linkedNotebookGuidsWithSyncChunksDownloaded))
    {
        strm << "{" << guid << "}; ";
    }

    if (m_userAccountDownloadNotesStatus) {
        strm << "  User account download notes status = ";
        m_userAccountDownloadNotesStatus->print(strm);
        strm << "\n";
    }

    strm << "  Linked notebook download notes statuses ("
         << m_linkedNotebookDownloadNotesStatuses.size() << ") = ";
    if (m_linkedNotebookDownloadNotesStatuses.isEmpty()) {
        strm << "<empty>\n";
    }
    else {
        for (const auto it:
             qevercloud::toRange(m_linkedNotebookDownloadNotesStatuses))
        {
            if (Q_UNLIKELY(!it.value())) {
                continue;
            }

            strm << "{" << it.key() << ": ";
            it.value()->print(strm);
            strm << "};";
        }
        strm << "\n";
    }

    if (m_userAccountDownloadResourcesStatus) {
        strm << "  User account download resources status = ";
        m_userAccountDownloadResourcesStatus->print(strm);
        strm << "\n";
    }

    strm << "  Linked notebook download resources statuses ("
         << m_linkedNotebookDownloadResourcesStatuses.size() << ")= ";
    if (m_linkedNotebookDownloadResourcesStatuses.isEmpty()) {
        strm << "<empty>\n";
    }
    else {
        for (const auto it:
             qevercloud::toRange(m_linkedNotebookDownloadResourcesStatuses))
        {
            if (Q_UNLIKELY(!it.value())) {
                continue;
            }

            strm << "{" << it.key() << ": ";
            it.value()->print(strm);
            strm << "};";
        }
        strm << "\n";
    }

    if (m_userAccountSendStatus) {
        strm << "  user account send status = ";
        m_userAccountSendStatus->print(strm);
        strm << "\n";
    }

    strm << "  Linked notebook send statuses ("
         << m_linkedNotebookSendStatuses.size() << ")= ";
    if (m_linkedNotebookSendStatuses.isEmpty()) {
        strm << "<empty>\n";
    }
    else {
        for (const auto it: qevercloud::toRange(m_linkedNotebookSendStatuses)) {
            if (Q_UNLIKELY(!it.value())) {
                continue;
            }

            strm << "{" << it.key() << ": ";
            it.value()->print(strm);
            strm << "};";
        }
        strm << "\n";
    }

    if (std::holds_alternative<RateLimitReachedError>(
            m_stopSynchronizationError))
    {
        const auto & rateLimitReachedError =
            std::get<RateLimitReachedError>(m_stopSynchronizationError);
        strm << "stopSynchronizationError = RateLimitReachedError{";
        if (rateLimitReachedError.rateLimitDurationSec) {
            strm << "duration = "
                 << *rateLimitReachedError.rateLimitDurationSec;
        }
        strm << "}";
    }
    else if (std::holds_alternative<AuthenticationExpiredError>(
                 m_stopSynchronizationError))
    {
        strm << "stopSynchronizationError = AuthenticationExpiredError";
    }

    return strm;
}

bool operator==(const SyncResult & lhs, const SyncResult & rhs) noexcept
{
    const auto comparePointerData = [](const auto & l, const auto & r) {
        if (static_cast<bool>(l.get()) != static_cast<bool>(r.get())) {
            return false;
        }

        if (l && (l.get() != r.get()) && (*l != *r)) {
            return false;
        }

        return true;
    };

    const auto compareHashData = [&](const auto & l, const auto & r) {
        if (l.size() != r.size()) {
            return false;
        }

        for (auto lit = l.constBegin(), rit = r.constBegin(),
                  lend = l.constEnd(), rend = r.constEnd();
             lit != lend && rit != rend; ++lit, ++rit)
        {
            if (lit.key() != rit.key()) {
                return false;
            }

            if (!comparePointerData(lit.value(), rit.value())) {
                return false;
            }
        }

        return true;
    };

    if (!comparePointerData(lhs.m_syncState, rhs.m_syncState)) {
        return false;
    }

    if (!comparePointerData(
            lhs.m_userAccountDownloadNotesStatus,
            rhs.m_userAccountDownloadNotesStatus))
    {
        return false;
    }

    if (!compareHashData(
            lhs.m_linkedNotebookDownloadNotesStatuses,
            rhs.m_linkedNotebookDownloadNotesStatuses))
    {
        return false;
    }

    if (!comparePointerData(
            lhs.m_userAccountDownloadResourcesStatus,
            rhs.m_userAccountDownloadResourcesStatus))
    {
        return false;
    }

    if (!compareHashData(
            lhs.m_linkedNotebookDownloadResourcesStatuses,
            rhs.m_linkedNotebookDownloadResourcesStatuses))
    {
        return false;
    }

    if (!comparePointerData(
            lhs.m_userAccountSendStatus, rhs.m_userAccountSendStatus))
    {
        return false;
    }

    if (!compareHashData(
            lhs.m_linkedNotebookSendStatuses, rhs.m_linkedNotebookSendStatuses))
    {
        return false;
    }

    if (lhs.m_stopSynchronizationError != rhs.m_stopSynchronizationError) {
        return false;
    }

    return true;
}

} // namespace quentier::synchronization
