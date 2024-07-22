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

#include <quentier/synchronization/types/serialization/json/SendStatus.h>

#include <synchronization/types/SendStatus.h>

#include "../Utils.h"
#include "SerializationUtils.h"

#include <qevercloud/serialization/json/Note.h>
#include <qevercloud/serialization/json/Notebook.h>
#include <qevercloud/serialization/json/SavedSearch.h>
#include <qevercloud/serialization/json/Tag.h>

#include <QJsonArray>

#include <string_view>
#include <utility>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gTotalAttemptedToSendNotesKey = "totalAttemptedToSendNotes"sv;
constexpr auto gTotalAttemptedToSendNotebooksKey =
    "totalAttemptedToSendNotebooks"sv;

constexpr auto gTotalAttemptedToSendSavedSearchesKey =
    "totalAttemptedToSendSavedSearches"sv;

constexpr auto gTotalAttemptedToSendTagsKey = "totalAttemptedToSendTags"sv;
constexpr auto gTotalSuccessfullySentNotesKey = "totalSuccessfullySentNotes"sv;
constexpr auto gFailedToSendNotesKey = "failedToSendNotes"sv;

constexpr auto gTotalSuccessfullySentNotebooksKey =
    "totalSuccessfullySentNotebooks"sv;

constexpr auto gFailedToSendNotebooksKey = "failedToSendNotebooks"sv;
constexpr auto gTotalSuccessfullySentSavedSearchesKey =
    "totalSuccessfullySentSavedSearches"sv;

constexpr auto gFailedToSendSavedSearchesKey = "failedToSendSavedSearches"sv;
constexpr auto gTotalSuccessfullySentTagsKey = "totalSuccessfullySentTags"sv;
constexpr auto gFailedToSendTagsKey = "failedToSendTags"sv;
constexpr auto gStopSynchronizationErrorKey = "stopSynchronizationError"sv;
constexpr auto gNeedToRepeatIncrementalSync = "needToRepeatIncrementalSync"sv;

constexpr auto gNoteKey = "note"sv;
constexpr auto gNotebookKey = "notebook"sv;
constexpr auto gSavedSearchKey = "savedSearch"sv;
constexpr auto gTagKey = "tag"sv;
constexpr auto gExceptionKey = "exception"sv;

[[nodiscard]] QString toStr(const std::string_view key)
{
    return synchronization::toString(key);
}

[[nodiscard]] bool readCounter(
    const std::string_view key, const QJsonObject & json,
    quint64 & counterValue)
{
    const auto it = json.constFind(toStr(key));
    if (it == json.constEnd() || !it->isString()) {
        return false;
    }

    bool conversionResult = false;
    counterValue = it->toString().toULongLong(&conversionResult);
    return conversionResult;
}

template <class TWithException>
[[nodiscard]] std::optional<QList<TWithException>>
    deserializeItemsWithExceptions(
        const std::string_view itemKey, const std::string_view itemsKey,
        const QJsonObject & json)
{
    QList<TWithException> itemsWithExceptions;
    if (const auto it = json.constFind(toStr(itemsKey)); it != json.constEnd())
    {
        if (!it->isArray()) {
            return std::nullopt;
        }

        const auto array = it->toArray();
        itemsWithExceptions.reserve(array.size());
        for (auto ait = array.constBegin(); ait != array.constEnd(); ++ait) {
            if (!ait->isObject()) {
                return std::nullopt;
            }

            const auto entry = ait->toObject();
            const auto itemIt = entry.constFind(toStr(itemKey));
            if (itemIt == entry.constEnd() || !itemIt->isObject()) {
                return std::nullopt;
            }

            const auto exceptionIt = entry.constFind(toStr(gExceptionKey));
            if (exceptionIt == entry.constEnd() || !exceptionIt->isObject()) {
                return std::nullopt;
            }

            typename TWithException::first_type item;
            if (!qevercloud::deserializeFromJson(itemIt->toObject(), item)) {
                return std::nullopt;
            }

            auto exception = deserializeException(exceptionIt->toObject());
            if (!exception) {
                return std::nullopt;
            }

            itemsWithExceptions
                << std::pair{std::move(item), std::move(exception)};
        }
    }

    return itemsWithExceptions;
}

} // namespace

