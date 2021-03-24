/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include "LocalStorageManagerNoteSearchQueryTest.h"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/Tag.h>

#include <QCryptographicHash>

namespace quentier::test {

[[nodiscard]] bool checkQueryString(
    const QString & queryString, const QVector<qevercloud::Note> & notes,
    const QVector<bool> & expectedContainedNotesIndices,
    const LocalStorageManager & localStorageManager,
    ErrorString & errorDescription)
{
    NoteSearchQuery noteSearchQuery;
    bool res = noteSearchQuery.setQueryString(queryString, errorDescription);
    if (!res) {
        return false;
    }

    errorDescription.clear();

    const LocalStorageManager::GetNoteOptions options(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    auto foundNotes = localStorageManager.findNotesWithSearchQuery(
        noteSearchQuery, options, errorDescription);

    if (foundNotes.isEmpty()) {
        if (!expectedContainedNotesIndices.contains(true)) {
            return true;
        }

        if (errorDescription.isEmpty()) {
            errorDescription.setBase(
                "Internal error: no notes corresponding to note search query "
                "were found and the error description is empty as well; "
                "query string");

            errorDescription.details() = queryString;

            errorDescription.details() +=
                QStringLiteral("; \nNoteSearchQuery: ");

            errorDescription.details() += noteSearchQuery.toString();
        }

        return false;
    }

    int numFoundNotes = foundNotes.size();

    res = true;
    int numOriginalNotes = notes.size();
    for (int i = 0; i < numOriginalNotes; ++i) {
        res &=
            (foundNotes.contains(notes[i]) == expectedContainedNotesIndices[i]);
    }

    if (!res) {
        errorDescription.setBase(
            "Internal error: unexpected result "
            "of note search query processing");

        for (int i = 0; i < numOriginalNotes; ++i) {
            errorDescription.details() +=
                QStringLiteral("foundNotes.contains(notes[");

            errorDescription.details() += QString::number(i);
            errorDescription.details() += QStringLiteral("]) = ");

            errorDescription.details() +=
                (foundNotes.contains(notes[i]) ? QStringLiteral("true")
                                               : QStringLiteral("false"));

            errorDescription.details() += QStringLiteral("; expected: ");

            errorDescription.details() +=
                (expectedContainedNotesIndices[i] ? QStringLiteral("true")
                                                  : QStringLiteral("false"));

            errorDescription.details() += QStringLiteral("\n");
        }

        errorDescription.details() += QStringLiteral("Query string: ");
        errorDescription.details() += queryString;
        errorDescription.details() += QStringLiteral("; \nNoteSearchQuery: ");
        errorDescription.details() += noteSearchQuery.toString();

        for (int i = 0; i < numFoundNotes; ++i) {
            const auto & note = foundNotes[i];
            errorDescription.details() += QStringLiteral("foundNotes[");
            errorDescription.details() += QString::number(i);
            errorDescription.details() += QStringLiteral("]: ");
            errorDescription.details() += note.toString();
            errorDescription.details() += QStringLiteral("\n");
        }

        for (int i = 0; i < numOriginalNotes; ++i) {
            const auto & note = notes[i];
            errorDescription.details() += QStringLiteral("originalNotes[");
            errorDescription.details() += QString::number(i);
            errorDescription.details() += QStringLiteral("]: ");
            errorDescription.details() += note.toString();
            errorDescription.details() += QStringLiteral("\n");
        }

        return false;
    }

    return true;
}

bool localStorageManagerNoteSearchQueryTest(QString & errorDescription)
{
    // 1) =========== Create some notebooks ================

    int numNotebooks = 3;
    QVector<qevercloud::Notebook> notebooks;
    notebooks.reserve(numNotebooks);

    for (int i = 0; i < numNotebooks; ++i) {
        notebooks << qevercloud::Notebook();
        auto & notebook = notebooks.back();

        notebook.setName(
            QString(QStringLiteral("Test notebook #")) + QString::number(i));

        notebook.setUpdateSequenceNum(i);
        notebook.setDefaultNotebook(i == 0);
        notebook.mutableLocalData()[QStringLiteral("isLastUsed")] = (i == 1);
        notebook.setServiceCreated(i);
        notebook.setServiceUpdated(i + 1);
    }

    // 2) =============== Create some tags =================

    int numTags = 9;
    QVector<qevercloud::Tag> tags;
    tags.reserve(numTags);

    for (int i = 0; i < numTags; ++i) {
        tags << qevercloud::Tag();
        auto & tag = tags.back();
        tag.setUpdateSequenceNum(i);
    }

    tags[0].setName(QStringLiteral("College"));
    tags[1].setName(QStringLiteral("Server"));
    tags[2].setName(QStringLiteral("Binary"));
    tags[3].setName(QStringLiteral("Download"));
    tags[4].setName(QStringLiteral("Browser"));
    tags[5].setName(QStringLiteral("Tracker"));
    tags[6].setName(QStringLiteral("Application"));
    tags[7].setName(QString::fromUtf8("Footlocker αυΤΟκίΝΗτο"));
    tags[8].setName(QStringLiteral("Money"));

    tags[0].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813a"));
    tags[1].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813b"));
    tags[2].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813c"));
    tags[3].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813d"));
    tags[4].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813e"));
    tags[5].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813f"));
    tags[6].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813g"));
    tags[7].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813h"));
    tags[8].setGuid(QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813i"));

    // 3) ================= Create some resources ==================

    int numResources = 3;
    QVector<qevercloud::Resource> resources;
    resources.reserve(numResources);

    for (int i = 0; i < numResources; ++i) {
        resources << qevercloud::Resource();
        auto & resource = resources.back();

        resource.setUpdateSequenceNum(i);
    }

    auto & res0 = resources[0];
    res0.setMime(QStringLiteral("image/gif"));
    res0.setData(qevercloud::Data{});
    res0.mutableData()->setBody(QByteArray("fake image/gif byte array"));
    res0.mutableData()->setSize(res0.data()->body()->size());

    res0.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *res0.data()->body(),
            QCryptographicHash::Md5));

    QString recognitionBodyStr = QStringLiteral(
        "<recoIndex docType=\"handwritten\" objType=\"image\" "
        "objID=\"fc83e58282d8059be17debabb69be900\" "
        "engineVersion=\"5.5.22.7\" recoType=\"service\" "
        "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
        "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
        "<t w=\"87\">INFO ?</t>"
        "<t w=\"83\">INFORMATION</t>"
        "<t w=\"82\">LNFOPWATION</t>"
        "<t w=\"71\">LNFOPMATION</t>"
        "<t w=\"67\">LNFOPWATJOM</t>"
        "<t w=\"67\">LMFOPWAFJOM</t>"
        "<t w=\"62\">ΕΊΝΑΙ ένα</t>"
        "</item>"
        "<item x=\"1850\" y=\"1465\" w=\"14\" h=\"12\">"
        "<t w=\"11\">et</t>"
        "<t w=\"10\">TQ</t>"
        "</item>"
        "</recoIndex>");

    res0.setRecognition(qevercloud::Data{});
    res0.mutableRecognition()->setBody(recognitionBodyStr.toUtf8());
    res0.mutableRecognition()->setSize(res0.recognition()->body()->size());

    res0.mutableRecognition()->setBodyHash(
        QCryptographicHash::hash(
            *res0.recognition()->body(),
            QCryptographicHash::Md5));

    auto & res1 = resources[1];
    res1.setMime(QStringLiteral("audio/*"));
    res1.setData(qevercloud::Data{});
    res1.mutableData()->setBody(QByteArray("fake audio/* byte array"));
    res1.mutableData()->setSize(res1.data()->body()->size());

    res1.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *res1.data()->body(),
            QCryptographicHash::Md5));

    res1.setRecognition(qevercloud::Data{});
    res1.mutableRecognition()->setBody(
        QByteArray("<recoIndex docType=\"picture\" objType=\"image\" "
                   "objID=\"fc83e58282d8059be17debabb69be900\" "
                   "engineVersion=\"5.5.22.7\" recoType=\"service\" "
                   "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
                   "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
                   "<t w=\"87\">WIKI ?</t>"
                   "<t w=\"83\">WIKIPEDIA</t>"
                   "<t w=\"82\">WIKJPEDJA</t>"
                   "<t w=\"71\">WJKJPEDJA</t>"
                   "<t w=\"67\">MJKJPEDJA</t>"
                   "<t w=\"67\">MJKJREDJA</t>"
                   "<t w=\"66\">MJKJREDJA</t>"
                   "</item>"
                   "<item x=\"1840\" y=\"1475\" w=\"14\" h=\"12\">"
                   "<t w=\"11\">et</t>"
                   "<t w=\"10\">TQ</t>"
                   "</item>"
                   "</recoIndex>"));

    res1.mutableRecognition()->setSize(res1.recognition()->body()->size());

    res1.mutableRecognition()->setBodyHash(
        QCryptographicHash::hash(
            *res1.recognition()->body(),
            QCryptographicHash::Md5));

    auto & res2 = resources[2];
    res2.setMime(QStringLiteral("application/vnd.evernote.ink"));

    res2.setData(qevercloud::Data{});
    res2.mutableData()->setBody(
        QByteArray("fake application/vnd.evernote.ink byte array"));

    res2.mutableData()->setSize(res2.data()->body()->size());

    res2.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *res2.data()->body(),
            QCryptographicHash::Md5));

    // 4) ============= Create some ranges for note's properties ==============
    int numTitles = 3;
    QVector<QString> titles;
    titles.reserve(numTitles);
    titles << QString::fromUtf8("Potato (είΝΑΙ)") << QStringLiteral("Ham")
           << QStringLiteral("Eggs");

    int numContents = 9;
    QVector<QString> contents;
    contents.reserve(numContents);
    contents << QStringLiteral(
                    "<en-note><h1>The unique identifier of this note. "
                    "Will be set by the server</h1></en-note>")
             << QStringLiteral(
                    "<en-note><h1>The XHTML block that makes up the note. "
                    "This is the canonical form of the note's contents"
                    "</h1><en-todo checked = \"true\"/></en-note>")
             << QStringLiteral(
                    "<en-note><h1>The binary MD5 checksum of the UTF-8 "
                    "encoded content body.</h1></en-note>")
             << QString::fromUtf8(
                    "<en-note><h1>The number of Unicode characters "
                    "\"αυτό είναι ένα αυτοκίνητο\" in the content "
                    "of the note.</h1><en-todo/></en-note>")
             << QStringLiteral(
                    "<en-note><en-todo checked = \"true\"/><h1>The date "
                    "and time when the note was created in one of "
                    "the clients.</h1><en-todo checked = \"false\"/></en-note>")
             << QStringLiteral(
                    "<en-note><h1>If present [code characters], the note "
                    "is considered \"deleted\", and this stores the date "
                    "and time when the note was deleted</h1></en-note>")
             << QString::fromUtf8(
                    "<en-note><h1>If the note is available {ΑΥΤΌ "
                    "ΕΊΝΑΙ ΈΝΑ ΑΥΤΟΚΊΝΗΤΟ} for normal actions and viewing, "
                    "this flag will be set to true.</h1><en-crypt "
                    "hint=\"My Cat\'s Name\">NKLHX5yK1MlpzemJQijA"
                    "N6C4545s2EODxQ8Bg1r==</en-crypt></en-note>")
             << QString::fromUtf8(
                    "<en-note><h1>A number identifying the last "
                    "transaction (Αυτό ΕΊΝΑΙ ένα αυΤΟκίΝΗτο) to "
                    "modify the state of this note</h1></en-note>")
             << QStringLiteral(
                    "<en-note><h1>The list of resources that are embedded "
                    "within this note.</h1><en-todo checked = \"true\"/>"
                    "<en-crypt hint=\"My Cat\'s Name\">NKLHX5yK1Mlpzem"
                    "JQijAN6C4545s2EODxQ8Bg1r==</en-crypt></en-note>");

    QHash<QString, qint64> timestampForDateTimeString;

    QDateTime datetime = QDateTime::currentDateTime();
    datetime.setTime(QTime(0, 0, 0, 0)); // today midnight

    timestampForDateTimeString[QStringLiteral("day")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-1);

    timestampForDateTimeString[QStringLiteral("day-1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-1);

    timestampForDateTimeString[QStringLiteral("day-2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-1);

    timestampForDateTimeString[QStringLiteral("day-3")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(4);

    timestampForDateTimeString[QStringLiteral("day+1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(1);

    timestampForDateTimeString[QStringLiteral("day+2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(1);

    timestampForDateTimeString[QStringLiteral("day+3")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-3); // return back to today midnight

    int dayOfWeek = datetime.date().dayOfWeek();

    // go to the closest previous Sunday
    datetime = datetime.addDays(-1 * dayOfWeek);

    timestampForDateTimeString[QStringLiteral("week")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-7);

    timestampForDateTimeString[QStringLiteral("week-1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-7);

    timestampForDateTimeString[QStringLiteral("week-2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-7);

    timestampForDateTimeString[QStringLiteral("week-3")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(28);

    timestampForDateTimeString[QStringLiteral("week+1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(7);

    timestampForDateTimeString[QStringLiteral("week+2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(7);

    timestampForDateTimeString[QStringLiteral("week+3")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addDays(-21 + dayOfWeek); // return to today midnight

    int dayOfMonth = datetime.date().day();
    datetime = datetime.addDays(-(dayOfMonth - 1));

    timestampForDateTimeString[QStringLiteral("month")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addMonths(-1);

    timestampForDateTimeString[QStringLiteral("month-1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addMonths(-1);

    timestampForDateTimeString[QStringLiteral("month-2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addMonths(-1);

    timestampForDateTimeString[QStringLiteral("month-3")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addMonths(4);

    timestampForDateTimeString[QStringLiteral("month+1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addMonths(1);

    timestampForDateTimeString[QStringLiteral("month+2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addMonths(1);

    timestampForDateTimeString[QStringLiteral("month+3")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addMonths(-3);
    datetime =
        datetime.addDays(dayOfMonth - 1); // return back to today midnight

    int monthOfYear = datetime.date().month();
    datetime = datetime.addMonths(-(monthOfYear - 1));
    datetime = datetime.addDays(-(dayOfMonth - 1));

    timestampForDateTimeString[QStringLiteral("year")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addYears(-1);

    timestampForDateTimeString[QStringLiteral("year-1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addYears(-1);

    timestampForDateTimeString[QStringLiteral("year-2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addYears(-1);

    timestampForDateTimeString[QStringLiteral("year-3")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addYears(4);

    timestampForDateTimeString[QStringLiteral("year+1")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addYears(1);

    timestampForDateTimeString[QStringLiteral("year+2")] =
        datetime.toMSecsSinceEpoch();

    datetime = datetime.addYears(1);

    timestampForDateTimeString[QStringLiteral("year+3")] =
        datetime.toMSecsSinceEpoch();

    int numCreationTimestamps = 9;
    QVector<qint64> creationTimestamps;
    creationTimestamps.reserve(numCreationTimestamps);

    creationTimestamps << timestampForDateTimeString[QStringLiteral("day-3")]
                       << timestampForDateTimeString[QStringLiteral("day-2")]
                       << timestampForDateTimeString[QStringLiteral("day-1")]
                       << timestampForDateTimeString[QStringLiteral("day")]
                       << timestampForDateTimeString[QStringLiteral("day+1")]
                       << timestampForDateTimeString[QStringLiteral("day+2")]
                       << timestampForDateTimeString[QStringLiteral("day+3")]
                       << timestampForDateTimeString[QStringLiteral("week-3")]
                       << timestampForDateTimeString[QStringLiteral("week-2")];

    int numModificationTimestamps = 9;
    QVector<qint64> modificationTimestamps;
    modificationTimestamps.reserve(numModificationTimestamps);

    modificationTimestamps
        << timestampForDateTimeString[QStringLiteral("month-3")]
        << timestampForDateTimeString[QStringLiteral("month-2")]
        << timestampForDateTimeString[QStringLiteral("month-1")]
        << timestampForDateTimeString[QStringLiteral("month")]
        << timestampForDateTimeString[QStringLiteral("month+1")]
        << timestampForDateTimeString[QStringLiteral("month+2")]
        << timestampForDateTimeString[QStringLiteral("month+3")]
        << timestampForDateTimeString[QStringLiteral("week-1")]
        << timestampForDateTimeString[QStringLiteral("week")];

    int numSubjectDateTimestamps = 3;
    QVector<qint64> subjectDateTimestamps;
    subjectDateTimestamps.reserve(numSubjectDateTimestamps);

    subjectDateTimestamps
        << timestampForDateTimeString[QStringLiteral("week+1")]
        << timestampForDateTimeString[QStringLiteral("week+2")]
        << timestampForDateTimeString[QStringLiteral("week+3")];

    int numLatitudes = 9;
    QVector<double> latitudes;
    latitudes.reserve(numLatitudes);

    latitudes << -72.5 << -51.3 << -32.1 << -11.03 << 10.24 << 32.33 << 54.78
              << 72.34 << 91.18;

    int numLongitudes = 9;
    QVector<double> longitudes;
    longitudes.reserve(numLongitudes);

    longitudes << -71.15 << -52.42 << -31.91 << -12.25 << 9.78 << 34.62 << 56.17
               << 73.27 << 92.46;

    int numAltitudes = 9;
    QVector<double> altitudes;
    altitudes.reserve(numAltitudes);

    altitudes << -70.23 << -51.81 << -32.62 << -11.14 << 10.45 << 31.73 << 52.73
              << 71.82 << 91.92;

    int numAuthors = 3;
    QVector<QString> authors;
    authors.reserve(numAuthors);

    authors << QStringLiteral("Shakespeare") << QStringLiteral("Homer")
            << QStringLiteral("Socrates");

    int numSources = 3;
    QVector<QString> sources;
    sources.reserve(numSources);

    sources << QStringLiteral("web.clip") << QStringLiteral("mail.clip")
            << QStringLiteral("mobile.android");

    int numSourceApplications = 3;
    QVector<QString> sourceApplications;
    sourceApplications.reserve(numSourceApplications);

    sourceApplications << QStringLiteral("food.*") << QStringLiteral("hello.*")
                       << QStringLiteral("skitch.*");

    int numContentClasses = 3;
    QVector<QString> contentClasses;
    contentClasses.reserve(numContentClasses);

    contentClasses << QStringLiteral("evernote.food.meal")
                   << QStringLiteral("evernote.hello.*")
                   << QStringLiteral("whatever");

    int numPlaceNames = 3;
    QVector<QString> placeNames;
    placeNames.reserve(numPlaceNames);

    placeNames << QStringLiteral("home") << QStringLiteral("school")
               << QStringLiteral("bus");

    int numApplicationData = 3;
    QVector<QString> applicationData;
    applicationData.reserve(numApplicationData);

    applicationData << QStringLiteral("myapp") << QStringLiteral("Evernote")
                    << QStringLiteral("Quentier");

    int numReminderOrders = 3;
    QVector<qint64> reminderOrders;
    reminderOrders.reserve(numReminderOrders);
    reminderOrders << 1 << 2 << 3;

    int numReminderTimes = 3;
    QVector<qint64> reminderTimes;
    reminderTimes.reserve(numReminderTimes);

    reminderTimes << timestampForDateTimeString[QStringLiteral("year-3")]
                  << timestampForDateTimeString[QStringLiteral("year-2")]
                  << timestampForDateTimeString[QStringLiteral("year-1")];

    int numReminderDoneTimes = 3;
    QVector<qint64> reminderDoneTimes;
    reminderDoneTimes.reserve(numReminderDoneTimes);

    reminderDoneTimes << timestampForDateTimeString[QStringLiteral("year")]
                      << timestampForDateTimeString[QStringLiteral("year+1")]
                      << timestampForDateTimeString[QStringLiteral("year+2")];

    // 5) ============= Create some notes ================

    int numNotes = 9;
    QVector<qevercloud::Note> notes;
    notes.reserve(numNotes);

    for (int i = 0; i < numNotes; ++i) {
        notes << qevercloud::Note();
        auto & note = notes.back();

        note.setTitle(
            titles[i / numTitles] + QStringLiteral(" #") + QString::number(i));

        note.setContent(contents[i]);

        if (i != 7) {
            note.setCreated(creationTimestamps[i]);
        }

        if (!note.attributes()) {
            note.setAttributes(qevercloud::NoteAttributes{});
        }

        auto & attributes = *note.mutableAttributes();

        attributes.setSubjectDate(
            subjectDateTimestamps[i / numSubjectDateTimestamps]);

        if ((i != 6) && (i != 7) && (i != 8)) {
            attributes.setLatitude(latitudes[i]);
        }

        attributes.setLongitude(longitudes[i]);
        attributes.setAltitude(altitudes[i]);
        attributes.setAuthor(authors[i / numAuthors]);
        attributes.setSource(sources[i / numSources]);

        attributes.setSourceApplication(
            sourceApplications[i / numSourceApplications]);

        attributes.setContentClass(contentClasses[i / numContentClasses]);

        if (i / numPlaceNames != 2) {
            attributes.setPlaceName(placeNames[i / numPlaceNames]);
        }

        if ((i != 3) && (i != 4) && (i != 5)) {
            attributes.setApplicationData(qevercloud::LazyMap{});
            attributes.mutableApplicationData()->setKeysOnly(QSet<QString>{});

            auto & keysOnly =
                *attributes.mutableApplicationData()->mutableKeysOnly();

            keysOnly.insert(applicationData[i / numApplicationData]);

            attributes.mutableApplicationData()->setFullMap(
                QMap<QString, QString>{});

            auto & fullMap =
                *attributes.mutableApplicationData()->mutableFullMap();

            fullMap.insert(
                applicationData[i / numApplicationData],
                QStringLiteral("Application data value at key ") +
                    applicationData[i / numApplicationData]);
        }

        if (i == 6) {
            if (!attributes.applicationData()->keysOnly()) {
                attributes.mutableApplicationData()->setKeysOnly(
                    QSet<QString>{});
            }

            auto & keysOnly =
                *attributes.mutableApplicationData()->mutableKeysOnly();

            keysOnly.insert(applicationData[1]);

            if (!attributes.applicationData()->fullMap()) {
                attributes.mutableApplicationData()->setFullMap(QMap<QString, QString>{});
            }

            auto & fullMap =
                *attributes.mutableApplicationData()->mutableFullMap();

            fullMap.insert(
                applicationData[1],
                QStringLiteral("Application data value at key ") +
                    applicationData[1]);
        }

        if ((i != 0) && (i != 1) && (i != 2)) {
            attributes.setReminderOrder(reminderOrders[i / numReminderOrders]);
        }

        attributes.setReminderTime(reminderTimes[i / numReminderTimes]);
        attributes.setReminderDoneTime(
            reminderDoneTimes[i / numReminderDoneTimes]);

        if (i != (numNotes - 1)) {
            int k = 0;
            while (((i + k) < numTags) && (k < 3)) {
                if (note.tagGuids()) {
                    note.setTagGuids(
                        QList<qevercloud::Guid>()
                        << tags[i + k].guid().value());
                }
                else {
                    note.mutableTagGuids()->append(tags[i + k].guid().value());
                }

                note.mutableTagLocalIds().append(tags[i + k].localId());
                ++k;
            }
        }

        if (i != 8) {
            auto resource = resources[i / numResources];
            resource.setLocalId(QUuid::createUuid().toString());
            if (!note.resources()) {
                note.setResources(QList<qevercloud::Resource>() << resource);
            }
            else {
                note.mutableResources()->append(resource);
            }
        }

        if (i == 3) {
            auto additionalResource = resources[0];
            additionalResource.setLocalId(QUuid::createUuid().toString());
            if (!note.resources()) {
                note.setResources(
                    QList<qevercloud::Resource>() << additionalResource);
            }
            else {
                note.mutableResources()->append(additionalResource);
            }
        }
    }

    QMap<int, int> notebookIndexForNoteIndex;
    for (int i = 0; i < numNotes; ++i) {
        notebookIndexForNoteIndex[i] = i / numNotebooks;
    }

    // 6) =========== Create local storage, add created notebooks,
    //                tags and notes there ===========

    const Account account(
        QStringLiteral("LocalStorageManagerNoteSearchQueryTestFakeUser"),
        Account::Type::Local);

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    for (int i = 0; i < numNotebooks; ++i) {
        bool res = localStorageManager.addNotebook(notebooks[i], errorMessage);
        if (!res) {
            errorDescription = errorMessage.nonLocalizedString();
            return false;
        }
    }

    for (int i = 0; i < numTags; ++i) {
        bool res = localStorageManager.addTag(tags[i], errorMessage);
        if (!res) {
            errorDescription = errorMessage.nonLocalizedString();
            return false;
        }
    }

    for (int i = 0; i < numNotes; ++i) {
        notes[i].setNotebookLocalId(
            notebooks[notebookIndexForNoteIndex[i]].localId());

        bool res = localStorageManager.addNote(notes[i], errorMessage);
        if (!res) {
            errorDescription = errorMessage.nonLocalizedString();
            return false;
        }
    }

    // 7) =========== Create and execute some note search queries with advanced
    //                search modifiers, verify they are consistent

    bool res = false;

#define RUN_CHECK()                                                            \
    res = checkQueryString(                                                    \
        queryString, notes, expectedContainedNotesIndices,                     \
        localStorageManager, errorMessage);                                    \
    if (!res) {                                                                \
        errorDescription = errorMessage.nonLocalizedString();                  \
        return false;                                                          \
    }

    // 7.1) ToDo queries

    // 7.1.1) Finished todo query
    QString queryString = QStringLiteral("todo:true");
    QVector<bool> expectedContainedNotesIndices(numNotes, false);
    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[4] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.1.2) Unfinished todo query
    queryString = QStringLiteral("todo:false");

    expectedContainedNotesIndices[1] = false;
    expectedContainedNotesIndices[8] = false;
    expectedContainedNotesIndices[3] = true;

    RUN_CHECK()

    // 7.1.3) Any todo
    queryString = QStringLiteral("todo:*");

    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.1.4) True todo but no false todo
    queryString = QStringLiteral("todo:true -todo:false");

    expectedContainedNotesIndices[3] = false;
    expectedContainedNotesIndices[4] = false;

    RUN_CHECK()

    // 7.1.5) True but no false todo but this time with another order of query
    // elements
    queryString = QStringLiteral("-todo:false todo:true");

    RUN_CHECK()

    // 7.1.6) False but no true todo
    queryString = QStringLiteral("todo:false -todo:true");
    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[3] = true;

    RUN_CHECK()

    // 7.1.7) False but no true todo but this time with another order of query
    // elements
    queryString = QStringLiteral("-todo:true todo:false");

    RUN_CHECK()

    // 7.1.8) Ensure asterisk for todo catches all and ignores other options
    queryString = QStringLiteral("todo:true -todo:false todo:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[3] = true;
    expectedContainedNotesIndices[4] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.1.9) Ensure "any:" modifier works as expected with todo
    queryString = QStringLiteral("any: todo:true todo:false");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[3] = true;
    expectedContainedNotesIndices[4] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.1.10) Ensure we have just one match without "any:"
    queryString = QStringLiteral("todo:true todo:false");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[4] = true;

    RUN_CHECK()

    // 7.1.11) Ensure notes without "todo" tags can be found too:
    queryString = QStringLiteral("-todo:*");
    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;
    expectedContainedNotesIndices[2] = true;
    expectedContainedNotesIndices[5] = true;
    expectedContainedNotesIndices[6] = true;
    expectedContainedNotesIndices[7] = true;

    RUN_CHECK()

    // 7.2.1) Notes with encryption tags
    queryString = QStringLiteral("encryption:");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[6] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.2.2) Notes without encryption tags
    queryString = QStringLiteral("-encryption:");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[6] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 7.3) Arbitrary reminder order
    queryString = QStringLiteral("reminderOrder:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[0] = false;
    expectedContainedNotesIndices[1] = false;
    expectedContainedNotesIndices[2] = false;

    RUN_CHECK()

    // 7.4) No reminder order
    queryString = QStringLiteral("-reminderOrder:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;
    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[2] = true;

    RUN_CHECK()

    // 7.5) Notebook
    queryString = QStringLiteral("notebook:\"Test notebook #1\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[3] = true;
    expectedContainedNotesIndices[4] = true;
    expectedContainedNotesIndices[5] = true;

    RUN_CHECK()

    // 7.6) Tags

    // 7.6.1) Check a single tag
    queryString = QStringLiteral("tag:\"");
    queryString += tags[1].name().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;
    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 7.6.2) Check negative for single tag
    queryString = QStringLiteral("-tag:\"");
    queryString += tags[2].name().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[0] = false;
    expectedContainedNotesIndices[1] = false;
    expectedContainedNotesIndices[2] = false;

    RUN_CHECK()

    // 7.6.3) Check for multiple tags
    queryString = QStringLiteral("tag:\"");
    queryString += tags[1].name().value();
    queryString += QStringLiteral("\" tag:\"");
    queryString += tags[3].name().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 7.6.3) Check for multiple tags with any modifier
    queryString = QStringLiteral("any: tag:\"");
    queryString += tags[1].name().value();
    queryString += QStringLiteral("\" tag:\"");
    queryString += tags[3].name().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;
    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[2] = true;
    expectedContainedNotesIndices[3] = true;

    RUN_CHECK()

    // 7.6.4) Check for both positive and negated tags
    queryString = QStringLiteral("tag:\"");
    queryString += tags[4].name().value();
    queryString += QStringLiteral("\" -tag:\"");
    queryString += tags[2].name().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[3] = true;
    expectedContainedNotesIndices[4] = true;

    RUN_CHECK()

    // 7.6.5) Check for both positive and negated tag names with "any:" modifier
    queryString = QStringLiteral("any: tag:\"");
    queryString += tags[4].name().value();
    queryString += QStringLiteral("\" -tag:\"");
    queryString += tags[2].name().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < 2; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    for (int i = 2; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    RUN_CHECK()

    // 7.6.6) Find all notes with a tag
    queryString = QStringLiteral("tag:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 7.6.7) Find all notes without a tag
    queryString = QStringLiteral("-tag:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.7) Resource mime types

    // 7.7.1) Check a single mime type
    queryString = QStringLiteral("resource:\"");
    queryString += resources[1].mime().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i / numResources == 1);
    }

    RUN_CHECK()

    // 7.7.2) Check negative for single resource mime type
    queryString = QStringLiteral("-resource:\"");
    queryString += resources[2].mime().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i / numResources != 2);
    }

    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.7.3) Check for multiple resource mime types
    queryString = QStringLiteral("resource:\"");
    queryString += resources[0].mime().value();
    queryString += QStringLiteral("\" resource:\"");
    queryString += resources[1].mime().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[3] = true;

    RUN_CHECK()

    // 7.7.4) Check for multiple resource mime types with "any:" modifier
    queryString = QStringLiteral("any: resource:\"");
    queryString += resources[0].mime().value();
    queryString += QStringLiteral("\" resource:\"");
    queryString += resources[1].mime().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < 6; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    for (int i = 6; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    RUN_CHECK()

    // 7.7.5) Check for both positive and negated resource mime types
    queryString = QStringLiteral("resource:\"");
    queryString += resources[0].mime().value();
    queryString += QStringLiteral("\" -resource:\"");
    queryString += resources[1].mime().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < 3; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    for (int i = 3; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    RUN_CHECK()

    // 7.7.6) Check for both positive and negated resource mime types with
    // "any:" modifier
    queryString = QStringLiteral("any: resource:\"");
    queryString += resources[0].mime().value();
    queryString += QStringLiteral("\" -resource:\"");
    queryString += resources[1].mime().value();
    queryString += QStringLiteral("\"");

    for (int i = 0; i < 4; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    for (int i = 4; i < 6; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    for (int i = 6; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    RUN_CHECK()

    // 7.7.7) Find all notes with a resource of any mime type
    queryString = QStringLiteral("resource:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 7.7.8) Find all notes without resources
    queryString = QStringLiteral("-resource:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 7.8) Creation timestamps

    // 7.8.1) Single creation timestamps
    QStringList creationTimestampDateTimeStrings;
    creationTimestampDateTimeStrings.reserve(numNotes - 1);

    creationTimestampDateTimeStrings
        << QStringLiteral("day-3") << QStringLiteral("day-2")
        << QStringLiteral("day-1") << QStringLiteral("day")
        << QStringLiteral("day+1") << QStringLiteral("day+2")
        << QStringLiteral("day+3") << QStringLiteral("week-3")
        << QStringLiteral("week-2");

    queryString = QStringLiteral("created:day");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i >= 3) && (i <= 6));
    }

    RUN_CHECK()

    // 7.8.2) Negated single creation timestamps
    queryString = QStringLiteral("-created:day+1");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i <= 3) || (i > 7));
    }

    RUN_CHECK()

    // 7.8.3) Multiple creation timestamps
    queryString = QStringLiteral("created:day created:day+2");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i >= 5) && (i < 7));
    }

    RUN_CHECK()

    // 7.8.4) Multiple negated creation timestamps
    queryString = QStringLiteral("-created:day+2 -created:day-1");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i < 2) || (i == 8));
    }

    RUN_CHECK()

    // 7.8.5) Multiple creation timestamps with "any:" modifier
    queryString = QStringLiteral("any: created:day-1 created:day+2");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i >= 2) && (i < 7));
    }

    RUN_CHECK()

    // 7.8.6) Multiple negated creation timestamps with "any:" modifier
    queryString = QStringLiteral("any: -created:day+2 -created:day-1");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i <= 4) || (i == 8));
    }

    RUN_CHECK()

    // 7.8.7) Both positive and negated creation timestamps
    queryString = QStringLiteral("created:day-1 -created:day+2");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i >= 2) && (i < 5));
    }

    RUN_CHECK()

    // 7.8.8) Both positive and negated creation timestamps with "any:" modifier
    queryString = QStringLiteral("any: created:day+2 -created:day-1");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] =
            ((i < 2) || ((i >= 5) && (i < 7)) || (i == 8));
    }

    RUN_CHECK()

    // 7.8.9) Find notes with any creation timestamp set
    queryString = QStringLiteral("created:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i != 7);
    }

    RUN_CHECK()

    // 7.8.10) Find notes with no creation timestamp set
    queryString = QStringLiteral("-created:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i == 7);
    }

    RUN_CHECK()

    // 7.9 latitudes

    // 7.9.1) Single latitude
    queryString = QStringLiteral("latitude:10");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i == 4) || (i == 5));
    }

    RUN_CHECK()

    // 7.9.2) Single negated latitude
    queryString = QStringLiteral("-latitude:-30");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i <= 2);
    }

    RUN_CHECK()

    // 7.9.3) Multiple latitudes
    queryString = QStringLiteral("latitude:-10 latitude:10");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i == 4) || (i == 5));
    }

    RUN_CHECK()

    // 7.9.4) Multiple latitudes with "any:" modifier
    queryString = QStringLiteral("any: latitude:-10 latitude:10");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i >= 4) && (i < 6));
    }

    RUN_CHECK()

    // 7.9.5) Multiple negated latitudes
    queryString = QStringLiteral("-latitude:-30 -latitude:-10");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i <= 2);
    }

    RUN_CHECK()

    // 7.9.6) Multiple negated latitudes with "any:" modifier
    queryString = QStringLiteral("any: -latitude:-30 -latitude:-10");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i <= 3);
    }

    RUN_CHECK()

    // 7.9.7) Both positive and negated latitudes
    queryString = QStringLiteral("latitude:-20 -latitude:20");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i == 3) || (i == 4));
    }

    RUN_CHECK()

    // 7.9.8) Both positive and negated latitudes with "any:" modifier
    queryString = QStringLiteral("any: -latitude:-30 latitude:30");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i <= 2) || (i == 5));
    }

    RUN_CHECK()

    // 7.9.9) Find notes with any latitude set
    queryString = QStringLiteral("latitude:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i < 6);
    }

    RUN_CHECK()

    // 7.9.10) Find notes without latitude set
    queryString = QStringLiteral("-latitude:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i >= 6);
    }

    RUN_CHECK()

    // 7.10) place names

    // 7.10.1) Single place name
    queryString = QStringLiteral("placeName:");
    queryString += placeNames[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i > 2) && (i < 6));
    }

    RUN_CHECK()

    // 7.10.2) Single negated place name
    queryString = QStringLiteral("-placeName:");
    queryString += placeNames[0];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i > 2);
    }

    RUN_CHECK()

    // 7.10.3) Two place names (each note has the only one)
    queryString = QStringLiteral("placeName:");
    queryString += placeNames[0];
    queryString += QStringLiteral(" placeName:");
    queryString += placeNames[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    RUN_CHECK()

    // 7.10.4) Two place names with "any:" modifier
    queryString = QStringLiteral("any: placeName:");
    queryString += placeNames[0];
    queryString += QStringLiteral(" placeName:");
    queryString += placeNames[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i < 6);
    }

    RUN_CHECK()

    // 7.10.5) Both positive and negated placeNames (should work the same way
    // as only positive placeName provided that negated one is not the same)
    queryString = QStringLiteral("placeName:");
    queryString += placeNames[0];
    queryString += QStringLiteral(" -placeName:");
    queryString += placeNames[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i < 3);
    }

    RUN_CHECK()

    // 7.10.6) Both positive and negated placeNames with "any:" modifier
    queryString = QStringLiteral("any: placeName:");
    queryString += placeNames[0];
    queryString += QStringLiteral(" -placeName:");
    queryString += placeNames[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i < 3) || (i > 5));
    }

    RUN_CHECK()

    // 7.10.7) Two negated placeNames
    queryString = QStringLiteral("-placeName:");
    queryString += placeNames[0];
    queryString += QStringLiteral(" -placeName:");
    queryString += placeNames[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i > 5);
    }

    RUN_CHECK()

    // 7.10.8) Two negated placeNames with "any:" modifier
    queryString = QStringLiteral("any: -placeName:");
    queryString += placeNames[0];
    queryString += QStringLiteral(" -placeName:");
    queryString += placeNames[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    RUN_CHECK()

    // 7.10.9) Find note with any place name
    queryString = QStringLiteral("placeName:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i < 6);
    }

    RUN_CHECK()

    // 7.10.10) Find note without any place name
    queryString = QStringLiteral("-placeName:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i > 5);
    }

    RUN_CHECK()

    // 7.11) Application data

    // 7.11.1) Find notes with a single application data entry
    queryString = QStringLiteral("applicationData:");
    queryString += applicationData[0];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i < 3);
    }

    RUN_CHECK()

    // 7.11.2) Find notes via negated application data entry
    queryString = QStringLiteral("-applicationData:");
    queryString += applicationData[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i < 6) || (i > 6));
    }

    RUN_CHECK()

    // 7.11.3) Find notes with two application data entries
    queryString = QStringLiteral("applicationData:");
    queryString += applicationData[1];
    queryString += QStringLiteral(" applicationData:");
    queryString += applicationData[2];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i == 6);
    }

    RUN_CHECK()

    // 7.11.4) Find notes with two application data entries and "any:" modifier
    queryString = QStringLiteral("any: applicationData:");
    queryString += applicationData[0];
    queryString += QStringLiteral(" applicationData:");
    queryString += applicationData[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i < 3) || (i == 6));
    }

    RUN_CHECK()

    // 7.11.5) Find notes with two negated application data entries
    queryString = QStringLiteral("-applicationData:");
    queryString += applicationData[0];
    queryString += QStringLiteral(" -applicationData:");
    queryString += applicationData[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = ((i > 2) && (i != 6));
    }

    RUN_CHECK()

    // 7.11.6) Find notes with two negated application data entries
    // and "any:" modifier
    queryString = QStringLiteral("any: -applicationData:");
    queryString += applicationData[0];
    queryString += QStringLiteral(" -applicationData:");
    queryString += applicationData[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    RUN_CHECK()

    // 7.11.7) Find notes with both positive and negated application data entry
    queryString = QStringLiteral("applicationData:");
    queryString += applicationData[2];
    queryString += QStringLiteral(" -applicationData:");
    queryString += applicationData[1];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i > 6);
    }

    RUN_CHECK()

    // 7.11.8) Find notes with both positive and negated application data entry
    // and "any:" modifier
    queryString = QStringLiteral("any: applicationData:");
    queryString += applicationData[2];
    queryString += QStringLiteral(" -applicationData:");
    queryString += applicationData[0];

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = (i > 2);
    }

    RUN_CHECK()

    // 7.11.9) Arbitrary application data
    queryString = QStringLiteral("applicationData:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }
    expectedContainedNotesIndices[3] = false;
    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[5] = false;

    RUN_CHECK()

    // 7.11.10) No application data
    queryString = QStringLiteral("-applicationData:*");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }
    expectedContainedNotesIndices[3] = true;
    expectedContainedNotesIndices[4] = true;
    expectedContainedNotesIndices[5] = true;

    RUN_CHECK()

    // 8) =========== Create and execute some note search queries without
    //                advanced search modifiers, verify they are consistent

    // 8.1.1 Find a single note with a single term query
    queryString = QStringLiteral("cAnOniCal");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }
    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.1.2 Find all notes without a singe term query
    queryString = QStringLiteral("-canOnIcal");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[1] = false;

    RUN_CHECK()

    // 8.1.3 Find all notes corresponding to several note search terms
    queryString = QStringLiteral("any: cAnOnical cHeckSuM ConsiDerEd");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[2] = true;
    expectedContainedNotesIndices[5] = true;

    RUN_CHECK()

    // 8.1.4 Attempt to find the intersection of all notes corresponding
    // to several note search terms
    queryString = QStringLiteral("cAnOnical cHeckSuM ConsiDerEd");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    RUN_CHECK()

    // 8.1.5 Find all notes except those excluded from the search
    queryString = QStringLiteral("-cAnOnical -cHeckSuM -ConsiDerEd");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[1] = false;
    expectedContainedNotesIndices[2] = false;
    expectedContainedNotesIndices[5] = false;

    RUN_CHECK()

    // 8.1.6 Find the union of all notes except those excluded from several
    // searches
    queryString = QStringLiteral("any: -cAnOnical -cHeckSuM -ConsiDerEd");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    RUN_CHECK()

    // 8.1.7 Find all notes corresponding to a mixed query containing both
    // included and excluded search terms when some of them "overlap" in certain
    // notes
    queryString = QStringLiteral("-iDEnTIfIEr xhTmL -cHeckSuM -ConsiDerEd");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.2.1 Find all notes corresponding to a query which involves tag names
    // as well as note content
    queryString = QStringLiteral("any: CaNonIcAl colLeGE UniCODe foOtLOCkeR");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;
    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[3] = true;
    expectedContainedNotesIndices[5] = true;
    expectedContainedNotesIndices[6] = true;
    expectedContainedNotesIndices[7] = true;

    RUN_CHECK()

    // 8.2.2 Find the intersection of all notes corresponding to queries
    // involving tag names as well as note content
    queryString = QStringLiteral("CaNonIcAl sERveR");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.2.3 Find all notes corresponding to a query which involves both
    // "positive" and negated note content words and tag names
    queryString = QStringLiteral("wiLl -colLeGe");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[6] = true;

    RUN_CHECK()

    // 8.3.1 Find all notes corresponding to a query which involves note titles
    // as well as note content
    queryString = QStringLiteral("any: CaNonIcAl eGGs");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[6] = true;
    expectedContainedNotesIndices[7] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 8.3.2 Find the intersection of all notes corresponding to a query which
    // involves note titles as well as note content
    queryString = QStringLiteral("CaNonIcAl PotAto");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.3.3 Find all notes corresponding to a query which involves both
    // "positive" and negated note content words and titles
    queryString = QStringLiteral("unIQue -EGgs");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;

    RUN_CHECK()

    // 8.3.4 Find the union of notes corresponding to a query involving both
    // "positive" and negated note content words and titles
    queryString = QStringLiteral("any: cONSiDERed -hAm");
    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[3] = false;
    expectedContainedNotesIndices[4] = false;

    RUN_CHECK()

    // 8.3.5 Find all notes corresponding to a query involving note content
    // words, titles and tag names
    queryString = QStringLiteral("any: cHECksUM SeRVEr hAM");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[6] = false;
    expectedContainedNotesIndices[7] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.4.1 Find note corresponding to resource recognition data
    queryString = QStringLiteral("inFoRmATiON");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;
    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[2] = true;
    expectedContainedNotesIndices[3] = true;

    RUN_CHECK()

    // 8.4.2 Find notes corresponding to negated resource recognition data
    queryString = QStringLiteral("-infoRMatiON");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[0] = false;
    expectedContainedNotesIndices[1] = false;
    expectedContainedNotesIndices[2] = false;
    expectedContainedNotesIndices[3] = false;

    RUN_CHECK()

    // 8.4.3 Find the union of notes corresponding to different resource
    // recognition data
    queryString = QStringLiteral("infoRMAtion wikiPEDiA any:");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[6] = false;
    expectedContainedNotesIndices[7] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.4.4. Find notes corresponding to different negated resource recognition
    // data
    queryString = QStringLiteral("-inFORMation -wikiPEDiA");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[6] = true;
    expectedContainedNotesIndices[7] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 8.4.5. Find the intersection of notes corresponding to the query
    // involving content search terms, note titles, tag names and resource
    // recognition data
    queryString = QStringLiteral("inFOrMATioN tHe poTaTO serVEr");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[0] = true;
    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.4.6 Find the union of notes corresponding to the query involving
    // content search terms, note titles, tag names and resource recognition
    // data

    queryString = QStringLiteral("wiKiPeDiA servER haM iDEntiFYiNg any:");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[2] = false;
    expectedContainedNotesIndices[6] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.4.7 Find the intersection of notes corresponding to the query involving
    // some positive and some negated content search terms, note titles, tag
    // names and resource recognition data

    queryString = QStringLiteral("infORMatioN -colLEgE pOtaTo -xHtMl");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[2] = true;

    RUN_CHECK()

    // 8.4.8 Find the union of notes corresponding to the query involving some
    // positive and some negated content search terms, note titles, tag names
    // and resource recognition data

    queryString = QStringLiteral("wikiPEDiA traNSActioN any: -PotaTo -biNARy");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[0] = false;
    expectedContainedNotesIndices[1] = false;
    expectedContainedNotesIndices[2] = false;

    RUN_CHECK()

    // 8.5.1 Find notes corresponding to a phrase containing a whitespace
    queryString = QStringLiteral("\"The list\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 8.5.2 Find the union of notes corresponding to several phrases

    queryString =
        QStringLiteral("\"tHe lIsT\" \"tHE lASt\" any: \"tHE xhTMl\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;
    expectedContainedNotesIndices[7] = true;
    expectedContainedNotesIndices[8] = true;

    RUN_CHECK()

    // 8.5.3 Find the union of notes corresponding to a couple of phrases and
    // other search terms as well
    queryString = QStringLiteral("any: \"The xhTMl\" eggS wikiPEDiA");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[0] = false;
    expectedContainedNotesIndices[2] = false;

    RUN_CHECK()

    // 8.5.4 Find notes corresponding to some positive and some negated search
    // terms containing phrases
    queryString = QStringLiteral("\"tHE noTE\" -\"of tHE\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[5] = true;
    expectedContainedNotesIndices[6] = true;

    RUN_CHECK()

    // 8.5.5 Find notes corresponding to some phrase with the wildcard in the
    // end
    queryString = QStringLiteral("\"the canonic*\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.5.6 Find notes corresponding to some phrase containing the wildcard
    // in the middle of it
    queryString = QStringLiteral("\"the can*cal\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.5.7 Find notes corresponding to some phrase containing the wildcard
    // in the beginning of it
    queryString = QStringLiteral("\"*onical\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[1] = true;

    RUN_CHECK()

    // 8.5.8 Find notes corresponding to some phrase containing the wildcard
    // in the beginning of it
    queryString = QStringLiteral("\"*code characters\"");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = false;
    }

    expectedContainedNotesIndices[3] = true;
    expectedContainedNotesIndices[5] = true;

    RUN_CHECK()

    // 8.6.1 Find notes corresponding to Greek letters using characters
    // with diacritics for the note search query
    queryString = QString::fromUtf8("είναι");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[5] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.6.2 Find notes corresponding to Greek letters using characters
    // with diacritics and upper case for the note search query
    queryString = QString::fromUtf8("ΕΊΝΑΙ");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[5] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.6.3 Find notes corresponding to Greek letters using characters
    // with diacritics and mixed case for the note search query
    queryString = QString::fromUtf8("ΕΊναι");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[5] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.6.4 Find notes corresponding to Greek letters using characters
    // without diacritics
    queryString = QString::fromUtf8("ειναι");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[5] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.6.5 Find notes corresponding to Greek letters using characters
    // without diacritics in upper case
    queryString = QString::fromUtf8("ΕΙΝΑΙ");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[5] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.6.6 Find notes corresponding to Greek letters using characters
    // without diacritics in mixed case
    queryString = QString::fromUtf8("ΕΙναι");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[5] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    // 8.6.7 Find notes corresponding to Greek letters using characters with
    // and without diacritics in mixed case when tags are also involved
    queryString = QString::fromUtf8("ΕΊναι any: αυΤΟκιΝΗτο");

    for (int i = 0; i < numNotes; ++i) {
        expectedContainedNotesIndices[i] = true;
    }

    expectedContainedNotesIndices[4] = false;
    expectedContainedNotesIndices[8] = false;

    RUN_CHECK()

    return true;
}

} // namespace quentier::test
