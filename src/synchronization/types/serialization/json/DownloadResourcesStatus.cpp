/*
 * Copyright 2024 Dmitry Ivanov
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

#include <quentier/synchronization/types/serialization/json/DownloadResourcesStatus.h>

#include <synchronization/types/DownloadResourcesStatus.h>

#include "../Utils.h"
#include "SerializationUtils.h"

#include <qevercloud/serialization/json/Resource.h>
#include <qevercloud/utility/ToRange.h>

#include <QJsonArray>

#include <string_view>
#include <utility>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gTotalNewResourcesKey = "totalNewResources"sv;
constexpr auto gTotalUpdatedResourcesKey = "totalUpdatedResources"sv;

constexpr auto gResourcesWhichFailedToDownloadKey =
    "resourcesWhichFailedToDownload"sv;

constexpr auto gResourcesWhichFailedToProcessKey =
    "resourcesWhichFailedToProcess"sv;

constexpr auto gResourceKey = "resource"sv;
constexpr auto gGuidKey = "guid"sv;
constexpr auto gExceptionKey = "exception"sv;
constexpr auto gUsnKey = "usn"sv;

constexpr auto gProcessedResourceGuidsAndUsnsKey =
    "processedResourceGuidsAndUsns"sv;

constexpr auto gCancelledResourceGuidsAndUsnsKey =
    "cancelledResourceGuidsAndUsns"sv;

constexpr auto gStopSynchronizationErrorKey = "stopSynchronizationError"sv;

[[nodiscard]] QString toStr(const std::string_view key)
{
    return synchronization::toString(key);
}

} // namespace

QJsonObject serializeDownloadResourcesStatusToJson(
    const IDownloadResourcesStatus & status)
{
    QJsonObject object;

    object[toStr(gTotalNewResourcesKey)] =
        QString::number(status.totalNewResources());

    object[toStr(gTotalUpdatedResourcesKey)] =
        QString::number(status.totalUpdatedResources());

    const auto serializeResourcesWithExceptions =
        [&object](
            const QList<IDownloadResourcesStatus::ResourceWithException> &
                resourcesWithExceptions,
            const std::string_view key) {
            if (resourcesWithExceptions.isEmpty()) {
                return;
            }

            QJsonArray array;
            for (const auto & resourceWithException:
                 std::as_const(resourcesWithExceptions))
            {
                QJsonObject resourceWithExceptionObject;
                resourceWithExceptionObject[toStr(gResourceKey)] =
                    qevercloud::serializeToJson(resourceWithException.first);
                Q_ASSERT(resourceWithException.second);
                resourceWithExceptionObject[toStr(gExceptionKey)] =
                    synchronization::serializeException(
                        *resourceWithException.second);

                array << resourceWithExceptionObject;
            }
            object[toStr(key)] = array;
        };

    const auto failedToDownloadResources =
        status.resourcesWhichFailedToDownload();
    serializeResourcesWithExceptions(
        failedToDownloadResources, gResourcesWhichFailedToDownloadKey);

    const auto failedToProcessResources =
        status.resourcesWhichFailedToProcess();
    serializeResourcesWithExceptions(
        failedToProcessResources, gResourcesWhichFailedToProcessKey);

    const auto serializeUsnsByGuids =
        [&object](
            const IDownloadResourcesStatus::UpdateSequenceNumbersByGuid &
                usnsByGuids,
            const std::string_view key) {
            if (usnsByGuids.isEmpty()) {
                return;
            }

            QJsonArray array;
            for (const auto it: qevercloud::toRange(usnsByGuids)) {
                QJsonObject entry;
                entry[toStr(gGuidKey)] = it.key();
                entry[toStr(gUsnKey)] = it.value();
                array << entry;
            }
            object[toStr(key)] = array;
        };

    const auto processedResourceGuidsWithUsns =
        status.processedResourceGuidsAndUsns();
    serializeUsnsByGuids(
        processedResourceGuidsWithUsns, gProcessedResourceGuidsAndUsnsKey);

    const auto cancelledResourceGuidsWithUsns =
        status.cancelledResourceGuidsAndUsns();
    serializeUsnsByGuids(
        cancelledResourceGuidsWithUsns, gCancelledResourceGuidsAndUsnsKey);

    const auto stopSynchronizationError = status.stopSynchronizationError();
    if (!std::holds_alternative<std::monostate>(stopSynchronizationError)) {
        object[toStr(gStopSynchronizationErrorKey)] =
            serializeStopSynchronizationError(stopSynchronizationError);
    }

    return object;
}

IDownloadResourcesStatusPtr deserializeDownloadResourcesStatusFromJson(
    const QJsonObject & json)
{
    const auto totalNewResourcesIt =
        json.constFind(toStr(gTotalNewResourcesKey));
    if (totalNewResourcesIt == json.constEnd() ||
        !totalNewResourcesIt->isString())
    {
        return nullptr;
    }

    quint64 totalNewResources = 0;
    {
        bool totalNewResourcesOk = false;
        totalNewResources =
            totalNewResourcesIt->toString().toULongLong(&totalNewResourcesOk);
        if (!totalNewResourcesOk) {
            return nullptr;
        }
    }

    const auto totalUpdatedResourcesIt =
        json.constFind(toStr(gTotalUpdatedResourcesKey));
    if (totalUpdatedResourcesIt == json.constEnd() ||
        !totalUpdatedResourcesIt->isString())
    {
        return nullptr;
    }

    quint64 totalUpdatedResources = 0;
    {
        bool totalUpdatedResourcesOk = false;
        totalUpdatedResources = totalUpdatedResourcesIt->toString().toULongLong(
            &totalUpdatedResourcesOk);
        if (!totalUpdatedResourcesOk) {
            return nullptr;
        }
    }

    const auto deserializeResourcesWithExceptions =
        [&json](const std::string_view key)
        -> std::optional<
            QList<IDownloadResourcesStatus::ResourceWithException>> {
        QList<IDownloadResourcesStatus::ResourceWithException>
            resourcesWithExceptions;
        if (const auto it = json.constFind(toStr(key)); it != json.constEnd()) {
            if (!it->isArray()) {
                return std::nullopt;
            }

            const auto array = it->toArray();
            resourcesWithExceptions.reserve(array.size());
            for (auto ait = array.constBegin(); ait != array.constEnd(); ++ait)
            {
                if (!ait->isObject()) {
                    return std::nullopt;
                }

                const auto entry = ait->toObject();
                const auto resourceIt = entry.constFind(toStr(gResourceKey));
                if (resourceIt == entry.constEnd() || !resourceIt->isObject()) {
                    return std::nullopt;
                }

                const auto exceptionIt = entry.constFind(toStr(gExceptionKey));
                if (exceptionIt == entry.constEnd() || !exceptionIt->isObject())
                {
                    return std::nullopt;
                }

                qevercloud::Resource resource;
                if (!qevercloud::deserializeFromJson(
                        resourceIt->toObject(), resource))
                {
                    return std::nullopt;
                }

                auto exception = deserializeException(exceptionIt->toObject());
                if (!exception) {
                    return std::nullopt;
                }

                resourcesWithExceptions
                    << std::pair{std::move(resource), std::move(exception)};
            }
        }

        return resourcesWithExceptions;
    };

    auto failedToDownloadResources =
        deserializeResourcesWithExceptions(gResourcesWhichFailedToDownloadKey);
    if (!failedToDownloadResources) {
        return nullptr;
    }

    auto failedToProcessResources =
        deserializeResourcesWithExceptions(gResourcesWhichFailedToProcessKey);
    if (!failedToProcessResources) {
        return nullptr;
    }

    const auto deserializeUsnsByGuids = [&json](const std::string_view key)
        -> std::optional<
            IDownloadResourcesStatus::UpdateSequenceNumbersByGuid> {
        IDownloadResourcesStatus::UpdateSequenceNumbersByGuid usns;
        if (const auto it = json.constFind(toStr(key)); it != json.constEnd()) {
            if (!it->isArray()) {
                return std::nullopt;
            }

            const auto array = it->toArray();
            for (auto ait = array.constBegin(); ait != array.constEnd(); ++ait)
            {
                if (!ait->isObject()) {
                    return std::nullopt;
                }

                const auto entry = ait->toObject();

                const auto guidIt = entry.constFind(toStr(gGuidKey));
                if (guidIt == entry.constEnd() || !guidIt->isString()) {
                    return std::nullopt;
                }

                const auto usnIt = entry.constFind(toStr(gUsnKey));
                if (usnIt == entry.constEnd() || !usnIt->isDouble()) {
                    return std::nullopt;
                }

                usns[guidIt->toString()] =
                    static_cast<qint32>(usnIt->toDouble());
            }
        }
        return usns;
    };

    auto processedResourceUsns =
        deserializeUsnsByGuids(gProcessedResourceGuidsAndUsnsKey);
    if (!processedResourceUsns) {
        return nullptr;
    }

    auto cancelledResourceUsns =
        deserializeUsnsByGuids(gCancelledResourceGuidsAndUsnsKey);
    if (!cancelledResourceUsns) {
        return nullptr;
    }

    StopSynchronizationError stopSynchronizationError{std::monostate{}};
    if (const auto it = json.constFind(toStr(gStopSynchronizationErrorKey));
        it != json.constEnd())
    {
        if (!it->isObject()) {
            return nullptr;
        }

        const auto entry = it->toObject();
        const auto error = deserializeStopSyncronizationError(entry);
        if (error) {
            stopSynchronizationError = *error;
        }
    }

    auto status = std::make_shared<DownloadResourcesStatus>();
    status->m_totalNewResources = totalNewResources;
    status->m_totalUpdatedResources = totalUpdatedResources;

    status->m_resourcesWhichFailedToDownload =
        std::move(*failedToDownloadResources);

    status->m_resourcesWhichFailedToProcess =
        std::move(*failedToProcessResources);

    status->m_processedResourceGuidsAndUsns = std::move(*processedResourceUsns);
    status->m_cancelledResourceGuidsAndUsns = std::move(*cancelledResourceUsns);

    status->m_stopSynchronizationError = stopSynchronizationError;
    return status;
}

} // namespace quentier::synchronization