QJsonObject serializeSendStatusToJson(const ISendStatus & sendStatus)
{
    QJsonObject object;

    object[toStr(gTotalAttemptedToSendNotesKey)] =
        QString::number(sendStatus.totalAttemptedToSendSavedSearches());

    object[toStr(gTotalAttemptedToSendNotebooksKey)] =
        QString::number(sendStatus.totalAttemptedToSendNotebooks());

    object[toStr(gTotalAttemptedToSendSavedSearchesKey)] =
        QString::number(sendStatus.totalAttemptedToSendSavedSearches());

    object[toStr(gTotalAttemptedToSendTagsKey)] =
        QString::number(sendStatus.totalAttemptedToSendTags());

    const auto serializeItemsWithExceptions =
        [&object](
            const auto & itemsWithExceptions, const std::string_view itemKey,
            const std::string_view itemsKey) {
            if (itemsWithExceptions.isEmpty()) {
                return;
            }

            QJsonArray array;
            for (const auto & itemWithException:
                 std::as_const(itemsWithExceptions)) {
                QJsonObject itemWithExceptionObject;
                itemWithExceptionObject[toStr(itemKey)] =
                    qevercloud::serializeToJson(itemWithException.first);
                Q_ASSERT(itemWithException.second);
                itemWithExceptionObject[toStr(gExceptionKey)] =
                    synchronization::serializeException(
                        *itemWithException.second);

                array << itemWithExceptionObject;
            }
            object[toStr(itemsKey)] = array;
        };

    object[toStr(gTotalSuccessfullySentNotesKey)] =
        QString::number(sendStatus.totalSuccessfullySentNotes());

    const auto failedToSendNotes = sendStatus.failedToSendNotes();
    serializeItemsWithExceptions(
        failedToSendNotes, gNoteKey, gFailedToSendNotesKey);

    object[toStr(gTotalSuccessfullySentNotebooksKey)] =
        QString::number(sendStatus.totalSuccessfullySentNotebooks());

    const auto failedToSendNotebooks = sendStatus.failedToSendNotebooks();
    serializeItemsWithExceptions(
        failedToSendNotebooks, gNotebookKey, gFailedToSendNotebooksKey);

    object[toStr(gTotalSuccessfullySentSavedSearchesKey)] =
        QString::number(sendStatus.totalSuccessfullySentSavedSearches());

    const auto failedToSendSavedSearches =
        sendStatus.failedToSendSavedSearches();
    serializeItemsWithExceptions(
        failedToSendSavedSearches, gSavedSearchKey,
        gFailedToSendSavedSearchesKey);

    object[toStr(gTotalSuccessfullySentTagsKey)] =
        QString::number(sendStatus.totalSuccessfullySentTags());

    const auto failedToSendTags = sendStatus.failedToSendTags();
    serializeItemsWithExceptions(
        failedToSendTags, gTagKey, gFailedToSendTagsKey);

    const auto stopSynchronizationError = sendStatus.stopSynchronizationError();
    if (!std::holds_alternative<std::monostate>(stopSynchronizationError)) {
        object[toStr(gStopSynchronizationErrorKey)] =
            serializeStopSynchronizationError(stopSynchronizationError);
    }

    object[toStr(gNeedToRepeatIncrementalSync)] =
        sendStatus.needToRepeatIncrementalSync();

    return object;
}

