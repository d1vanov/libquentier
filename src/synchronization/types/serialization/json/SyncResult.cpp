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

#include <quentier/synchronization/types/serialization/json/SyncResult.h>

#include <quentier/synchronization/types/serialization/json/DownloadNotesStatus.h>
#include <quentier/synchronization/types/serialization/json/DownloadResourcesStatus.h>
#include <quentier/synchronization/types/serialization/json/SendStatus.h>
#include <quentier/synchronization/types/serialization/json/SyncChunksDataCounters.h>
#include <quentier/synchronization/types/serialization/json/SyncState.h>

#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SendStatus.h>
#include <synchronization/types/SyncChunksDataCounters.h>
#include <synchronization/types/SyncResult.h>
#include <synchronization/types/SyncState.h>

#include "../Utils.h"
#include "SerializationUtils.h"

#include <qevercloud/utility/ToRange.h>

#include <QJsonArray>

#include <cmath>
#include <string_view>
#include <utility>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gSyncStateKey = "syncState"sv;
constexpr auto gUserAccountSyncChunksDataCountersKey =
    "userAccountSyncChunksDataCounters"sv;

constexpr auto gLinkedNotebookSyncChunksDataCountersKey =
    "linkedNotebookSyncChunksDataCounters"sv;

constexpr auto gLinkedNotebookGuidKey = "linkedNotebookGuid"sv;
constexpr auto gSyncChunksDataCountersKey = "syncChunksDataCounters"sv;
constexpr auto gDownloadNotesStatusKey = "downloadNotesStatus"sv;
constexpr auto gDownloadResourcesStatusKey = "downloadResourcesStatus"sv;
constexpr auto gSendStatusKey = "sendStatus"sv;

constexpr auto gUserAccountDownloadNotesStatusKey =
    "userAccountDownloadNotesStatus"sv;

constexpr auto gLinkedNotebookDownloadNotesStatusesKey =
    "linkedNotebookDownloadNotesStatuses"sv;

constexpr auto gUserAccountDownloadResourcesStatusKey =
    "userAccountDownloadResourcesStatus"sv;

constexpr auto gLinkedNotebookDownloadResourcesStatusesKey =
    "linkedNotebookDownloadResourcesStatuses"sv;

constexpr auto gUserAccountSendStatusKey = "userAccountSendStatus"sv;
constexpr auto gLinkedNotebookSendStatuses = "linkedNotebookSendStatuses"sv;
constexpr auto gStopSynchronizationErrorKey = "stopSynchronizationError"sv;

[[nodiscard]] QString toStr(const std::string_view key)
{
    return synchronization::toString(key);
}

} // namespace

