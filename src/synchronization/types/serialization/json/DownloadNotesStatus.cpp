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

#include <quentier/synchronization/types/serialization/json/DownloadNotesStatus.h>

#include <synchronization/types/DownloadNotesStatus.h>

#include "../Utils.h"
#include "SerializationUtils.h"

#include <qevercloud/serialization/json/Note.h>
#include <qevercloud/utility/ToRange.h>

#include <QJsonArray>

#include <string_view>
#include <utility>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gTotalNewNotesKey = "totalNewNotes"sv;
constexpr auto gTotalUpdatedNotesKey = "totalUpdatedNotes"sv;
constexpr auto gTotalExpungedNotesKey = "totalExpungedNotes"sv;
constexpr auto gNotesWhichFailedToDownloadKey = "notesWhichFailedToDownload"sv;
constexpr auto gNotesWhichFailedToProcessKey = "notesWhichFailedToProcess"sv;

constexpr auto gNoteKey = "note"sv;
constexpr auto gGuidKey = "guid"sv;
constexpr auto gExceptionKey = "exception"sv;
constexpr auto gUsnKey = "usn"sv;

constexpr auto gNoteGuidsWhichFailedToExpungeKey =
    "noteGuidsWhichFailedToExpunge"sv;

constexpr auto gProcessedNoteGuidsAndUsnsKey = "processedNoteGuidsAndUsns"sv;
constexpr auto gCancelledNoteGuidsAndUsnsKey = "cancelledNoteGuidsAndUsns"sv;
constexpr auto gExpungedNoteGuidsKey = "expungedNoteGuids"sv;

constexpr auto gStopSynchronizationErrorKey = "stopSynchronizationError"sv;

[[nodiscard]] QString toStr(const std::string_view key)
{
    return synchronization::toString(key);
}

} // namespace