ISendStatusPtr deserializeSendStatusFromJson(const QJsonObject & json)
{
    quint64 totalAttemptedToSendNotes = 0UL;
    if (!readCounter(
            gTotalAttemptedToSendNotesKey, json, totalAttemptedToSendNotes))
    {
        return nullptr;
    }

    quint64 totalAttemptedToSendNotebooks = 0UL;
    if (!readCounter(
            gTotalAttemptedToSendNotebooksKey, json,
            totalAttemptedToSendNotebooks))
    {
        return nullptr;
    }

    quint64 totalAttemptedToSendSavedSearches = 0UL;
    if (!readCounter(
            gTotalAttemptedToSendSavedSearchesKey, json,
            totalAttemptedToSendSavedSearches))
    {
        return nullptr;
    }

    quint64 totalAttemptedToSendTags = 0UL;
    if (!readCounter(
            gTotalAttemptedToSendTagsKey, json, totalAttemptedToSendTags))
    {
        return nullptr;
    }

    quint64 totalSuccessfullySentNotes = 0UL;
    if (!readCounter(
            gTotalSuccessfullySentNotesKey, json, totalSuccessfullySentNotes))
    {
        return nullptr;
    }

    auto failedToSendNotes =
        deserializeItemsWithExceptions<ISendStatus::NoteWithException>(
            gFailedToSendNotesKey, gNoteKey, json);
    if (!failedToSendNotes) {
        return nullptr;
    }

    quint64 totalSuccessfullySentNotebooks = 0UL;
    if (!readCounter(
            gTotalSuccessfullySentNotebooksKey, json,
            totalSuccessfullySentNotebooks))
    {
        return nullptr;
    }

    auto failedToSendNotebooks =
        deserializeItemsWithExceptions<ISendStatus::NotebookWithException>(
            gFailedToSendNotebooksKey, gNotebookKey, json);
    if (!failedToSendNotebooks) {
        return nullptr;
    }

    quint64 totalSuccessfullySentSavedSearches = 0UL;
    if (!readCounter(
            gTotalSuccessfullySentSavedSearchesKey, json,
            totalSuccessfullySentSavedSearches))
    {
        return nullptr;
    }

    auto failedToSendSavedSearches =
        deserializeItemsWithExceptions<ISendStatus::SavedSearchWithException>(
            gFailedToSendSavedSearchesKey, gSavedSearchKey, json);
    if (!failedToSendSavedSearches) {
        return nullptr;
    }

    quint64 totalSuccessfullySentTags = 0UL;
    if (!readCounter(
            gTotalSuccessfullySentTagsKey, json, totalSuccessfullySentTags))
    {
        return nullptr;
    }

    auto failedToSendTags =
        deserializeItemsWithExceptions<ISendStatus::TagWithException>(
            gFailedToSendTagsKey, gTagKey, json);
    if (!failedToSendTags) {
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

    const auto needToRepeatIncrementalSyncIt =
        json.constFind(toStr(gNeedToRepeatIncrementalSync));
    if (needToRepeatIncrementalSyncIt == json.constEnd() ||
        !needToRepeatIncrementalSyncIt->isBool())
    {
        return nullptr;
    }

    auto sendStatus = std::make_shared<SendStatus>();
    sendStatus->m_totalAttemptedToSendNotes = totalAttemptedToSendNotes;
    sendStatus->m_totalAttemptedToSendNotebooks = totalAttemptedToSendNotebooks;
    sendStatus->m_totalAttemptedToSendSavedSearches =
        totalAttemptedToSendSavedSearches;
    sendStatus->m_totalAttemptedToSendTags = totalAttemptedToSendTags;

    sendStatus->m_totalSuccessfullySentNotes = totalSuccessfullySentNotes;
    sendStatus->m_failedToSendNotes = std::move(*failedToSendNotes);

    sendStatus->m_totalSuccessfullySentNotebooks =
        totalSuccessfullySentNotebooks;
    sendStatus->m_failedToSendNotebooks = std::move(*failedToSendNotebooks);

    sendStatus->m_totalSuccessfullySentSavedSearches =
        totalSuccessfullySentSavedSearches;
    sendStatus->m_failedToSendSavedSearches =
        std::move(*failedToSendSavedSearches);

    sendStatus->m_totalSuccessfullySentTags = totalSuccessfullySentTags;
    sendStatus->m_failedToSendTags = std::move(*failedToSendTags);

    sendStatus->m_stopSynchronizationError = stopSynchronizationError;
    sendStatus->m_needToRepeatIncrementalSync =
        needToRepeatIncrementalSyncIt->toBool();

    return sendStatus;
}

} // namespace quentier::synchronization
