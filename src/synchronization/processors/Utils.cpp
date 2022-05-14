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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>

namespace quentier::synchronization::utils {

namespace {

////////////////////////////////////////////////////////////////////////////////

const QString gProcessedNotesIniFileName = QStringLiteral("processedNotes.ini");
const QString gCancelledNotesDirName = QStringLiteral("cancelledNotes");

const QString gFailedToDownloadNotesDirName =
    QStringLiteral("failedToDownloadNotes");

const QString gFailedToProcessNotesDirName =
    QStringLiteral("failedToProcessNotes");

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

    QFile file{QString::fromUtf8("%1.json").arg(*note.guid())};
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

} // namespace

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
    const QDir failedToDownloadNotesDir{lastSyncNotesDir.absoluteFilePath(
        gFailedToDownloadNotesDirName)};

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
    const QDir failedToProcessNotesDir{lastSyncNotesDir.absoluteFilePath(
        gFailedToProcessNotesDirName)};

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
        note,
        QDir{lastSyncNotesDir.absoluteFilePath(gCancelledNotesDirName)});
}

void writeExpungedNote(
    const qevercloud::Guid & expungedNoteGuid, const QDir & lastSyncNotesDir)
{
    QSettings notesWhichFailedToExpunge{
        lastSyncNotesDir.absoluteFilePath(gFailedToExpungeNotesIniFileName),
        QSettings::IniFormat};

    notesWhichFailedToExpunge.setValue(expungedNoteGuid, {});
    notesWhichFailedToExpunge.sync();
}

} // namespace quentier::synchronization::utils
