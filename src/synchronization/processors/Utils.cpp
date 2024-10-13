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

#include "Utils.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>

#include <qevercloud/serialization/json/Note.h>
#include <qevercloud/serialization/json/Resource.h>
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

const char * gProcessedNotesIniFileName = "processedNotes.ini";
const char * gCancelledNotesDirName = "cancelledNotes";
const char * gFailedToDownloadNotesDirName = "failedToDownloadNotes";
const char * gFailedToProcessNotesDirName = "failedToProcessNotes";
const char * gExpungeNotesIniFileName = "expungedNotes.ini";
const char * gFailedToExpungeNotesIniFileName = "failedToExpungeNotes.ini";

////////////////////////////////////////////////////////////////////////////////

const char * gProcessedResourcesIniFileName = "processedResources.ini";
const char * gCancelledResourcesDirName = "cancelledResources";
const char * gFailedToDownloadResourcesDirName = "failedToDownloadResources";
const char * gFailedToProcessResourcesDirName = "failedToProcessResources";

////////////////////////////////////////////////////////////////////////////////

template <class T>
void writeItem(const T & item, const QString & itemTypeName, const QDir & dir)
{
    if (Q_UNLIKELY(!item.guid())) {
        QNWARNING(
            "synchronization::utils",
            "Cannot write " << itemTypeName << " to file: " << itemTypeName
                            << " has no guid: " << item);
        return;
    }

    if (Q_UNLIKELY(!dir.exists())) {
        if (!dir.mkpath(dir.absolutePath())) {
            QNWARNING(
                "synchronization::utils",
                "Cannot write " << itemTypeName << " to file: failed to create "
                                << " dir for " << itemTypeName << ": "
                                << dir.absolutePath());
            return;
        }
    }

    QFile file{
        dir.absoluteFilePath(QString::fromUtf8("%1.json").arg(*item.guid()))};
    if (Q_UNLIKELY(!file.open(QIODevice::WriteOnly))) {
        QNWARNING(
            "synchronization::utils",
            "Cannot write "
                << itemTypeName << " to file: failed to open file "
                << "for writing: "
                << dir.absoluteFilePath(*item.guid() + QStringLiteral(".json"))
                << " (" << file.errorString() << ")");
        return;
    }

    const QJsonObject obj = qevercloud::serializeToJson(item);
    QJsonDocument doc;
    doc.setObject(obj);

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

template <class T>
[[nodiscard]] QList<T> readItems(const QString & itemTypeName, const QDir & dir)
{
    if (!dir.exists()) {
        return {};
    }

    const auto fileNames = dir.entryList(QDir::NoDotAndDotDot | QDir::Files);

    QList<T> result;
    result.reserve(fileNames.size());

    for (const auto & fileName: std::as_const(fileNames)) {
        QFile file{dir.absoluteFilePath(fileName)};
        if (Q_UNLIKELY(!file.open(QIODevice::ReadOnly))) {
            QNWARNING(
                "synchronization::utils",
                "Failed to open file with " << itemTypeName << " for reading: "
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
                "Failed to parse serialized "
                    << itemTypeName
                    << " from file to json document: " << error.errorString()
                    << "; file: " << dir.absoluteFilePath(fileName));
            continue;
        }

        if (Q_UNLIKELY(!doc.isObject())) {
            QNWARNING(
                "synchronization::utils",
                "Cannot parse serialized " << itemTypeName
                                           << ": json is not an object; file: "
                                           << dir.absoluteFilePath(fileName));
            continue;
        }

        const QJsonObject obj = doc.object();
        T item;
        if (Q_UNLIKELY(!qevercloud::deserializeFromJson(obj, item))) {
            QNWARNING(
                "synchronization::utils",
                "Failed to deserialized "
                    << itemTypeName
                    << " from json; file: " << dir.absoluteFilePath(fileName));
            continue;
        }

        result << item;
    }

    return result;
}

void writeNote(const qevercloud::Note & note, const QDir & dir)
{
    writeItem<qevercloud::Note>(note, QStringLiteral("note"), dir);
}

[[nodiscard]] QList<qevercloud::Note> readNotes(const QDir & dir)
{
    return readItems<qevercloud::Note>(QStringLiteral("note"), dir);
}

void writeResource(const qevercloud::Resource & resource, const QDir & dir)
{
    writeItem<qevercloud::Resource>(resource, QStringLiteral("resource"), dir);
}

[[nodiscard]] QList<qevercloud::Resource> readResources(const QDir & dir)
{
    return readItems<qevercloud::Resource>(QStringLiteral("resource"), dir);
}

[[nodiscard]] QHash<qevercloud::Guid, qint32> processedItemsInfoFromLastSync(
    const QDir & dir, const QString & itemTypeName, // NOLINT
    const QString & processedItemsIniFileName)
{
    if (!dir.exists()) {
        return {};
    }

    QSettings processedItemsSettings{
        dir.absoluteFilePath(processedItemsIniFileName), QSettings::IniFormat};

    const QStringList guids = processedItemsSettings.allKeys();
    if (guids.isEmpty()) {
        return {};
    }

    QHash<qevercloud::Guid, qint32> result;
    result.reserve(guids.size());
    for (const auto & guid: std::as_const(guids)) {
        const auto value = processedItemsSettings.value(guid);
        if (Q_UNLIKELY(!value.isValid())) {
            QNWARNING(
                "synchronization::utils",
                "Detected corrupted processed "
                    << itemTypeName << " USN value for " << itemTypeName
                    << " guid " << guid);
            // Try to remove this key so that it doesn't interfere the next time
            processedItemsSettings.remove(guid);
            continue;
        }

        bool conversionResult = false;
        qint32 usn = value.toInt(&conversionResult);
        if (Q_UNLIKELY(!conversionResult)) {
            QNWARNING(
                "synchronization::utils",
                "Detected non-integer processed "
                    << itemTypeName << " USN value for " << itemTypeName
                    << " guid " << guid);
            // Try to remove this key so that it doesn't interfere the next time
            processedItemsSettings.remove(guid);
            continue;
        }

        result[guid] = usn;
    }

    return result;
}

} // namespace

