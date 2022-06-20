/*
 * Copyright 2022 Dmitry Ivanov
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

#include <quentier/synchronization/ISynchronizer.h>
#include <quentier/utility/DateTime.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

QTextStream & ISynchronizer::DownloadResourcesStatus::print(
    QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadResourcesStatus: "
         << "totalNewResources = " << totalNewResources
         << ", totalUpdatedResources = " << totalUpdatedResources;

    const auto printResourceWithExceptionList =
        [&strm](const QList<
                ISynchronizer::DownloadResourcesStatus::ResourceWithException> &
                    values) {
            if (values.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto & resourceWithException: qAsConst(values)) {
                strm << "{" << resourceWithException << "};";
            }
            strm << " ";
        };

    strm << ", resourcesWhichFailedToDownload = ";
    printResourceWithExceptionList(resourcesWhichFailedToDownload);

    strm << "resourcesWhichFailedToProcess = ";
    printResourceWithExceptionList(resourcesWhichFailedToProcess);

    const auto printResourceGuidsAndUsns =
        [&strm](const ISynchronizer::DownloadResourcesStatus::
                    UpdateSequenceNumbersByGuid & usns) {
            if (usns.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto it: qevercloud::toRange(qAsConst(usns))) {
                strm << "{" << it.key() << ": " << it.value() << "};";
            }
            strm << " ";
        };

    strm << "processedResourceGuidsAndUsns = ";
    printResourceGuidsAndUsns(processedResourceGuidsAndUsns);

    strm << "cancelledResourceGuidsAndUsns = ";
    printResourceGuidsAndUsns(cancelledResourceGuidsAndUsns);

    return strm;
}

QTextStream &
    ISynchronizer::DownloadResourcesStatus::ResourceWithException::print(
        QTextStream & strm) const
{
    strm << "ISynchronizer::DownloadResourcesStatus::ResourceWithException: "
            "resource = "
         << resource << ", exception: ";

    if (exception) {
        try {
            exception->raise();
        }
        catch (const QException & e) {
            strm << e.what();
        }
    }
    else {
        strm << "<no info>";
    }

    return strm;
}

QTextStream & ISynchronizer::SyncResult::print(QTextStream & strm) const
{
    strm << "ISynchronizer::SyncResult: userAccountSyncState = "
         << userAccountSyncState << ", linkedNotebookSyncStates = ";

    if (linkedNotebookSyncStates.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it: qevercloud::toRange(linkedNotebookSyncStates)) {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "userAccountDownloadNotesStatus = "
         << userAccountDownloadNotesStatus
         << ", linkedNotebookDownloadNotesStatuses = ";

    if (linkedNotebookDownloadNotesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadNotesStatuses)) {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "userAccountDownloadResourcesStatus = "
         << userAccountDownloadResourcesStatus
         << ", linkedNotebookDownloadResourcesStatuses = ";
    if (linkedNotebookDownloadResourcesStatuses.isEmpty()) {
        strm << "<empty>, ";
    }
    else {
        for (const auto it:
             qevercloud::toRange(linkedNotebookDownloadResourcesStatuses))
        {
            strm << "{" << it.key() << ": " << it.value() << "};";
        }
        strm << " ";
    }

    strm << "syncStats = " << syncStats;
    return strm;
}

bool operator==(
    const ISynchronizer::DownloadResourcesStatus & lhs,
    const ISynchronizer::DownloadResourcesStatus & rhs) noexcept
{
    // clang-format off
    return lhs.totalNewResources == rhs.totalNewResources &&
        lhs.totalUpdatedResources == rhs.totalUpdatedResources &&
        lhs.resourcesWhichFailedToDownload ==
            rhs.resourcesWhichFailedToDownload &&
        lhs.resourcesWhichFailedToProcess ==
            rhs.resourcesWhichFailedToProcess &&
        lhs.processedResourceGuidsAndUsns ==
            rhs.processedResourceGuidsAndUsns &&
        lhs.cancelledResourceGuidsAndUsns == rhs.cancelledResourceGuidsAndUsns;
    // clang-format on
}

bool operator!=(
    const ISynchronizer::DownloadResourcesStatus & lhs,
    const ISynchronizer::DownloadResourcesStatus & rhs) noexcept
{
    return !(lhs == rhs);
}

bool operator==(
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException & lhs,
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException &
        rhs) noexcept
{
    return lhs.resource == rhs.resource && lhs.exception == rhs.exception;
}

bool operator!=(
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException & lhs,
    const ISynchronizer::DownloadResourcesStatus::ResourceWithException &
        rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
