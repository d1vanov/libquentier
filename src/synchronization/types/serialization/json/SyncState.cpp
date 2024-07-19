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

#include <quentier/synchronization/types/serialization/json/SyncState.h>

#include <synchronization/types/SyncState.h>
#include <synchronization/types/SyncStateBuilder.h>

#include "SerializationUtils.h"
#include "../Utils.h"

#include <qevercloud/utility/ToRange.h>

#include <QJsonArray>

#include <cmath>
#include <string_view>
#include <utility>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gUserDataUpdateCountKey = "userDataUpdateCount"sv;
constexpr auto gUserDataLastSyncTimeKey = "userDataLastSyncTime"sv;
constexpr auto gLinkedNotebookUpdateCountsKey = "linkedNotebookUpdateCounts"sv;
constexpr auto gLinkedNotebookGuidKey = "linkedNotebookGuid"sv;
constexpr auto gLinkedNotebookUpdateCountKey = "linkedNotebookUpdateCount"sv;

constexpr auto gLinkedNotebookLastSyncTimesKey =
    "linkedNotebookLastSyncTimes"sv;

constexpr auto gLinkedNotebookLastSyncTimeKey = "linkedNotebookLastSyncTime"sv;

[[nodiscard]] QString toStr(const std::string_view key)
{
    return synchronization::toString(key);
}

} // namespace

QJsonObject serializeSyncStateToJson(const ISyncState & syncState)
{
    QJsonObject object;

    object[toStr(gUserDataUpdateCountKey)] = syncState.userDataUpdateCount();
    object[toStr(gUserDataLastSyncTimeKey)] = syncState.userDataLastSyncTime();

    const auto linkedNotebookUpdateCounts =
        syncState.linkedNotebookUpdateCounts();

    QJsonArray linkedNotebookUpdateCountsJson;
    for (const auto it:
         qevercloud::toRange(std::as_const(linkedNotebookUpdateCounts)))
    {
        QJsonObject usnObject;
        usnObject[toStr(gLinkedNotebookGuidKey)] = it.key();
        usnObject[toStr(gLinkedNotebookUpdateCountKey)] = it.value();
        linkedNotebookUpdateCountsJson << usnObject;
    }

    object[toStr(gLinkedNotebookUpdateCountsKey)] =
        linkedNotebookUpdateCountsJson;

    const auto linkedNotebookLastSyncTimes =
        syncState.linkedNotebookLastSyncTimes();

    QJsonArray linkedNotebookLastSyncTimesJson;
    for (const auto it:
         qevercloud::toRange(std::as_const(linkedNotebookLastSyncTimes)))
    {
        QJsonObject lastSyncTimeObject;
        lastSyncTimeObject[toStr(gLinkedNotebookGuidKey)] = it.key();
        lastSyncTimeObject[toStr(gLinkedNotebookLastSyncTimeKey)] = it.value();
        linkedNotebookLastSyncTimesJson << lastSyncTimeObject;
    }

    object[toStr(gLinkedNotebookLastSyncTimesKey)] =
        linkedNotebookLastSyncTimesJson;

    return object;
}

