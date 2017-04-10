/*
 * Copyright 2017 Dmitry Ivanov
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

#include "EnexExportImportTests.h"
#include <quentier/enml/ENMLConverter.h>
#include <quentier/types/Note.h>
#include <quentier/types/Tag.h>
#include <quentier/types/Resource.h>
#include <quentier/types/ErrorString.h>
#include <quentier/logging/QuentierLogger.h>
#include <QHash>
#include <QCryptographicHash>
#include <QFile>
#include <cmath>

namespace quentier {
namespace test {

bool compareNoteContents(const Note & lhs, const Note & rhs, QString & error);
bool compareNotes(const QVector<Note> & originalNotes, const QVector<Note> & importedNotes, QString & error);

void setupSampleNote(Note & note);

void setupNoteTags(Note & note, QHash<QString, QString> & tagNamesByTagLocalUids);
void bindTagsWithNotes(QVector<Note> & importedNotes, const QHash<QString, QStringList> & tagNamesByNoteLocalUid,
                       const QHash<QString, QString> & tagNamesByTagLocalUids);

bool setupNoteResources(Note & note, QString & error);

bool exportSingleNoteWithoutTagsAndResourcesToEnexAndImportBack(QString & error)
{
    Note note;
    setupSampleNote(note);

    QVector<Note> notes;
    notes << note;

    QHash<QString, QString> tagNamesByTagLocalUids;
    ErrorString errorDescription;

    QString enex;

    ENMLConverter converter;
    bool res = converter.exportNotesToEnex(notes, tagNamesByTagLocalUids, enex, errorDescription);
    if (Q_UNLIKELY(!res)) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    QVector<Note> importedNotes;
    QHash<QString, QStringList> tagNamesByNoteLocalUid;

    res = converter.importEnex(enex, importedNotes, tagNamesByNoteLocalUid, errorDescription);
    if (Q_UNLIKELY(!res)) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    return compareNotes(notes, importedNotes, error);
}

bool exportSingleNoteWithTagsButNoResourcesToEnexAndImportBack(QString & error)
{
    Note note;
    setupSampleNote(note);

    QHash<QString, QString> tagNamesByTagLocalUids;
    setupNoteTags(note, tagNamesByTagLocalUids);

    QVector<Note> notes;
    notes << note;

    ErrorString errorDescription;
    QString enex;

    ENMLConverter converter;
    bool res = converter.exportNotesToEnex(notes, tagNamesByTagLocalUids, enex, errorDescription);
    if (Q_UNLIKELY(!res)) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    QVector<Note> importedNotes;
    QHash<QString, QStringList> tagNamesByNoteLocalUid;

    res = converter.importEnex(enex, importedNotes, tagNamesByNoteLocalUid, errorDescription);
    if (Q_UNLIKELY(!res)) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    bindTagsWithNotes(importedNotes, tagNamesByNoteLocalUid, tagNamesByTagLocalUids);
    return compareNotes(notes, importedNotes, error);
}

bool exportSingleNoteWithResourcesButNoTagsToEnexAndImportBack(QString & error)
{
    Note note;
    setupSampleNote(note);
    bool res = setupNoteResources(note, error);
    if (Q_UNLIKELY(!res)) {
        return false;
    }

    QHash<QString, QString> tagNamesByTagLocalUids;

    QVector<Note> notes;
    notes << note;

    ErrorString errorDescription;
    QString enex;

    ENMLConverter converter;
    res = converter.exportNotesToEnex(notes, tagNamesByTagLocalUids, enex, errorDescription);
    if (Q_UNLIKELY(!res)) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    QVector<Note> importedNotes;
    QHash<QString, QStringList> tagNamesByNoteLocalUid;

    res = converter.importEnex(enex, importedNotes, tagNamesByNoteLocalUid, errorDescription);
    if (Q_UNLIKELY(!res)) {
        error = errorDescription.nonLocalizedString();
        return false;
    }

    return compareNotes(notes, importedNotes, error);
}

bool compareNoteContents(const Note & lhs, const Note & rhs, QString & error)
{
    if (lhs.hasTitle() != rhs.hasTitle()) {
        error = QStringLiteral("left: has title = ") + (lhs.hasTitle() ? QStringLiteral("true") : QStringLiteral("false")) +
                QStringLiteral(", right: has title = ") + (rhs.hasTitle() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.hasTitle() && (lhs.title() != rhs.title())) {
        error = QStringLiteral("left: title = ") + lhs.title() + QStringLiteral(", right: title = ") + rhs.title();
        return false;
    }

    if (lhs.hasContent() != rhs.hasContent()) {
        error = QStringLiteral("left: has content = ") + (lhs.hasContent() ? QStringLiteral("true") : QStringLiteral("false")) +
                QStringLiteral(", right: has title = ") + (rhs.hasContent() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.hasContent() && (lhs.content() != rhs.content())) {
        error = QStringLiteral("left: content = ") + lhs.content() + QStringLiteral("\n\nRight: content = ") + rhs.content();
        return false;
    }

    if (lhs.hasCreationTimestamp() != rhs.hasCreationTimestamp()) {
        error = QStringLiteral("left: has creation timestamp = ") +
                (lhs.hasCreationTimestamp() ? QStringLiteral("true") : QStringLiteral("false")) +
                QStringLiteral(", right: has creation timestamp = ") +
                (rhs.hasCreationTimestamp() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.hasCreationTimestamp() && (lhs.creationTimestamp() != rhs.creationTimestamp())) {
        error = QStringLiteral("left: creation timestamp = ") + QString::number(lhs.creationTimestamp()) +
                QStringLiteral(", right: creation timestamp = ") + QString::number(rhs.creationTimestamp());
        return false;
    }

    if (lhs.hasModificationTimestamp() != rhs.hasModificationTimestamp()) {
        error = QStringLiteral("left: has modification timestamp = ") +
                (lhs.hasModificationTimestamp() ? QStringLiteral("true") : QStringLiteral("false")) +
                QStringLiteral(", right: has modification timestamp = ") +
                (rhs.hasModificationTimestamp() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.hasModificationTimestamp() && (lhs.modificationTimestamp() != rhs.modificationTimestamp())) {
        error = QStringLiteral("left: modification timestamp = ") + QString::number(lhs.modificationTimestamp()) +
                QStringLiteral(", right: modification timestamp = ") + QString::number(rhs.modificationTimestamp());
        return false;
    }

    if (lhs.hasTagLocalUids() != rhs.hasTagLocalUids()) {
        error = QStringLiteral("left: has tag local uids = ") +
                (lhs.hasTagLocalUids() ? QStringLiteral("true") : QStringLiteral("false")) +
                QStringLiteral(", right: has tag local uids = ") +
                (rhs.hasTagLocalUids() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.hasTagLocalUids())
    {
        const QStringList & leftTagLocalUids = lhs.tagLocalUids();
        const QStringList & rightTagLocalUids = rhs.tagLocalUids();
        if (leftTagLocalUids.size() != rightTagLocalUids.size()) {
            error = QStringLiteral("left and right notes has different number of tag local uids");
            return false;
        }

        for(auto it = leftTagLocalUids.constBegin(), end = leftTagLocalUids.constEnd(); it != end; ++it)
        {
            if (!rightTagLocalUids.contains(*it)) {
                error = QStringLiteral("left: has tag local uid ") + *it + QStringLiteral(" which right doesn't have");
                return false;
            }
        }
    }

    if (lhs.hasNoteAttributes() != rhs.hasNoteAttributes()) {
        error = QStringLiteral("left: has note attributes = ") +
                (lhs.hasNoteAttributes() ? QStringLiteral("true") : QStringLiteral("false")) +
                QStringLiteral(", right: has note attributes = ") +
                (rhs.hasNoteAttributes() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.hasNoteAttributes())
    {
        const qevercloud::NoteAttributes & leftNoteAttributes = lhs.noteAttributes();
        const qevercloud::NoteAttributes & rightNoteAttributes = rhs.noteAttributes();

#define CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftNoteAttributes.attrName.isSet() != rightNoteAttributes.attrName.isSet()) { \
            error = QStringLiteral("left: has " #attrName " = ") + \
                    (leftNoteAttributes.attrName.isSet() ? QStringLiteral("true") : QStringLiteral("false")) + \
                    QStringLiteral(", right: has " #attrName " = ") + \
                    (rightNoteAttributes.attrName.isSet() ? QStringLiteral("true") : QStringLiteral("false")); \
            return false; \
        }

#define CHECK_NOTE_DOUBLE_ATTRIBUTE(attrName) \
        CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftNoteAttributes.attrName.isSet() && \
            (std::fabs(leftNoteAttributes.attrName.ref() - rightNoteAttributes.attrName.ref()) > 1.0e-9)) \
        { \
            error = QStringLiteral("left: " #attrName " = ") + QString::number(leftNoteAttributes.attrName.ref()) + \
                    QStringLiteral(", right: " #attrName " = ") + QString::number(rightNoteAttributes.attrName.ref()); \
            return false; \
        }

#define CHECK_NOTE_STRING_ATTRIBUTE(attrName) \
        CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftNoteAttributes.attrName.isSet() && (leftNoteAttributes.attrName.ref() != rightNoteAttributes.attrName.ref())) { \
            error = QStringLiteral("left: " #attrName " = ") + leftNoteAttributes.attrName.ref() + \
                    QStringLiteral(", right: " #attrName " = ") + rightNoteAttributes.attrName.ref(); \
            return false; \
        }

#define CHECK_NOTE_INTEGER_ATTRIBUTE(attrName) \
        CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftNoteAttributes.attrName.isSet() && (leftNoteAttributes.attrName.ref() != rightNoteAttributes.attrName.ref())) { \
            error = QStringLiteral("left: " #attrName " = ") + QString::number(leftNoteAttributes.attrName.ref()) + \
                    QStringLiteral(", right: " #attrName " = ") + QString::number(rightNoteAttributes.attrName.ref()); \
            return false; \
        }

        CHECK_NOTE_DOUBLE_ATTRIBUTE(latitude)
        CHECK_NOTE_DOUBLE_ATTRIBUTE(longitude)
        CHECK_NOTE_DOUBLE_ATTRIBUTE(altitude)

        CHECK_NOTE_STRING_ATTRIBUTE(author)
        CHECK_NOTE_STRING_ATTRIBUTE(source)
        CHECK_NOTE_STRING_ATTRIBUTE(sourceURL)
        CHECK_NOTE_STRING_ATTRIBUTE(sourceApplication)

        CHECK_NOTE_INTEGER_ATTRIBUTE(reminderOrder)
        CHECK_NOTE_INTEGER_ATTRIBUTE(reminderTime)
        CHECK_NOTE_INTEGER_ATTRIBUTE(reminderDoneTime)

        CHECK_NOTE_STRING_ATTRIBUTE(placeName)
        CHECK_NOTE_STRING_ATTRIBUTE(contentClass)

        CHECK_NOTE_ATTRIBUTE_PRESENCE(applicationData)
        if (leftNoteAttributes.applicationData.isSet())
        {
            const qevercloud::LazyMap & leftLazyMap = leftNoteAttributes.applicationData.ref();
            const qevercloud::LazyMap & rightLazyMap = rightNoteAttributes.applicationData.ref();

            if (leftLazyMap.fullMap.isSet() != rightLazyMap.fullMap.isSet()) {
                error = QStringLiteral("left: application data full map is set = ") +
                        (leftLazyMap.fullMap.isSet() ? QStringLiteral("true") : QStringLiteral("false")) +
                        QStringLiteral(", right: application data full map is set = ") +
                        (rightLazyMap.fullMap.isSet() ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            if (leftLazyMap.fullMap.isSet() && (leftLazyMap.fullMap.ref() != rightLazyMap.fullMap.ref())) {
                error = QStringLiteral("left and right notes' application data full maps are not equal");
                return false;
            }
        }
    }

    if (lhs.hasResources() != rhs.hasResources()) {
        error = QStringLiteral("left: has resources = ") +
                (lhs.hasResources() ? QStringLiteral("true") : QStringLiteral("false")) +
                QStringLiteral(", right: has resources = ") +
                (rhs.hasResources() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.hasResources())
    {
        QList<Resource> leftResources = lhs.resources();
        QList<Resource> rightResources = rhs.resources();

        int numResources = leftResources.size();
        if (numResources != rightResources.size()) {
            error = QStringLiteral("left note has ") + QString::number(numResources) +
                    QStringLiteral(" resources while the right one has ") + QString::number(rightResources.size()) +
                    QStringLiteral(" resources");
            return false;
        }

        for(int i = 0; i < numResources; ++i)
        {
            const Resource & leftResource = leftResources.at(i);
            const Resource & rightResource = rightResources.at(i);

            if (Q_UNLIKELY(!leftResource.hasMime())) {
                error = QStringLiteral("left note's resource has no mime");
                return false;
            }

            if (Q_UNLIKELY(!rightResource.hasMime())) {
                error = QStringLiteral("right note's resource has no mime");
                return false;
            }

            if (leftResource.mime() != rightResource.mime()) {
                error = QStringLiteral("left and right resource's mime types don't match: left = ") +
                        leftResource.mime() + QStringLiteral(", right = ") + rightResource.mime();
                return false;
            }

            if (Q_UNLIKELY(!leftResource.hasDataBody())) {
                error = QStringLiteral("left note's resource has no data body");
                return false;
            }

            if (Q_UNLIKELY(!rightResource.hasDataBody())) {
                error = QStringLiteral("right note's resource has no data body");
                return false;
            }

            if (leftResource.dataBody() != rightResource.dataBody()) {
                error = QStringLiteral("left and right resources' data bodies don't match");
                return false;
            }

            if (leftResource.hasWidth() != rightResource.hasWidth()) {
                error = QStringLiteral("left resource has width = ") +
                        (leftResource.hasWidth() ? QStringLiteral("true") : QStringLiteral("false")) +
                        QStringLiteral(", right resource has width = ") +
                        (rightResource.hasWidth() ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            if (leftResource.hasWidth() && (leftResource.width() != rightResource.width())) {
                error = QStringLiteral("left resource width = ") + QString::number(leftResource.width()) +
                        QStringLiteral(", right resource width = ") + QString::number(rightResource.width());
                return false;
            }

            if (leftResource.hasHeight() != rightResource.hasHeight()) {
                error = QStringLiteral("left resource has height = ") +
                        (leftResource.hasHeight() ? QStringLiteral("true") : QStringLiteral("false")) +
                        QStringLiteral(", right resource has height = ") +
                        (rightResource.hasHeight() ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            if (leftResource.hasHeight() && (leftResource.height() != rightResource.height())) {
                error = QStringLiteral("left resource height = ") + QString::number(leftResource.height()) +
                        QStringLiteral(", right resource height = ") + QString::number(rightResource.height());
                return false;
            }

            if (leftResource.hasRecognitionDataBody() != rightResource.hasRecognitionDataBody()) {
                error = QStringLiteral("left resource has recognition data body = ") +
                        (leftResource.hasRecognitionDataBody() ? QStringLiteral("true") : QStringLiteral("false")) +
                        QStringLiteral(", right resource has recognition data body = ") +
                        (rightResource.hasRecognitionDataBody() ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            if (leftResource.hasRecognitionDataBody() && (leftResource.recognitionDataBody() != rightResource.recognitionDataBody())) {
                error = QStringLiteral("left and right resources' recognition data bodies don't match");
                return false;
            }

            if (leftResource.hasAlternateDataBody() != rightResource.hasAlternateDataBody()) {
                error = QStringLiteral("left resource has alternate data body = ") +
                        (leftResource.hasAlternateDataBody() ? QStringLiteral("true") : QStringLiteral("false")) +
                        QStringLiteral(", right resource has alternate data body = ") +
                        (rightResource.hasAlternateDataBody() ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            if (leftResource.hasAlternateDataBody() && (leftResource.alternateDataBody() != rightResource.alternateDataBody())) {
                error = QStringLiteral("left and right resources' alternate data bodies don't match");
                return false;
            }

            if (leftResource.hasResourceAttributes() != rightResource.hasResourceAttributes()) {
                error = QStringLiteral("left resource has resource attributes = ") +
                        (leftResource.hasResourceAttributes() ? QStringLiteral("true") : QStringLiteral("false")) +
                        QStringLiteral(", right resource has resource attributes = ") +
                        (rightResource.hasResourceAttributes() ? QStringLiteral("true") : QStringLiteral("false"));
                return false;
            }

            if (leftResource.hasResourceAttributes())
            {
                const qevercloud::ResourceAttributes & leftResourceAttributes = leftResource.resourceAttributes();
                const qevercloud::ResourceAttributes & rightResourceAttributes = rightResource.resourceAttributes();

#define CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftResourceAttributes.attrName.isSet() != rightResourceAttributes.attrName.isSet()) { \
            error = QStringLiteral("left resource: has " #attrName " = ") + \
                    (leftResourceAttributes.attrName.isSet() ? QStringLiteral("true") : QStringLiteral("false")) + \
                    QStringLiteral(", right resource: has " #attrName " = ") + \
                    (rightResourceAttributes.attrName.isSet() ? QStringLiteral("true") : QStringLiteral("false")); \
            return false; \
        }

#define CHECK_RESOURCE_DOUBLE_ATTRIBUTE(attrName) \
        CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftResourceAttributes.attrName.isSet() && \
            (std::fabs(leftResourceAttributes.attrName.ref() - rightResourceAttributes.attrName.ref()) > 1.0e-9)) \
        { \
            error = QStringLiteral("left resource: " #attrName " = ") + QString::number(leftResourceAttributes.attrName.ref()) + \
                    QStringLiteral(", right resource: " #attrName " = ") + QString::number(rightResourceAttributes.attrName.ref()); \
            return false; \
        }

#define CHECK_RESOURCE_STRING_ATTRIBUTE(attrName) \
        CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftResourceAttributes.attrName.isSet() && (leftResourceAttributes.attrName.ref() != rightResourceAttributes.attrName.ref())) { \
            error = QStringLiteral("left resource: " #attrName " = ") + leftResourceAttributes.attrName.ref() + \
                    QStringLiteral(", right resource: " #attrName " = ") + rightResourceAttributes.attrName.ref(); \
            return false; \
        }

#define CHECK_RESOURCE_INTEGER_ATTRIBUTE(attrName) \
        CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftResourceAttributes.attrName.isSet() && (leftResourceAttributes.attrName.ref() != rightResourceAttributes.attrName.ref())) { \
            error = QStringLiteral("left resource: " #attrName " = ") + QString::number(leftResourceAttributes.attrName.ref()) + \
                    QStringLiteral(", right resource: " #attrName " = ") + QString::number(rightResourceAttributes.attrName.ref()); \
            return false; \
        }

#define CHECK_RESOURCE_BOOLEAN_ATTRIBUTE(attrName) \
        CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName) \
        if (leftResourceAttributes.attrName.isSet() && (leftResourceAttributes.attrName.ref() != rightResourceAttributes.attrName.ref())) { \
            error = QStringLiteral("left resource: " #attrName " = ") + QString::number(leftResourceAttributes.attrName.ref()) + \
                    QStringLiteral(", right resource: " #attrName " = ") + QString::number(rightResourceAttributes.attrName.ref()); \
            return false; \
        }

                CHECK_RESOURCE_STRING_ATTRIBUTE(sourceURL)
                CHECK_RESOURCE_INTEGER_ATTRIBUTE(timestamp)
                CHECK_RESOURCE_DOUBLE_ATTRIBUTE(latitude)
                CHECK_RESOURCE_DOUBLE_ATTRIBUTE(longitude)
                CHECK_RESOURCE_DOUBLE_ATTRIBUTE(altitude)
                CHECK_RESOURCE_STRING_ATTRIBUTE(cameraMake)
                CHECK_RESOURCE_STRING_ATTRIBUTE(recoType)
                CHECK_RESOURCE_STRING_ATTRIBUTE(fileName)
                CHECK_RESOURCE_BOOLEAN_ATTRIBUTE(attachment)

                CHECK_RESOURCE_ATTRIBUTE_PRESENCE(applicationData)
                if (leftResourceAttributes.applicationData.isSet())
                {
                    const qevercloud::LazyMap & leftLazyMap = leftResourceAttributes.applicationData.ref();
                    const qevercloud::LazyMap & rightLazyMap = rightResourceAttributes.applicationData.ref();

                    if (leftLazyMap.fullMap.isSet() != rightLazyMap.fullMap.isSet()) {
                        error = QStringLiteral("left resource: application data full map is set = ") +
                            (leftLazyMap.fullMap.isSet() ? QStringLiteral("true") : QStringLiteral("false")) +
                            QStringLiteral(", right resource: application data full map is set = ") +
                            (rightLazyMap.fullMap.isSet() ? QStringLiteral("true") : QStringLiteral("false"));
                        return false;
                    }

                    if (leftLazyMap.fullMap.isSet() && (leftLazyMap.fullMap.ref() != rightLazyMap.fullMap.ref())) {
                        error = QStringLiteral("left and right resources' application data full maps are not equal");
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

bool compareNotes(const QVector<Note> & notes, const QVector<Note> & importedNotes, QString & error)
{
    int numNotes = notes.size();

    if (numNotes != importedNotes.size()) {
        error = QStringLiteral("The number of original and imported notes doesn't match");
        return false;
    }

    for(int i = 0; i < numNotes; ++i)
    {
        const Note & originalNote = notes.at(i);
        const Note & importedNote = importedNotes.at(i);

        bool res = compareNoteContents(originalNote, importedNote, error);
        if (!res) {
            return false;
        }
    }

    return true;
}

void setupSampleNote(Note & note)
{
    note.setTitle(QStringLiteral("Simple note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // NOTE: rounding the timestamp to ensure the msec would all be zero
    timestamp /= 1000;
    timestamp *= 1000;

    note.setCreationTimestamp(timestamp);
    note.setModificationTimestamp(timestamp);

    qevercloud::NoteAttributes & noteAttributes = note.noteAttributes();
    noteAttributes.source = QStringLiteral("The magnificent author");
    noteAttributes.author = QStringLiteral("Very cool guy");
    noteAttributes.placeName = QStringLiteral("bathroom");
    noteAttributes.contentClass = QStringLiteral("average");
    noteAttributes.subjectDate = timestamp;
}

void setupNoteTags(Note & note, QHash<QString, QString> & tagNamesByTagLocalUids)
{
    Tag tag1, tag2, tag3;
    tag1.setName(QStringLiteral("First tag"));
    tag2.setName(QStringLiteral("Second tag"));
    tag3.setName(QStringLiteral("Third tag"));

    note.addTagLocalUid(tag1.localUid());
    note.addTagLocalUid(tag2.localUid());
    note.addTagLocalUid(tag3.localUid());

    tagNamesByTagLocalUids[tag1.localUid()] = tag1.name();
    tagNamesByTagLocalUids[tag2.localUid()] = tag2.name();
    tagNamesByTagLocalUids[tag3.localUid()] = tag3.name();
}

void bindTagsWithNotes(QVector<Note> & importedNotes, const QHash<QString, QStringList> & tagNamesByNoteLocalUid,
                       const QHash<QString, QString> & tagNamesByTagLocalUids)
{
    for(auto it = importedNotes.begin(), end = importedNotes.end(); it != end; ++it)
    {
        Note & note = *it;
        auto tagIt = tagNamesByNoteLocalUid.find(note.localUid());
        if (tagIt == tagNamesByNoteLocalUid.end()) {
            continue;
        }

        const QStringList & tagNames = tagIt.value();
        for(auto tagNameIt = tagNames.constBegin(), tagNameEnd = tagNames.constEnd(); tagNameIt != tagNameEnd; ++tagNameIt)
        {
            const QString & tagName = *tagNameIt;

            // Linear search, not nice but ok on this tiny set
            for(auto tagNamesByTagLocalUidsIt = tagNamesByTagLocalUids.constBegin(),
                tagNamesByTagLocalUidsEnd = tagNamesByTagLocalUids.constEnd();
                tagNamesByTagLocalUidsIt != tagNamesByTagLocalUidsEnd; ++tagNamesByTagLocalUidsIt)
            {
                if (tagNamesByTagLocalUidsIt.value() == tagName) {
                    note.addTagLocalUid(tagNamesByTagLocalUidsIt.key());
                }
            }
        }
    }
}

bool setupNoteResources(Note & note, QString & error)
{
    Resource firstResource;

    QString sampleDataBody = QStringLiteral("XXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
    firstResource.setDataBody(sampleDataBody.toLocal8Bit());
    firstResource.setDataHash(QCryptographicHash::hash(firstResource.dataBody(), QCryptographicHash::Md5));
    firstResource.setDataSize(firstResource.dataBody().size());

    firstResource.setMime("application/text-plain");

    qevercloud::ResourceAttributes & firstResourceAttributes = firstResource.resourceAttributes();

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // NOTE: rounding the timestamp to ensure the msec would all be zero
    timestamp /= 1000;
    timestamp *= 1000;

    firstResourceAttributes.timestamp = timestamp;
    firstResourceAttributes.cameraMake = QStringLiteral("Canon. Or Nixon");
    firstResourceAttributes.fileName = QStringLiteral("Huh?");
    firstResourceAttributes.attachment = false;

    Resource secondResource;

    QFile imageResourceFile(QStringLiteral(":/tests/life_to_blame.jpg"));
    bool res = imageResourceFile.open(QIODevice::ReadOnly);
    if (Q_UNLIKELY(!res)) {
        error = QStringLiteral("Failed to open the qrc resource file with sample image resource data");
        return false;
    }

    QByteArray imageResourceDataBody = imageResourceFile.readAll();

    secondResource.setDataBody(imageResourceDataBody);
    secondResource.setDataHash(QCryptographicHash::hash(imageResourceDataBody, QCryptographicHash::Md5));
    secondResource.setDataSize(imageResourceDataBody.size());

    secondResource.setWidth(640);
    secondResource.setHeight(480);

    secondResource.setMime(QStringLiteral("image/jpg"));

    qevercloud::ResourceAttributes & secondResourceAttributes = secondResource.resourceAttributes();
    secondResourceAttributes.sourceURL = QStringLiteral("https://www.google.ru");
    secondResourceAttributes.fileName = imageResourceFile.fileName();
    secondResourceAttributes.attachment = true;
    secondResourceAttributes.latitude = 53.02;
    secondResourceAttributes.longitude = 43.16;
    secondResourceAttributes.altitude = 28.92;
    secondResourceAttributes.recoType = QStringLiteral("Fake");

    QFile fakeRecognitionDataFile(QStringLiteral(":/tests/recoIndex-all-in-one-example.xml"));
    res = fakeRecognitionDataFile.open(QIODevice::ReadOnly);
    if (Q_UNLIKELY(!res)) {
        error = QStringLiteral("Failed to open the qrc resource file with sample resource recognition data");
        return false;
    }

    QByteArray recognitionDataBody = fakeRecognitionDataFile.readAll();

    secondResource.setRecognitionDataBody(recognitionDataBody);
    secondResource.setRecognitionDataHash(QCryptographicHash::hash(recognitionDataBody, QCryptographicHash::Md5));
    secondResource.setRecognitionDataSize(recognitionDataBody.size());

    QList<Resource> resources;
    resources.reserve(2);
    resources << firstResource;
    resources << secondResource;

    note.setResources(resources);
    return true;
}

} // namespace test
} // namespace quentier
