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

#include "Utils.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>

#include <qevercloud/serialization/json/Note.h>
#include <qevercloud/utility/ToRange.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>

#include <set>

namespace quentier::synchronization::utils {

namespace {

////////////////////////////////////////////////////////////////////////////////

const QString gProcessedNotesIniFileName = QStringLiteral("processedNotes.ini");
const QString gCancelledNotesDirName = QStringLiteral("cancelledNotes");

const QString gFailedToDownloadNotesDirName =
    QStringLiteral("failedToDownloadNotes");

const QString gFailedToProcessNotesDirName =
    QStringLiteral("failedToProcessNotes");

const QString gExpungeNotesIniFileName = QStringLiteral("expungedNotes.ini");

const QString gFailedToExpungeNotesIniFileName =
    QStringLiteral("failedToExpungeNotes.ini");

////////////////////////////////////////////////////////////////////////////////

void writeNote(const qevercloud::Note & note, const QDir & dir)
{
    if (Q_UNLIKELY(!note.guid())) {
        QNWARNING(
            "synchronization::utils",
            "Cannot write note to file: note has no guid: " << note);
        return;
    }

    if (Q_UNLIKELY(!dir.exists())) {
        if (!dir.mkpath(dir.absolutePath())) {
            QNWARNING(
                "synchronization::utils",
                "Cannot write note to file: failed to create dir for note: "
                    << dir.absolutePath());
            return;
        }
    }

    QFile file{
        dir.absoluteFilePath(QString::fromUtf8("%1.json").arg(*note.guid()))};
    if (Q_UNLIKELY(!file.open(QIODevice::WriteOnly))) {
        QNWARNING(
            "synchronization::utils",
            "Cannot write note to file: failed to open file for writing: "
                << dir.absoluteFilePath(*note.guid() + QStringLiteral(".json"))
                << " (" << file.errorString() << ")");
        return;
    }

    const QJsonObject obj = qevercloud::serializeToJson(note);
    QJsonDocument doc;
    doc.setObject(obj);

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

[[nodiscard]] QList<qevercloud::Note> readNotes(const QDir & dir)
{
    if (!dir.exists()) {
        return {};
    }

    const auto fileNames = dir.entryList(QDir::NoDotAndDotDot | QDir::Files);

    QList<qevercloud::Note> result;
    result.reserve(fileNames.size());

    for (const auto & fileName: qAsConst(fileNames)) {
        QFile file{dir.absoluteFilePath(fileName)};
        if (Q_UNLIKELY(!file.open(QIODevice::ReadOnly))) {
            QNWARNING(
                "synchronization::utils",
                "Failed to open file with note for reading: "
                    << dir.absoluteFilePath(fileName));
            continue;
        }

        const QByteArray contents = file.readAll();
        file.close();

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(contents, &error);
        if (Q_UNLIKELY(doc.isNull())) {
            QNWARNING(
                "synchronization::utils",
                "Failed to parse serialized note from file to json document: "
                    << error.errorString()
                    << "; file: " << dir.absoluteFilePath(fileName));
            continue;
        }

        if (Q_UNLIKELY(!doc.isObject())) {
            QNWARNING(
                "synchronization::utils",
                "Cannot parse serialized note: json is not an object; file: "
                    << dir.absoluteFilePath(fileName));
            continue;
        }

        const QJsonObject obj = doc.object();
        qevercloud::Note note;
        if (Q_UNLIKELY(!qevercloud::deserializeFromJson(obj, note))) {
            QNWARNING(
                "synchronization::utils",
                "Failed to deserialized note from json; file: "
                    << dir.absoluteFilePath(fileName));
            continue;
        }

        result << note;
    }

    return result;
}

} // namespace

INotesProcessor::DownloadNotesStatus mergeDownloadNotesStatuses(
    INotesProcessor::DownloadNotesStatus lhs,
    const INotesProcessor::DownloadNotesStatus & rhs)
{
    using DownloadNotesStatus = INotesProcessor::DownloadNotesStatus;

    lhs.totalNewNotes += rhs.totalNewNotes;
    lhs.totalUpdatedNotes += rhs.totalUpdatedNotes;
    lhs.totalExpungedNotes += rhs.totalExpungedNotes;

    const auto mergeNoteLists =
        [](QList<DownloadNotesStatus::NoteWithException> lhs,
           const QList<DownloadNotesStatus::NoteWithException> & rhs) {
            using Iter =
                QList<DownloadNotesStatus::NoteWithException>::const_iterator;
            QHash<qevercloud::Guid, Iter> rhsNotesByGuid;
            rhsNotesByGuid.reserve(rhs.size());
            for (auto it = rhs.constBegin(); it != rhs.constEnd(); ++it) {
                const auto & noteWithException = *it;
                const auto & note = noteWithException.note;
                if (Q_UNLIKELY(!note.guid())) {
                    continue;
                }
                rhsNotesByGuid[*note.guid()] = it;
            }

            std::set<Iter> replacedIters;
            for (auto it = lhs.begin(); it != lhs.end();) {
                if (Q_UNLIKELY(!it->note.guid())) {
                    it = lhs.erase(it);
                    continue;
                }

                const auto rit = rhsNotesByGuid.find(*it->note.guid());
                if (rit != rhsNotesByGuid.end()) {
                    *it = *(*rit);
                    replacedIters.insert(*rit);
                }

                ++it;
            }

            for (auto it = rhs.constBegin(); it != rhs.constEnd(); ++it) {
                if (replacedIters.find(it) != replacedIters.end()) {
                    continue;
                }

                lhs << *it;
            }

            return lhs;
        };

    lhs.notesWhichFailedToDownload = mergeNoteLists(
        lhs.notesWhichFailedToDownload, rhs.notesWhichFailedToDownload);

    lhs.notesWhichFailedToProcess = mergeNoteLists(
        lhs.notesWhichFailedToProcess, rhs.notesWhichFailedToProcess);

    lhs.noteGuidsWhichFailedToExpunge << rhs.noteGuidsWhichFailedToExpunge;
    lhs.noteGuidsWhichFailedToExpunge.erase(
        std::unique(
            lhs.noteGuidsWhichFailedToExpunge.begin(),
            lhs.noteGuidsWhichFailedToExpunge.end(),
            [](const auto & lhs, const auto & rhs) {
                return lhs.guid == rhs.guid;
            }),
        lhs.noteGuidsWhichFailedToExpunge.end());

    for (const auto it: qevercloud::toRange(rhs.processedNoteGuidsAndUsns)) {
        lhs.processedNoteGuidsAndUsns[it.key()] = it.value();
    }

    for (const auto it: qevercloud::toRange(rhs.cancelledNoteGuidsAndUsns)) {
        lhs.cancelledNoteGuidsAndUsns[it.key()] = it.value();
    }

    lhs.expungedNoteGuids << rhs.expungedNoteGuids;
    lhs.expungedNoteGuids.erase(
        std::unique(lhs.expungedNoteGuids.begin(), lhs.expungedNoteGuids.end()),
        lhs.expungedNoteGuids.end());

    return lhs;
}

////////////////////////////////////////////////////////////////////////////////

void writeProcessedNoteInfo(
    const qevercloud::Guid & noteGuid, qint32 updateSequenceNum,
    const QDir & lastSyncNotesDir)
{
    if (Q_UNLIKELY(!lastSyncNotesDir.exists())) {
        if (!lastSyncNotesDir.mkpath(lastSyncNotesDir.absolutePath())) {
            QNWARNING(
                "synchronization::utils",
                "Failed to create dir for last sync notes data persistence");
            return;
        }
    }

    // First, write the info into a file containing the list of guids and USNs
    // of processed notes
    QSettings processedNotesSettings{
        lastSyncNotesDir.absoluteFilePath(gProcessedNotesIniFileName),
        QSettings::IniFormat};

    processedNotesSettings.setValue(noteGuid, updateSequenceNum);
    processedNotesSettings.sync();

    // Now see whether there are files corresponding to this note guid
    // with notes which failed to download or process or was cancelled
    // during the previous sync
    const auto getNoteFileInfo = [&noteGuid](const QDir & dir) {
        return dir.absoluteFilePath(QString::fromUtf8("%1.json").arg(noteGuid));
    };

    // 1. Cancelled notes
    const QDir cancelledNotesDir{
        lastSyncNotesDir.absoluteFilePath(gCancelledNotesDirName)};

    const QFileInfo cancelledNoteFileInfo{getNoteFileInfo(cancelledNotesDir)};

    if (cancelledNoteFileInfo.exists() &&
        !removeFile(cancelledNoteFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "synchronization::utils",
            "Failed to remove file corresponding to note which sync was "
                << "cancelled: " << cancelledNoteFileInfo.absoluteFilePath());
    }

    // 2. Notes which failed to download
    const QDir failedToDownloadNotesDir{
        lastSyncNotesDir.absoluteFilePath(gFailedToDownloadNotesDirName)};

    const QFileInfo failedToDownloadNoteFileInfo{
        getNoteFileInfo(failedToDownloadNotesDir)};

    if (failedToDownloadNoteFileInfo.exists() &&
        !removeFile(failedToDownloadNoteFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "synchronization::utils",
            "Failed to remove file corresponding to note which failed to "
                << "download during the last sync: "
                << failedToDownloadNoteFileInfo.absoluteFilePath());
    }

    // 3. Notes which failed to process
    const QDir failedToProcessNotesDir{
        lastSyncNotesDir.absoluteFilePath(gFailedToProcessNotesDirName)};

    const QFileInfo failedToProcessNoteFileInfo{
        getNoteFileInfo(failedToProcessNotesDir)};

    if (failedToProcessNoteFileInfo.exists() &&
        !removeFile(failedToProcessNoteFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "synchronization::utils",
            "Failed to remove file corresponding to note which failed to "
                << "process during the last sync: "
                << failedToProcessNoteFileInfo.absoluteFilePath());
    }

    // 4. Also ensure that the note is not in the list of those which failed
    //    to expunge during the last sync
    QSettings notesWhichFailedToExpunge{
        lastSyncNotesDir.absoluteFilePath(gFailedToExpungeNotesIniFileName),
        QSettings::IniFormat};

    notesWhichFailedToExpunge.remove(noteGuid);
    notesWhichFailedToExpunge.sync();
}

void writeFailedToDownloadNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir)
{
    writeNote(
        note,
        QDir{lastSyncNotesDir.absoluteFilePath(gFailedToDownloadNotesDirName)});
}

void writeFailedToProcessNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir)
{
    writeNote(
        note,
        QDir{lastSyncNotesDir.absoluteFilePath(gFailedToProcessNotesDirName)});
}

void writeCancelledNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir)
{
    writeNote(
        note, QDir{lastSyncNotesDir.absoluteFilePath(gCancelledNotesDirName)});
}

void writeExpungedNote(
    const qevercloud::Guid & expungedNoteGuid, const QDir & lastSyncNotesDir)
{
    QSettings expungedNotes{
        lastSyncNotesDir.absoluteFilePath(gExpungeNotesIniFileName),
        QSettings::IniFormat};

    expungedNotes.setValue(expungedNoteGuid, {});
    expungedNotes.sync();
}

void writeFailedToExpungeNote(
    const qevercloud::Guid & noteGuid, const QDir & lastSyncNotesDir)
{
    QSettings failedToExpungeNotes{
        lastSyncNotesDir.absoluteFilePath(gFailedToExpungeNotesIniFileName),
        QSettings::IniFormat};

    failedToExpungeNotes.setValue(noteGuid, {});
    failedToExpungeNotes.sync();
}

QHash<qevercloud::Guid, qint32> processedNotesInfoFromLastSync(
    const QDir & lastSyncNotesDir)
{
    if (!lastSyncNotesDir.exists()) {
        return {};
    }

    QSettings processedNotesSettings{
        lastSyncNotesDir.absoluteFilePath(gProcessedNotesIniFileName),
        QSettings::IniFormat};

    const QStringList noteGuids = processedNotesSettings.allKeys();
    if (noteGuids.isEmpty()) {
        return {};
    }

    QHash<qevercloud::Guid, qint32> result;
    result.reserve(noteGuids.size());
    for (const auto & noteGuid: qAsConst(noteGuids)) {
        const auto value = processedNotesSettings.value(noteGuid);
        if (Q_UNLIKELY(!value.isValid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected corrupted processed note USN value for note guid "
                    << noteGuid);
            // Try to remove this key so that it doesn't interfere the next time
            processedNotesSettings.remove(noteGuid);
            continue;
        }

        bool conversionResult = false;
        qint32 usn = value.toInt(&conversionResult);
        if (Q_UNLIKELY(!conversionResult)) {
            QNWARNING(
                "synchronization::utils",
                "Detected non-integer processed note USN value for note guid "
                    << noteGuid);
            // Try to remove this key so that it doesn't interfere the next time
            processedNotesSettings.remove(noteGuid);
            continue;
        }

        result[noteGuid] = usn;
    }

    return result;
}