ISyncStatePtr deserializeSyncStateFromJson(const QJsonObject & json)
{
    const auto userDataUpdateCountIt =
        json.constFind(toStr(gUserDataUpdateCountKey));
    if (userDataUpdateCountIt == json.constEnd() ||
        !userDataUpdateCountIt->isDouble())
    {
        return nullptr;
    }

    const auto userDataLastSyncTimeIt =
        json.constFind(toStr(gUserDataLastSyncTimeKey));
    if (userDataLastSyncTimeIt == json.constEnd() ||
        !userDataLastSyncTimeIt->isDouble())
    {
        return nullptr;
    }

    const auto linkedNotebookUpdateCountsIt =
        json.constFind(toStr(gLinkedNotebookUpdateCountsKey));
    if (linkedNotebookUpdateCountsIt == json.constEnd() ||
        !linkedNotebookUpdateCountsIt->isArray())
    {
        return nullptr;
    }

    const auto linkedNotebookLastSyncTimesIt =
        json.constFind(toStr(gLinkedNotebookLastSyncTimesKey));
    if (linkedNotebookLastSyncTimesIt == json.constEnd() ||
        !linkedNotebookLastSyncTimesIt->isArray())
    {
        return nullptr;
    }

    const QJsonArray linkedNotebookUpdateCountsJson =
        linkedNotebookUpdateCountsIt->toArray();

    QHash<qevercloud::Guid, qint32> linkedNotebookUpdateCounts;
    linkedNotebookUpdateCounts.reserve(linkedNotebookUpdateCountsJson.size());
    for (auto it = linkedNotebookUpdateCountsJson.constBegin(),
         end = linkedNotebookUpdateCountsJson.constEnd(); it != end; ++it)
    {
        const auto & linkedNotebookUpdateCountJson = *it;
        if (!linkedNotebookUpdateCountJson.isObject()) {
            return nullptr;
        }

        const QJsonObject object = linkedNotebookUpdateCountJson.toObject();

        const auto linkedNotebookGuidIt =
            object.constFind(toStr(gLinkedNotebookGuidKey));
        if (linkedNotebookGuidIt == object.constEnd() ||
            !linkedNotebookGuidIt->isString())
        {
            return nullptr;
        }

        const auto linkedNotebookUsnIt =
            object.constFind(toStr(gLinkedNotebookUpdateCountKey));
        if (linkedNotebookUsnIt == object.constEnd() ||
            !linkedNotebookUsnIt->isDouble())
        {
            return nullptr;
        }

        linkedNotebookUpdateCounts[linkedNotebookGuidIt->toString()] =
            safeCast<double, qint32>(
                std::round(linkedNotebookUsnIt->toDouble()));
    }

    const QJsonArray linkedNotebookLastSyncTimesJson =
        linkedNotebookLastSyncTimesIt->toArray();

    QHash<qevercloud::Guid, qevercloud::Timestamp> linkedNotebookLastSyncTimes;
    linkedNotebookLastSyncTimes.reserve(linkedNotebookLastSyncTimesJson.size());
    for (auto it = linkedNotebookLastSyncTimesJson.constBegin(),
         end = linkedNotebookLastSyncTimesJson.constEnd(); it != end; ++it)
    {
        const auto & linkedNotebookLastSyncTimeJson = *it;
        if (!linkedNotebookLastSyncTimeJson.isObject()) {
            return nullptr;
        }

        const QJsonObject object = linkedNotebookLastSyncTimeJson.toObject();

        const auto linkedNotebookGuidIt =
            object.constFind(toStr(gLinkedNotebookGuidKey));
        if (linkedNotebookGuidIt == object.constEnd() ||
            !linkedNotebookGuidIt->isString())
        {
            return nullptr;
        }

        const auto linkedNotebookLastSyncTimeIt =
            object.constFind(toStr(gLinkedNotebookLastSyncTimeKey));
        if (linkedNotebookLastSyncTimeIt == object.constEnd() ||
            !linkedNotebookLastSyncTimeIt->isDouble())
        {
            return nullptr;
        }

        linkedNotebookLastSyncTimes[linkedNotebookGuidIt->toString()] =
            safeCast<double, qevercloud::Timestamp>(
                std::round(linkedNotebookLastSyncTimeIt->toDouble()));
    }

    return SyncStateBuilder{}
        .setUserDataUpdateCount(safeCast<double, qint32>(
            std::round(userDataUpdateCountIt->toDouble())))
        .setUserDataLastSyncTime(safeCast<double, qevercloud::Timestamp>(
            std::round(userDataLastSyncTimeIt->toDouble())))
        .setLinkedNotebookUpdateCounts(std::move(linkedNotebookUpdateCounts))
        .setLinkedNotebookLastSyncTimes(std::move(linkedNotebookLastSyncTimes))
        .build();
}

} // namespace quentier::synchronization