DownloadNotesStatus mergeDownloadNotesStatuses(
    DownloadNotesStatus lhs, const DownloadNotesStatus & rhs)
{
    lhs.m_totalNewNotes += rhs.m_totalNewNotes;
    lhs.m_totalUpdatedNotes += rhs.m_totalUpdatedNotes;
    lhs.m_totalExpungedNotes += rhs.m_totalExpungedNotes;

    const auto mergeNoteLists =
        [](QList<DownloadNotesStatus::NoteWithException> lhs,
           const QList<DownloadNotesStatus::NoteWithException> & rhs) {
            using Iter =
                QList<DownloadNotesStatus::NoteWithException>::const_iterator;
            QHash<qevercloud::Guid, Iter> rhsNotesByGuid;
            rhsNotesByGuid.reserve(rhs.size());
            for (auto it = rhs.constBegin(); it != rhs.constEnd(); ++it) {
                const auto & noteWithException = *it;
                const auto & note = noteWithException.first;
                if (Q_UNLIKELY(!note.guid())) {
                    continue;
                }
                rhsNotesByGuid[*note.guid()] = it;
            }

            std::set<Iter> replacedIters;
            for (auto it = lhs.begin(); it != lhs.end();) {
                if (Q_UNLIKELY(!it->first.guid())) {
                    it = lhs.erase(it);
                    continue;
                }

                const auto rit = rhsNotesByGuid.find(*it->first.guid());
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

    lhs.m_notesWhichFailedToDownload = mergeNoteLists(
        lhs.m_notesWhichFailedToDownload, rhs.m_notesWhichFailedToDownload);

    lhs.m_notesWhichFailedToProcess = mergeNoteLists(
        lhs.m_notesWhichFailedToProcess, rhs.m_notesWhichFailedToProcess);

    lhs.m_noteGuidsWhichFailedToExpunge << rhs.m_noteGuidsWhichFailedToExpunge;
    lhs.m_noteGuidsWhichFailedToExpunge.erase(
        std::unique(
            lhs.m_noteGuidsWhichFailedToExpunge.begin(),
            lhs.m_noteGuidsWhichFailedToExpunge.end(),
            [](const auto & lhs, const auto & rhs) {
                return lhs.first == rhs.first;
            }),
        lhs.m_noteGuidsWhichFailedToExpunge.end());

    for (const auto it: qevercloud::toRange(rhs.m_processedNoteGuidsAndUsns)) {
        lhs.m_processedNoteGuidsAndUsns[it.key()] = it.value();
    }

    for (const auto it: qevercloud::toRange(rhs.m_cancelledNoteGuidsAndUsns)) {
        lhs.m_cancelledNoteGuidsAndUsns[it.key()] = it.value();
    }

    lhs.m_expungedNoteGuids << rhs.m_expungedNoteGuids;
    lhs.m_expungedNoteGuids.erase(
        std::unique(
            lhs.m_expungedNoteGuids.begin(), lhs.m_expungedNoteGuids.end()),
        lhs.m_expungedNoteGuids.end());

    return lhs;
}

DownloadResourcesStatus mergeDownloadResourcesStatuses(
    DownloadResourcesStatus lhs, const DownloadResourcesStatus & rhs)
{
    lhs.m_totalNewResources += rhs.m_totalNewResources;
    lhs.m_totalUpdatedResources += rhs.m_totalUpdatedResources;

    const auto mergeResourceLists =
        [](QList<DownloadResourcesStatus::ResourceWithException> lhs,
           const QList<DownloadResourcesStatus::ResourceWithException> & rhs) {
            using Iter = QList<
                DownloadResourcesStatus::ResourceWithException>::const_iterator;
            QHash<qevercloud::Guid, Iter> rhsResourcesByGuid;
            rhsResourcesByGuid.reserve(rhs.size());
            for (auto it = rhs.constBegin(); it != rhs.constEnd(); ++it) {
                const auto & resourceWithException = *it;
                const auto & resource = resourceWithException.first;
                if (Q_UNLIKELY(!resource.guid())) {
                    continue;
                }
                rhsResourcesByGuid[*resource.guid()] = it;
            }

            std::set<Iter> replacedIters;
            for (auto it = lhs.begin(); it != lhs.end();) {
                if (Q_UNLIKELY(!it->first.guid())) {
                    it = lhs.erase(it);
                    continue;
                }

                const auto rit = rhsResourcesByGuid.find(*it->first.guid());
                if (rit != rhsResourcesByGuid.end()) {
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

    lhs.m_resourcesWhichFailedToDownload = mergeResourceLists(
        lhs.m_resourcesWhichFailedToDownload,
        rhs.m_resourcesWhichFailedToDownload);

    lhs.m_resourcesWhichFailedToProcess = mergeResourceLists(
        lhs.m_resourcesWhichFailedToProcess,
        rhs.m_resourcesWhichFailedToProcess);

    for (const auto it:
         qevercloud::toRange(rhs.m_processedResourceGuidsAndUsns)) {
        lhs.m_processedResourceGuidsAndUsns[it.key()] = it.value();
    }

    for (const auto it:
         qevercloud::toRange(rhs.m_cancelledResourceGuidsAndUsns)) {
        lhs.m_cancelledResourceGuidsAndUsns[it.key()] = it.value();
    }

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
                "Failed to create dir for last sync notes persistence");
            return;
        }
    }

    // First, write the info into a file containing the list of guids and USNs
    // of processed notes
    QSettings processedNotesSettings{
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gProcessedNotesIniFileName)),
        QSettings::IniFormat};

    processedNotesSettings.setValue(noteGuid, updateSequenceNum);
    processedNotesSettings.sync();

    // Now see whether there are files corresponding to this note guid
    // with notes which failed to download or process or were cancelled
    // during the previous sync
    const auto getNoteFileInfo = [&noteGuid](const QDir & dir) {
        return dir.absoluteFilePath(QString::fromUtf8("%1.json").arg(noteGuid));
    };

    // 1. Cancelled notes
    const QDir cancelledNotesDir{
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gCancelledNotesDirName))};

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
        QString::fromUtf8(gFailedToDownloadNotesDirName))};

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
        QString::fromUtf8(gFailedToProcessNotesDirName))};

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
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToExpungeNotesIniFileName)),
        QSettings::IniFormat};

    notesWhichFailedToExpunge.remove(noteGuid);
    notesWhichFailedToExpunge.sync();
}