QList<qevercloud::Note> notesWhichFailedToDownloadDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    return readNotes(
        QDir{lastSyncNotesDir.absoluteFilePath(gFailedToDownloadNotesDirName)});
}

QList<qevercloud::Note> notesWhichFailedToProcessDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    return readNotes(
        QDir{lastSyncNotesDir.absoluteFilePath(gFailedToProcessNotesDirName)});
}

QList<qevercloud::Note> notesCancelledDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    return readNotes(
        QDir{lastSyncNotesDir.absoluteFilePath(gCancelledNotesDirName)});
}

QList<qevercloud::Guid> noteGuidsExpungedDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    QSettings expungedNotes{
        lastSyncNotesDir.absoluteFilePath(gExpungeNotesIniFileName),
        QSettings::IniFormat};

    // NOTE: implicitly converting QStringList to QList<qevercloud::Guid> here.
    // It works because QStringList inherits QList<QString> and qevercloud::Guid
    // is just a type alias for QString. It is ok to do such conversion because
    // QStringList is layout-compatible with QList<QString> and just adds
    // convenience methods.
    return expungedNotes.allKeys();
}

QList<qevercloud::Guid> noteGuidsWhichFailedToExpungeDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    QSettings notesWhichFailedToExpunge{
        lastSyncNotesDir.absoluteFilePath(gFailedToExpungeNotesIniFileName),
        QSettings::IniFormat};

    // NOTE: implicitly converting QStringList to QList<qevercloud::Guid> here.
    // It works because QStringList inherits QList<QString> and qevercloud::Guid
    // is just a type alias for QString. It is ok to do such conversion because
    // QStringList is layout-compatible with QList<QString> and just adds
    // convenience methods.
    return notesWhichFailedToExpunge.allKeys();
}

} // namespace quentier::synchronization::utils