QJsonObject serializeDownloadNotesStatusToJson(
    const IDownloadNotesStatus & status)
{
    QJsonObject object;

    object[toStr(gTotalNewNotesKey)] = QString::number(status.totalNewNotes());
    object[toStr(gTotalUpdatedNotesKey)] =
        QString::number(status.totalUpdatedNotes());
    object[toStr(gTotalExpungedNotesKey)] =
        QString::number(status.totalExpungedNotes());

    const auto serializeNotesWithExceptions =
        [&object](
            const QList<IDownloadNotesStatus::NoteWithException> &
                notesWithExceptions,
            const std::string_view key) {
            if (notesWithExceptions.isEmpty()) {
                return;
            }

            QJsonArray array;
            for (const auto & noteWithException:
                 std::as_const(notesWithExceptions)) {
                QJsonObject noteWithExceptionObject;
                noteWithExceptionObject[toStr(gNoteKey)] =
                    qevercloud::serializeToJson(noteWithException.first);
                Q_ASSERT(noteWithException.second);
                noteWithExceptionObject[toStr(gExceptionKey)] =
                    synchronization::serializeException(
                        *noteWithException.second);

                array << noteWithExceptionObject;
            }
            object[toStr(key)] = array;
        };

    const auto failedToDownloadNotes = status.notesWhichFailedToDownload();
    serializeNotesWithExceptions(
        failedToDownloadNotes, gNotesWhichFailedToDownloadKey);

    const auto failedToProcessNotes = status.notesWhichFailedToProcess();
    serializeNotesWithExceptions(
        failedToProcessNotes, gNotesWhichFailedToProcessKey);

    if (const auto failedToExpungeNoteGuids =
            status.noteGuidsWhichFailedToExpunge();
        !failedToExpungeNoteGuids.isEmpty())
    {
        QJsonArray array;
        for (const auto & guidWithException:
             std::as_const(failedToExpungeNoteGuids))
        {
            QJsonObject guidWithExceptionObject;
            guidWithExceptionObject[toStr(gGuidKey)] = guidWithException.first;
            Q_ASSERT(guidWithException.second);
            guidWithExceptionObject[toStr(gExceptionKey)] =
                synchronization::serializeException(*guidWithException.second);

            array << guidWithExceptionObject;
        }
        object[toStr(gNoteGuidsWhichFailedToExpungeKey)] = array;
    }

    const auto serializeUsnsByGuids =
        [&object](
            const IDownloadNotesStatus::UpdateSequenceNumbersByGuid &
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

    const auto processedNoteGuidsWithUsns = status.processedNoteGuidsAndUsns();
    serializeUsnsByGuids(
        processedNoteGuidsWithUsns, gProcessedNoteGuidsAndUsnsKey);

    const auto cancelledNoteGuidsWithUsns = status.cancelledNoteGuidsAndUsns();
    serializeUsnsByGuids(
        cancelledNoteGuidsWithUsns, gCancelledNoteGuidsAndUsnsKey);

    if (const auto noteGuids = status.expungedNoteGuids(); !noteGuids.isEmpty())
    {
        QJsonArray array;
        for (const auto & noteGuid: std::as_const(noteGuids)) {
            array << noteGuid;
        }
        object[toStr(gExpungedNoteGuidsKey)] = array;
    }

    const auto stopSynchronizationError = status.stopSynchronizationError();
    if (!std::holds_alternative<std::monostate>(stopSynchronizationError)) {
        object[toStr(gStopSynchronizationErrorKey)] =
            serializeStopSynchronizationError(stopSynchronizationError);
    }

    return object;
}

IDownloadNotesStatusPtr deserializeDownloadNotesStatusFromJson(
    const QJsonObject & json)
{
    const auto totalNewNotesIt = json.constFind(toStr(gTotalNewNotesKey));
    if (totalNewNotesIt == json.constEnd() || !totalNewNotesIt->isString()) {
        return nullptr;
    }

    quint64 totalNewNotes = 0;
    {
        bool totalNewNotesOk = false;
        totalNewNotes =
            totalNewNotesIt->toString().toULongLong(&totalNewNotesOk);
        if (!totalNewNotesOk) {
            return nullptr;
        }
    }

    const auto totalUpdatedNotesIt =
        json.constFind(toStr(gTotalUpdatedNotesKey));
    if (totalUpdatedNotesIt == json.constEnd() ||
        !totalUpdatedNotesIt->isString())
    {
        return nullptr;
    }

    quint64 totalUpdatedNotes = 0;
    {
        bool totalUpdatedNotesOk = false;
        totalUpdatedNotes =
            totalUpdatedNotesIt->toString().toULongLong(&totalUpdatedNotesOk);
        if (!totalUpdatedNotesOk) {
            return nullptr;
        }
    }

    const auto totalExpungedNotesIt =
        json.constFind(toStr(gTotalExpungedNotesKey));
    if (totalExpungedNotesIt == json.constEnd() ||
        !totalExpungedNotesIt->isString())
    {
        return nullptr;
    }

    quint64 totalExpungedNotes = 0;
    {
        bool totalExpungedNotesOk = false;
        totalExpungedNotes =
            totalExpungedNotesIt->toString().toULongLong(&totalExpungedNotesOk);
        if (!totalExpungedNotesOk) {
            return nullptr;
        }
    }

    const auto deserializeNotesWithExceptions =
        [&json](const std::string_view key)
        -> std::optional<QList<IDownloadNotesStatus::NoteWithException>> {
        QList<IDownloadNotesStatus::NoteWithException> notesWithExceptions;
        if (const auto it = json.constFind(toStr(key)); it != json.constEnd()) {
            if (!it->isArray()) {
                return std::nullopt;
            }

            const auto array = it->toArray();
            notesWithExceptions.reserve(array.size());
            for (auto ait = array.constBegin(); ait != array.constEnd(); ++ait)
            {
                if (!ait->isObject()) {
                    return std::nullopt;
                }

                const auto entry = ait->toObject();
                const auto noteIt = entry.constFind(toStr(gNoteKey));
                if (noteIt == entry.constEnd() || !noteIt->isObject()) {
                    return std::nullopt;
                }

                const auto exceptionIt = entry.constFind(toStr(gExceptionKey));
                if (exceptionIt == entry.constEnd() || !exceptionIt->isObject())
                {
                    return std::nullopt;
                }

                qevercloud::Note note;
                if (!qevercloud::deserializeFromJson(noteIt->toObject(), note))
                {
                    return std::nullopt;
                }

                auto exception = deserializeException(exceptionIt->toObject());
                if (!exception) {
                    return std::nullopt;
                }

                notesWithExceptions
                    << std::pair{std::move(note), std::move(exception)};
            }
        }

        return notesWithExceptions;
    };

    auto failedToDownloadNotes =
        deserializeNotesWithExceptions(gNotesWhichFailedToDownloadKey);
    if (!failedToDownloadNotes) {
        return nullptr;
    }

    auto failedToProcessNotes =
        deserializeNotesWithExceptions(gNotesWhichFailedToProcessKey);
    if (!failedToProcessNotes) {
        return nullptr;
    }

    QList<IDownloadNotesStatus::GuidWithException> failedToExpungeNoteGuids;
    if (const auto it =
            json.constFind(toStr(gNoteGuidsWhichFailedToExpungeKey));
        it != json.constEnd())
    {
        if (!it->isArray()) {
            return nullptr;
        }

        const auto array = it->toArray();
        failedToExpungeNoteGuids.reserve(array.size());
        for (auto ait = array.constBegin(); ait != array.constEnd(); ++ait) {
            if (!ait->isObject()) {
                return nullptr;
            }

            const auto entry = ait->toObject();
            const auto guidIt = entry.constFind(toStr(gGuidKey));
            if (guidIt == entry.constEnd() || !guidIt->isString()) {
                return nullptr;
            }

            const auto exceptionIt = entry.constFind(toStr(gExceptionKey));
            if (exceptionIt == entry.constEnd() || !exceptionIt->isObject()) {
                return nullptr;
            }

            auto exception = deserializeException(exceptionIt->toObject());
            if (!exception) {
                return nullptr;
            }

            failedToExpungeNoteGuids
                << std::pair{guidIt->toString(), std::move(exception)};
        }
    }

    const auto deserializeUsnsByGuids = [&json](const std::string_view key)
        -> std::optional<IDownloadNotesStatus::UpdateSequenceNumbersByGuid> {
        IDownloadNotesStatus::UpdateSequenceNumbersByGuid usns;
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

    auto processedNoteUsns =
        deserializeUsnsByGuids(gProcessedNoteGuidsAndUsnsKey);
    if (!processedNoteUsns) {
        return nullptr;
    }

    auto cancelledNoteUsns =
        deserializeUsnsByGuids(gCancelledNoteGuidsAndUsnsKey);
    if (!cancelledNoteUsns) {
        return nullptr;
    }

    QList<qevercloud::Guid> expungedNotes;
    if (const auto it = json.constFind(toStr(gExpungedNoteGuidsKey));
        it != json.constEnd())
    {
        if (!it->isArray()) {
            return nullptr;
        }

        const auto array = it->toArray();
        expungedNotes.reserve(array.size());
        for (auto ait = array.constBegin(); ait != array.constEnd(); ++ait) {
            if (!ait->isString()) {
                return nullptr;
            }

            expungedNotes << ait->toString();
        }
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

    auto status = std::make_shared<DownloadNotesStatus>();
    status->m_totalNewNotes = totalNewNotes;
    status->m_totalUpdatedNotes = totalUpdatedNotes;
    status->m_totalExpungedNotes = totalExpungedNotes;

    status->m_notesWhichFailedToDownload = std::move(*failedToDownloadNotes);
    status->m_notesWhichFailedToProcess = std::move(*failedToProcessNotes);
    status->m_noteGuidsWhichFailedToExpunge =
        std::move(failedToExpungeNoteGuids);

    status->m_processedNoteGuidsAndUsns = std::move(*processedNoteUsns);
    status->m_cancelledNoteGuidsAndUsns = std::move(*cancelledNoteUsns);
    status->m_expungedNoteGuids = std::move(expungedNotes);

    status->m_stopSynchronizationError = stopSynchronizationError;
    return status;
}

} // namespace quentier::synchronization