void clearProcessedNotesInfos(const QDir & lastSyncNotesDir)
{
    QNDEBUG(
        "synchronization::utils",
        "clearProcessedNotesInfos: " << lastSyncNotesDir.absolutePath());

    QSettings processedNotesSettings{
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gProcessedNotesIniFileName)),
        QSettings::IniFormat};

    processedNotesSettings.clear();
    processedNotesSettings.sync();
}

void writeFailedToDownloadNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir)
{
    writeNote(
        note,
        QDir{lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToDownloadNotesDirName))});
}

void writeFailedToProcessNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir)
{
    writeNote(
        note,
        QDir{lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToProcessNotesDirName))});
}

void writeCancelledNote(
    const qevercloud::Note & note, const QDir & lastSyncNotesDir)
{
    writeNote(
        note,
        QDir{lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gCancelledNotesDirName))});
}

void writeExpungedNote(
    const qevercloud::Guid & expungedNoteGuid, const QDir & lastSyncNotesDir)
{
    QSettings expungedNotes{
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gExpungeNotesIniFileName)),
        QSettings::IniFormat};

    expungedNotes.setValue(expungedNoteGuid, {});
    expungedNotes.sync();
}

void writeFailedToExpungeNote(
    const qevercloud::Guid & noteGuid, const QDir & lastSyncNotesDir)
{
    QSettings failedToExpungeNotes{
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToExpungeNotesIniFileName)),
        QSettings::IniFormat};

    failedToExpungeNotes.setValue(noteGuid, {});
    failedToExpungeNotes.sync();
}

