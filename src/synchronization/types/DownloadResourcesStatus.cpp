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

#include "DownloadResourcesStatus.h"

#include <qevercloud/utility/ToRange.h>

#include <utility>

namespace quentier::synchronization {

quint64 DownloadResourcesStatus::totalNewResources() const noexcept
{
    return m_totalNewResources;
}

quint64 DownloadResourcesStatus::totalUpdatedResources() const noexcept
{
    return m_totalUpdatedResources;
}

QList<IDownloadResourcesStatus::ResourceWithException>
    DownloadResourcesStatus::resourcesWhichFailedToDownload() const
{
    return m_resourcesWhichFailedToDownload;
}

QList<IDownloadResourcesStatus::ResourceWithException>
    DownloadResourcesStatus::resourcesWhichFailedToProcess() const
{
    return m_resourcesWhichFailedToProcess;
}

IDownloadResourcesStatus::UpdateSequenceNumbersByGuid
    DownloadResourcesStatus::processedResourceGuidsAndUsns() const
{
    return m_processedResourceGuidsAndUsns;
}

IDownloadResourcesStatus::UpdateSequenceNumbersByGuid
    DownloadResourcesStatus::cancelledResourceGuidsAndUsns() const
{
    return m_cancelledResourceGuidsAndUsns;
}

StopSynchronizationError DownloadResourcesStatus::stopSynchronizationError()
    const
{
    return m_stopSynchronizationError;
}

QTextStream & DownloadResourcesStatus::print(QTextStream & strm) const
{
    strm << "DownloadResourcesStatus: "
         << "totalNewResources = " << m_totalNewResources
         << ", totalUpdatedResources = " << m_totalUpdatedResources;

    const auto printResourceWithExceptionList =
        [&strm](const QList<DownloadResourcesStatus::ResourceWithException> &
                    values) {
            if (values.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto & resourceWithException: std::as_const(values)) {
                strm << "{resource: " << resourceWithException.first
                     << "\nException:";

                if (resourceWithException.second) {
                    try {
                        resourceWithException.second->raise();
                    }
                    catch (const QException & e) {
                        strm << e.what();
                    }
                }
                else {
                    strm << "<no exception info>";
                }

                strm << "};";
            }
            strm << " ";
        };

    strm << ", resourcesWhichFailedToDownload = ";
    printResourceWithExceptionList(m_resourcesWhichFailedToDownload);

    strm << "resourcesWhichFailedToProcess = ";
    printResourceWithExceptionList(m_resourcesWhichFailedToProcess);

    const auto printResourceGuidsAndUsns =
        [&strm](
            const DownloadResourcesStatus::UpdateSequenceNumbersByGuid & usns) {
            if (usns.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto it: qevercloud::toRange(std::as_const(usns))) {
                strm << "{" << it.key() << ": " << it.value() << "};";
            }
            strm << " ";
        };

    strm << "processedResourceGuidsAndUsns = ";
    printResourceGuidsAndUsns(m_processedResourceGuidsAndUsns);

    strm << "cancelledResourceGuidsAndUsns = ";
    printResourceGuidsAndUsns(m_cancelledResourceGuidsAndUsns);

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

bool operator==(
    const DownloadResourcesStatus & lhs,
    const DownloadResourcesStatus & rhs) noexcept
{
    const auto compareExceptionPtrs =
        [](const DownloadResourcesStatus::QExceptionPtr & lhs,
           const DownloadResourcesStatus::QExceptionPtr & rhs) {
            if (!lhs && !rhs) {
                return true;
            }

            if (lhs && !rhs) {
                return false;
            }

            if (!lhs && rhs) {
                return false;
            }

            const auto lhsMsg = QString::fromUtf8(lhs->what());
            const auto rhsMsg = QString::fromUtf8(rhs->what());
            return lhsMsg == rhsMsg;
        };

    const auto compareResourcesWithExceptions =
        [&](const IDownloadResourcesStatus::ResourceWithException & lhs,
            const IDownloadResourcesStatus::ResourceWithException & rhs) {
            return lhs.first == rhs.first &&
                compareExceptionPtrs(lhs.second, rhs.second);
        };

    const auto compareResourceListsWithExceptions =
        [&](const QList<IDownloadResourcesStatus::ResourceWithException> & lhs,
            const QList<IDownloadResourcesStatus::ResourceWithException> &
                rhs) {
            if (lhs.size() != rhs.size()) {
                return false;
            }

            for (int i = 0; i < lhs.size(); ++i) {
                if (!compareResourcesWithExceptions(lhs[i], rhs[i])) {
                    return false;
                }
            }

            return true;
        };

    if (lhs.m_totalNewResources != rhs.m_totalNewResources ||
        lhs.m_totalUpdatedResources != rhs.m_totalUpdatedResources ||
        !compareResourceListsWithExceptions(
            lhs.m_resourcesWhichFailedToDownload,
            rhs.m_resourcesWhichFailedToDownload) ||
        !compareResourceListsWithExceptions(
            lhs.m_resourcesWhichFailedToProcess,
            rhs.m_resourcesWhichFailedToProcess) ||
        lhs.m_processedResourceGuidsAndUsns !=
            rhs.m_processedResourceGuidsAndUsns ||
        lhs.m_cancelledResourceGuidsAndUsns !=
            rhs.m_cancelledResourceGuidsAndUsns)
    {
        return false;
    }

    if (std::holds_alternative<RateLimitReachedError>(
            lhs.m_stopSynchronizationError))
    {
        if (!std::holds_alternative<RateLimitReachedError>(
                rhs.m_stopSynchronizationError))
        {
            return false;
        }

        const auto & l =
            std::get<RateLimitReachedError>(lhs.m_stopSynchronizationError);
        const auto & r =
            std::get<RateLimitReachedError>(rhs.m_stopSynchronizationError);
        return l.rateLimitDurationSec == r.rateLimitDurationSec;
    }

    if (std::holds_alternative<AuthenticationExpiredError>(
            lhs.m_stopSynchronizationError))
    {
        return std::holds_alternative<AuthenticationExpiredError>(
            rhs.m_stopSynchronizationError);
    }

    return true;
}

bool operator!=(
    const DownloadResourcesStatus & lhs,
    const DownloadResourcesStatus & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
