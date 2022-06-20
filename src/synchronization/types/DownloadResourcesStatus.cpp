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

#include <quentier/synchronization/types/DownloadResourcesStatus.h>

#include <qevercloud/utility/ToRange.h>

namespace quentier::synchronization {

QTextStream & DownloadResourcesStatus::print(QTextStream & strm) const
{
    strm << "DownloadResourcesStatus: "
         << "totalNewResources = " << totalNewResources
         << ", totalUpdatedResources = " << totalUpdatedResources;

    const auto printResourceWithExceptionList =
        [&strm](const QList<DownloadResourcesStatus::ResourceWithException> &
                    values) {
            if (values.isEmpty()) {
                strm << "<empty>, ";
                return;
            }

            for (const auto & resourceWithException: qAsConst(values)) {
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
    printResourceWithExceptionList(resourcesWhichFailedToDownload);

    strm << "resourcesWhichFailedToProcess = ";
    printResourceWithExceptionList(resourcesWhichFailedToProcess);

    const auto printResourceGuidsAndUsns =
        [&strm](
            const DownloadResourcesStatus::UpdateSequenceNumbersByGuid & usns) {
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

bool operator==(
    const DownloadResourcesStatus & lhs,
    const DownloadResourcesStatus & rhs) noexcept
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
    const DownloadResourcesStatus & lhs,
    const DownloadResourcesStatus & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