void writeProcessedResourceInfo(
    const qevercloud::Guid & resourceGuid, qint32 updateSequenceNum,
    const QDir & lastSyncResourcesDir)
{
    if (Q_UNLIKELY(!lastSyncResourcesDir.exists())) {
        if (!lastSyncResourcesDir.mkpath(lastSyncResourcesDir.absolutePath())) {
            QNWARNING(
                "synchronization::utils",
                "Failed to create dir for last sync resources persistence");
            return;
        }
    }

    // First, write the info into a file containing the list of guids and USNs
    // of processed resources
    QSettings processedResourcesSettings{
        lastSyncResourcesDir.absoluteFilePath(
            QString::fromUtf8(gProcessedResourcesIniFileName)),
        QSettings::IniFormat};

    processedResourcesSettings.setValue(resourceGuid, updateSequenceNum);
    processedResourcesSettings.sync();

    // Now see whether there are files corresponding to this resource guif
    // with resources which failed to download or process or were cancelled
    // during the previous sync
    const auto getResourceFileInfo = [&resourceGuid](const QDir & dir) {
        return dir.absoluteFilePath(
            QString::fromUtf8("%1.json").arg(resourceGuid));
    };

    // 1. Cancelled resources
    const QDir cancelledResourcesDir{lastSyncResourcesDir.absoluteFilePath(
        QString::fromUtf8(gCancelledResourcesDirName))};

    const QFileInfo cancelledResourceFileInfo{
        getResourceFileInfo(cancelledResourcesDir)};

    if (cancelledResourceFileInfo.exists() &&
        !removeFile(cancelledResourceFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "synchronization::utils",
            "Failed to remove file corresponding to resource which sync was "
                << "cancelled: "
                << cancelledResourceFileInfo.absoluteFilePath());
    }

    // 2. Resources which failed to download
    const QDir failedToDownloadResourcesDir{
        lastSyncResourcesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToDownloadResourcesDirName))};

    const QFileInfo failedToDownloadResourceFileInfo{
        getResourceFileInfo(failedToDownloadResourcesDir)};

    if (failedToDownloadResourceFileInfo.exists() &&
        !removeFile(failedToDownloadResourceFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "synchronization::utils",
            "Failed to remove file corresponding to resource which failed to "
                << "download during the last sync: "
                << failedToDownloadResourceFileInfo.absoluteFilePath());
    }

    // 3. Resources which failed to process
    const QDir failedToProcessResourcesDir{
        lastSyncResourcesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToProcessResourcesDirName))};

    const QFileInfo failedToProcessResourceFileInfo{
        getResourceFileInfo(failedToProcessResourcesDir)};

    if (failedToProcessResourceFileInfo.exists() &&
        !removeFile(failedToProcessResourceFileInfo.absoluteFilePath()))
    {
        QNWARNING(
            "synchronization::utils",
            "Failed to remove file corresponding to resource which failed to "
                << "process during the last sync: "
                << failedToProcessResourceFileInfo.absoluteFilePath());
    }
}