QJsonObject serializeSyncResultToJson(const ISyncResult & syncResult)
{
    QJsonObject object;

    const auto syncState = syncResult.syncState();
    if (syncState) {
        object[toStr(gSyncStateKey)] = serializeSyncStateToJson(*syncState);
    }
    else {
        object[toStr(gSyncStateKey)] = QJsonValue::Null;
    }

    const auto userAccountSyncChunksDataCounters =
        syncResult.userAccountSyncChunksDataCounters();
    if (userAccountSyncChunksDataCounters) {
        object[toStr(gUserAccountSyncChunksDataCountersKey)] =
            serializeSyncChunksDataCountersToJson(
                *userAccountSyncChunksDataCounters);
    }
    else {
        object[toStr(gUserAccountSyncChunksDataCountersKey)] = QJsonValue::Null;
    }

    const auto linkedNotebookSyncChunksDataCounters =
        syncResult.linkedNotebookSyncChunksDataCounters();
    QJsonArray linkedNotebookSyncChunksDataCountersArray;

    for (const auto it: qevercloud::toRange(
             std::as_const(linkedNotebookSyncChunksDataCounters)))
    {
        QJsonObject obj;
        obj[toStr(gLinkedNotebookGuidKey)] = it.key();

        const auto & syncChunksDataCounters = it.value();
        if (syncChunksDataCounters) {
            obj[toStr(gSyncChunksDataCountersKey)] =
                serializeSyncChunksDataCountersToJson(*syncChunksDataCounters);
        }
        else {
            obj[toStr(gSyncChunksDataCountersKey)] = QJsonValue::Null;
        }

        linkedNotebookSyncChunksDataCountersArray << obj;
    }
    object[toStr(gLinkedNotebookSyncChunksDataCountersKey)] =
        linkedNotebookSyncChunksDataCountersArray;

    const auto userAccountDownloadNotesStatus =
        syncResult.userAccountDownloadNotesStatus();
    if (userAccountDownloadNotesStatus) {
        object[toStr(gUserAccountDownloadNotesStatusKey)] =
            serializeDownloadNotesStatusToJson(*userAccountDownloadNotesStatus);
    }
    else {
        object[toStr(gUserAccountDownloadNotesStatusKey)] = QJsonValue::Null;
    }

    const auto linkedNotebookDownloadNotesStatuses =
        syncResult.linkedNotebookDownloadNotesStatuses();
    QJsonArray linkedNotebookDownloadNotesStatusesArray;

    for (const auto it: qevercloud::toRange(
             std::as_const(linkedNotebookDownloadNotesStatuses)))
    {
        QJsonObject obj;
        obj[toStr(gLinkedNotebookGuidKey)] = it.key();

        const auto & downloadNotesStatus = it.value();
        if (downloadNotesStatus) {
            obj[toStr(gDownloadNotesStatusKey)] =
                serializeDownloadNotesStatusToJson(*downloadNotesStatus);
        }
        else {
            obj[toStr(gDownloadNotesStatusKey)] = QJsonValue::Null;
        }

        linkedNotebookDownloadNotesStatusesArray << obj;
    }
    object[toStr(gLinkedNotebookDownloadNotesStatusesKey)] =
        linkedNotebookDownloadNotesStatusesArray;

    const auto userAccountDownloadResourcesStatus =
        syncResult.userAccountDownloadResourcesStatus();
    if (userAccountDownloadResourcesStatus) {
        object[toStr(gUserAccountDownloadResourcesStatusKey)] =
            serializeDownloadResourcesStatusToJson(
                *userAccountDownloadResourcesStatus);
    }
    else {
        object[toStr(gUserAccountDownloadResourcesStatusKey)] =
            QJsonValue::Null;
    }

    const auto linkedNotebookDownloadResourcesStatuses =
        syncResult.linkedNotebookDownloadResourcesStatuses();
    QJsonArray linkedNotebookDownloadResourcesStatusesArray;

    for (const auto it: qevercloud::toRange(
             std::as_const(linkedNotebookDownloadResourcesStatuses)))
    {
        QJsonObject obj;
        obj[toStr(gLinkedNotebookGuidKey)] = it.key();

        const auto & downloadResourcesStatus = it.value();
        if (downloadResourcesStatus) {
            obj[toStr(gDownloadResourcesStatusKey)] =
                serializeDownloadResourcesStatusToJson(
                    *downloadResourcesStatus);
        }
        else {
            obj[toStr(gDownloadResourcesStatusKey)] = QJsonValue::Null;
        }

        linkedNotebookDownloadResourcesStatusesArray << obj;
    }
    object[toStr(gLinkedNotebookDownloadResourcesStatusesKey)] =
        linkedNotebookDownloadResourcesStatusesArray;

    const auto userAccountSendStatus = syncResult.userAccountSendStatus();
    if (userAccountSendStatus) {
        object[toStr(gUserAccountSendStatusKey)] =
            serializeSendStatusToJson(*userAccountSendStatus);
    }
    else {
        object[toStr(gUserAccountSendStatusKey)] = QJsonValue::Null;
    }

    const auto linkedNotebookSendStatuses =
        syncResult.linkedNotebookSendStatuses();
    QJsonArray linkedNotebookSendStatusesArray;

    for (const auto it:
         qevercloud::toRange(std::as_const(linkedNotebookSendStatuses)))
    {
        QJsonObject obj;
        obj[toStr(gLinkedNotebookGuidKey)] = it.key();

        const auto & sendStatus = it.value();
        if (sendStatus) {
            obj[toStr(gSendStatusKey)] = serializeSendStatusToJson(*sendStatus);
        }
        else {
            obj[toStr(gSendStatusKey)] = QJsonValue::Null;
        }

        linkedNotebookSendStatusesArray << obj;
    }
    object[toStr(gLinkedNotebookSendStatuses)] =
        linkedNotebookSendStatusesArray;

    const auto stopSynchronizationError = syncResult.stopSynchronizationError();
    if (!std::holds_alternative<std::monostate>(stopSynchronizationError)) {
        object[toStr(gStopSynchronizationErrorKey)] =
            serializeStopSynchronizationError(stopSynchronizationError);
    }

    return object;
}

