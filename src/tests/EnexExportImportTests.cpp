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
#include <quentier/types/Resource.h>
#include <quentier/types/ErrorString.h>
#include <QHash>
#include <cmath>

namespace quentier {
namespace test {

bool compareNoteContents(const Note & lhs, const Note & rhs, QString & error);

bool exportSingleNoteWithoutTagsAndResourcesToEnexAndImportBack(QString & error)
{
    Note note;
    note.setTitle(QStringLiteral("Simple note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    note.setCreationTimestamp(timestamp);
    note.setModificationTimestamp(timestamp);

    qevercloud::NoteAttributes & noteAttributes = note.noteAttributes();
    noteAttributes.source = QStringLiteral("The magnificent author");
    noteAttributes.author = QStringLiteral("Very cool guy");
    noteAttributes.placeName = QStringLiteral("bathroom");
    noteAttributes.contentClass = QStringLiteral("average");
    noteAttributes.subjectDate = timestamp;

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

    if (notes.size() != importedNotes.size()) {
        error = QStringLiteral("The number of original and imported notes doesn't match");
        return false;
    }

    int numNotes = notes.size();
    for(int i = 0; i < numNotes; ++i)
    {
        const Note & originalNote = notes.at(i);
        const Note & importedNote = importedNotes.at(i);

        res = compareNoteContents(originalNote, importedNote, error);
        if (!res) {
            return false;
        }
    }

    return true;
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

} // namespace test
} // namespace quentier