void clearProcessedResourcesInfos(const QDir & lastSyncResourcesDir)
{
    QNDEBUG(
        "synchronization::utils",
        "clearProcessedResourcesInfos: "
            << lastSyncResourcesDir.absolutePath());

    QSettings processedResourcesSettings{
        lastSyncResourcesDir.absoluteFilePath(
            QString::fromUtf8(gProcessedResourcesIniFileName)),
        QSettings::IniFormat};

    processedResourcesSettings.clear();
    processedResourcesSettings.sync();
}

// Persists information about resource which data failed to get downloaded
// inside the passed in dir
void writeFailedToDownloadResource(
    const qevercloud::Resource & resource, const QDir & lastSyncResourcesDir)
{
    writeResource(
        resource,
        QDir{lastSyncResourcesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToDownloadResourcesDirName))});
}

void writeFailedToProcessResource(
    const qevercloud::Resource & resource, const QDir & lastSyncResourcesDir)
{
    writeResource(
        resource,
        QDir{lastSyncResourcesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToProcessResourcesDirName))});
}

void writeCancelledResource(
    const qevercloud::Resource & resource, const QDir & lastSyncResourcesDir)
{
    writeResource(
        resource,
        QDir{lastSyncResourcesDir.absoluteFilePath(
            QString::fromUtf8(gCancelledResourcesDirName))});
}

////////////////////////////////////////////////////////////////////////////////

QHash<qevercloud::Guid, qint32> processedNotesInfoFromLastSync(
    const QDir & lastSyncNotesDir)
{
    return processedItemsInfoFromLastSync(
        lastSyncNotesDir, QStringLiteral("note"),
        QString::fromUtf8(gProcessedNotesIniFileName));
}

QList<qevercloud::Note> notesWhichFailedToDownloadDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    return readNotes(QDir{lastSyncNotesDir.absoluteFilePath(
        QString::fromUtf8(gFailedToDownloadNotesDirName))});
}

QList<qevercloud::Note> notesWhichFailedToProcessDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    return readNotes(QDir{lastSyncNotesDir.absoluteFilePath(
        QString::fromUtf8(gFailedToProcessNotesDirName))});
}

QList<qevercloud::Note> notesCancelledDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    return readNotes(QDir{lastSyncNotesDir.absoluteFilePath(
        QString::fromUtf8(gCancelledNotesDirName))});
}

QList<qevercloud::Guid> noteGuidsExpungedDuringLastSync(
    const QDir & lastSyncNotesDir)
{
    QSettings expungedNotes{
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gExpungeNotesIniFileName)),
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
        lastSyncNotesDir.absoluteFilePath(
            QString::fromUtf8(gFailedToExpungeNotesIniFileName)),
        QSettings::IniFormat};

    // NOTE: implicitly converting QStringList to QList<qevercloud::Guid> here.
    // It works because QStringList inherits QList<QString> and qevercloud::Guid
    // is just a type alias for QString. It is ok to do such conversion because
    // QStringList is layout-compatible with QList<QString> and just adds
    // convenience methods.
    return notesWhichFailedToExpunge.allKeys();
}

QHash<qevercloud::Guid, qint32> processedResourcesInfoFromLastSync(
    const QDir & lastSyncResourcesDir)
{
    return processedItemsInfoFromLastSync(
        lastSyncResourcesDir, QStringLiteral("resource"),
        QString::fromUtf8(gProcessedResourcesIniFileName));
}

QList<qevercloud::Resource> resourcesWhichFailedToDownloadDuringLastSync(
    const QDir & lastSyncResourcesDir)
{
    return readResources(QDir{lastSyncResourcesDir.absoluteFilePath(
        QString::fromUtf8(gFailedToDownloadResourcesDirName))});
}

QList<qevercloud::Resource> resourcesWhichFailedToProcessDuringLastSync(
    const QDir & lastSyncResourcesDir)
{
    return readResources(QDir{lastSyncResourcesDir.absoluteFilePath(
        QString::fromUtf8(gFailedToProcessResourcesDirName))});
}

QList<qevercloud::Resource> resourcesCancelledDuringLastSync(
    const QDir & lastSyncResourcesDir)
{
    return readResources(QDir{lastSyncResourcesDir.absoluteFilePath(
        QString::fromUtf8(gCancelledResourcesDirName))});
}

} // namespace quentier::synchronization::utils