ISyncResultPtr deserializeSyncResultFromJson(const QJsonObject & json)
{
    auto syncResult = std::make_shared<SyncResult>();

    if (const auto it = json.constFind(toStr(gSyncStateKey));
        it != json.constEnd() && it->isObject())
    {
        const QJsonObject obj = it->toObject();
        syncResult->m_syncState = std::dynamic_pointer_cast<SyncState>(
            deserializeSyncStateFromJson(obj));
    }

    if (const auto it =
            json.constFind(toStr(gUserAccountSyncChunksDataCountersKey));
        it != json.constEnd() && it->isObject())
    {
        const QJsonObject obj = it->toObject();
        syncResult->m_userAccountSyncChunksDataCounters =
            std::dynamic_pointer_cast<SyncChunksDataCounters>(
                deserializeSyncChunksDataCountersFromJson(obj));
    }

    if (const auto it =
            json.constFind(toStr(gLinkedNotebookSyncChunksDataCountersKey));
        it != json.constEnd() && it->isArray())
    {
        QHash<qevercloud::Guid, SyncChunksDataCountersPtr>
            linkedNotebookSyncChunksDataCounters;

        const QJsonArray array = it->toArray();
        for (auto ait = array.constBegin(), aend = array.constEnd();
             ait != aend; ++ait)
        {
            if (!ait->isObject()) {
                return nullptr;
            }

            const QJsonObject obj = ait->toObject();

            const auto guidIt = obj.constFind(toStr(gLinkedNotebookGuidKey));
            if (guidIt == obj.constEnd() || !guidIt->isString()) {
                return nullptr;
            }

            SyncChunksDataCountersPtr syncChunksDataCounters;
            const auto countersIt =
                obj.constFind(toStr(gSyncChunksDataCountersKey));
            if (countersIt == obj.constEnd()) {
                return nullptr;
            }

            if (countersIt->isObject()) {
                syncChunksDataCounters =
                    std::dynamic_pointer_cast<SyncChunksDataCounters>(
                        deserializeSyncChunksDataCountersFromJson(
                            countersIt->toObject()));
            }
            else {
                continue;
            }

            linkedNotebookSyncChunksDataCounters[guidIt->toString()] =
                std::move(syncChunksDataCounters);
        }

        syncResult->m_linkedNotebookSyncChunksDataCounters =
            std::move(linkedNotebookSyncChunksDataCounters);
    }

    if (const auto it =
            json.constFind(toStr(gUserAccountDownloadNotesStatusKey));
        it != json.constEnd() && it->isObject())
    {
        const QJsonObject obj = it->toObject();
        syncResult->m_userAccountDownloadNotesStatus =
            std::dynamic_pointer_cast<DownloadNotesStatus>(
                deserializeDownloadNotesStatusFromJson(obj));
    }

    if (const auto it =
            json.constFind(toStr(gLinkedNotebookDownloadNotesStatusesKey));
        it != json.constEnd() && it->isArray())
    {
        QHash<qevercloud::Guid, DownloadNotesStatusPtr>
            linkedNotebookDownloadNotesStatuses;

        const QJsonArray array = it->toArray();
        for (auto ait = array.constBegin(), aend = array.constEnd();
             ait != aend; ++ait)
        {
            if (!ait->isObject()) {
                return nullptr;
            }

            const QJsonObject obj = ait->toObject();

            const auto guidIt = obj.constFind(toStr(gLinkedNotebookGuidKey));
            if (guidIt == obj.constEnd() || !guidIt->isString()) {
                return nullptr;
            }

            DownloadNotesStatusPtr downloadNotesStatus;
            const auto statusIt = obj.constFind(toStr(gDownloadNotesStatusKey));
            if (statusIt == obj.constEnd()) {
                return nullptr;
            }

            if (statusIt->isObject()) {
                downloadNotesStatus =
                    std::dynamic_pointer_cast<DownloadNotesStatus>(
                        deserializeDownloadNotesStatusFromJson(
                            statusIt->toObject()));
            }
            else {
                continue;
            }

            linkedNotebookDownloadNotesStatuses[guidIt->toString()] =
                std::move(downloadNotesStatus);
        }

        syncResult->m_linkedNotebookDownloadNotesStatuses =
            std::move(linkedNotebookDownloadNotesStatuses);
    }

    if (const auto it =
            json.constFind(toStr(gUserAccountDownloadResourcesStatusKey));
        it != json.constEnd() && it->isObject())
    {
        const QJsonObject obj = it->toObject();
        syncResult->m_userAccountDownloadResourcesStatus =
            std::dynamic_pointer_cast<DownloadResourcesStatus>(
                deserializeDownloadResourcesStatusFromJson(obj));
    }

    if (const auto it =
            json.constFind(toStr(gLinkedNotebookDownloadResourcesStatusesKey));
        it != json.constEnd() && it->isArray())
    {
        QHash<qevercloud::Guid, DownloadResourcesStatusPtr>
            linkedNotebookDownloadResourcesStatuses;

        const QJsonArray array = it->toArray();
        for (auto ait = array.constBegin(), aend = array.constEnd();
             ait != aend; ++ait)
        {
            if (!ait->isObject()) {
                return nullptr;
            }

            const QJsonObject obj = ait->toObject();

            const auto guidIt = obj.constFind(toStr(gLinkedNotebookGuidKey));
            if (guidIt == obj.constEnd() || !guidIt->isString()) {
                return nullptr;
            }

            DownloadResourcesStatusPtr downloadResourcesStatus;
            const auto statusIt =
                obj.constFind(toStr(gDownloadResourcesStatusKey));
            if (statusIt == obj.constEnd()) {
                return nullptr;
            }

            if (statusIt->isObject()) {
                downloadResourcesStatus =
                    std::dynamic_pointer_cast<DownloadResourcesStatus>(
                        deserializeDownloadResourcesStatusFromJson(
                            statusIt->toObject()));
            }
            else {
                continue;
            }

            linkedNotebookDownloadResourcesStatuses[guidIt->toString()] =
                std::move(downloadResourcesStatus);
        }

        syncResult->m_linkedNotebookDownloadResourcesStatuses =
            std::move(linkedNotebookDownloadResourcesStatuses);
    }

    if (const auto it = json.constFind(toStr(gUserAccountSendStatusKey));
        it != json.constEnd() && it->isObject())
    {
        const QJsonObject obj = it->toObject();
        syncResult->m_userAccountSendStatus =
            std::dynamic_pointer_cast<SendStatus>(
                deserializeSendStatusFromJson(obj));
    }

    if (const auto it = json.constFind(toStr(gLinkedNotebookSendStatuses));
        it != json.constEnd() && it->isArray())
    {
        QHash<qevercloud::Guid, SendStatusPtr> linkedNotebookSendStatuses;

        const QJsonArray array = it->toArray();
        for (auto ait = array.constBegin(), aend = array.constEnd();
             ait != aend; ++ait)
        {
            if (!ait->isObject()) {
                return nullptr;
            }

            const QJsonObject obj = ait->toObject();

            const auto guidIt = obj.constFind(toStr(gLinkedNotebookGuidKey));
            if (guidIt == obj.constEnd() || !guidIt->isString()) {
                return nullptr;
            }

            SendStatusPtr sendStatus;
            const auto statusIt = obj.constFind(toStr(gSendStatusKey));
            if (statusIt == obj.constEnd()) {
                return nullptr;
            }

            if (statusIt->isObject()) {
                sendStatus = std::dynamic_pointer_cast<SendStatus>(
                    deserializeSendStatusFromJson(statusIt->toObject()));
            }
            else {
                continue;
            }

            linkedNotebookSendStatuses[guidIt->toString()] =
                std::move(sendStatus);
        }

        syncResult->m_linkedNotebookSendStatuses =
            std::move(linkedNotebookSendStatuses);
    }

    if (const auto it = json.constFind(toStr(gStopSynchronizationErrorKey));
        it != json.constEnd() && it->isObject())
    {
        const QJsonObject obj = it->toObject();
        if (auto stopSynchronizationError =
                deserializeStopSyncronizationError(obj))
        {
            syncResult->m_stopSynchronizationError = *stopSynchronizationError;
        }
    }

    return syncResult;
}

} // namespace quentier::synchronization
