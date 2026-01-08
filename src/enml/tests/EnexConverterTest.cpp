/*
 * Copyright 2023-2024 Dmitry Ivanov
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

#include <quentier/enml/Factory.h>
#include <quentier/enml/IConverter.h>
#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/enml/IHtmlData.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/NoteUtils.h>

#include <qevercloud/types/Note.h>
#include <qevercloud/types/Tag.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QHash>

#include <gtest/gtest.h>

#include <array>
#include <cmath>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::enml::tests {

namespace {

[[nodiscard]] bool compareNoteContents(
    const qevercloud::Note & lhs, const qevercloud::Note & rhs, QString & error)
{
    if (lhs.title().has_value() != rhs.title().has_value()) {
        error = QStringLiteral("left: has title = ") +
            (lhs.title() ? QStringLiteral("true") : QStringLiteral("false")) +
            QStringLiteral(", right: has title = ") +
            (rhs.title() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.title() && (*lhs.title() != *rhs.title())) {
        error = QStringLiteral("left: title = ") + *lhs.title() +
            QStringLiteral(", right: title = ") + *rhs.title();
        return false;
    }

    if (lhs.content().has_value() != rhs.content().has_value()) {
        error = QStringLiteral("left: has content = ") +
            (lhs.content() ? QStringLiteral("true") : QStringLiteral("false")) +
            QStringLiteral(", right: has title = ") +
            (rhs.content() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.content() && (*lhs.content() != *rhs.content())) {
        error = QStringLiteral("left: content = ") + *lhs.content() +
            QStringLiteral("\n\nRight: content = ") + *rhs.content();
        return false;
    }

    if (lhs.created().has_value() != rhs.created().has_value()) {
        error = QStringLiteral("left: has creation timestamp = ") +
            (lhs.created() ? QStringLiteral("true") : QStringLiteral("false")) +
            QStringLiteral(", right: has creation timestamp = ") +
            (rhs.created() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.created() && (*lhs.created() != *rhs.created())) {
        error = QStringLiteral("left: creation timestamp = ") +
            QString::number(*lhs.created()) +
            QStringLiteral(", right: creation timestamp = ") +
            QString::number(*rhs.created());
        return false;
    }

    if (lhs.updated().has_value() != rhs.updated().has_value()) {
        error = QStringLiteral("left: has modification timestamp = ") +
            (lhs.updated() ? QStringLiteral("true") : QStringLiteral("false")) +
            QStringLiteral(", right: has modification timestamp = ") +
            (rhs.updated() ? QStringLiteral("true") : QStringLiteral("false"));
        return false;
    }

    if (lhs.updated() && (*lhs.updated() != *rhs.updated())) {
        error = QStringLiteral("left: modification timestamp = ") +
            QString::number(*lhs.updated()) +
            QStringLiteral(", right: modification timestamp = ") +
            QString::number(*rhs.updated());
        return false;
    }

    const QStringList & lhsTagLocalIds = lhs.tagLocalIds();
    const QStringList & rhsTagLocalIds = rhs.tagLocalIds();

    if (lhsTagLocalIds.size() != rhsTagLocalIds.size()) {
        error = QStringLiteral(
            "left and right notes has different number "
            "of tag local ids");
        return false;
    }

    for (auto it = lhsTagLocalIds.constBegin(), end = lhsTagLocalIds.constEnd();
         it != end; ++it)
    {
        if (!rhsTagLocalIds.contains(*it)) {
            error = QStringLiteral("left: has tag local uid ") + *it +
                QStringLiteral(" which right doesn't have");
            return false;
        }
    }

    if (lhs.attributes().has_value() != rhs.attributes().has_value()) {
        error = QStringLiteral("left: has note attributes = ") +
            (lhs.attributes() ? QStringLiteral("true")
                              : QStringLiteral("false")) +
            QStringLiteral(", right: has note attributes = ") +
            (rhs.attributes() ? QStringLiteral("true")
                              : QStringLiteral("false"));
        return false;
    }

    if (lhs.attributes()) {
        const auto & leftNoteAttributes = *lhs.attributes();
        const auto & rightNoteAttributes = *rhs.attributes();

#define CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName)                                \
    if (leftNoteAttributes.attrName().has_value() !=                           \
        rightNoteAttributes.attrName().has_value())                            \
    {                                                                          \
        error = QStringLiteral("left: has " #attrName " = ") +                 \
            (leftNoteAttributes.attrName() ? QStringLiteral("true")            \
                                           : QStringLiteral("false")) +        \
            QStringLiteral(", right: has " #attrName " = ") +                  \
            (rightNoteAttributes.attrName() ? QStringLiteral("true")           \
                                            : QStringLiteral("false"));        \
        return false;                                                          \
    }

#define CHECK_NOTE_DOUBLE_ATTRIBUTE(attrName)                                  \
    CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName)                                    \
    if (leftNoteAttributes.attrName() &&                                       \
        (std::fabs(                                                            \
             *leftNoteAttributes.attrName() -                                  \
             *rightNoteAttributes.attrName()) > 1.0e-9))                       \
    {                                                                          \
        error = QStringLiteral("left: " #attrName " = ") +                     \
            QString::number(*leftNoteAttributes.attrName()) +                  \
            QStringLiteral(", right: " #attrName " = ") +                      \
            QString::number(*rightNoteAttributes.attrName());                  \
        return false;                                                          \
    }

#define CHECK_NOTE_STRING_ATTRIBUTE(attrName)                                  \
    CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName)                                    \
    if (leftNoteAttributes.attrName() &&                                       \
        (*leftNoteAttributes.attrName() != *rightNoteAttributes.attrName()))   \
    {                                                                          \
        error = QStringLiteral("left: " #attrName " = ") +                     \
            *leftNoteAttributes.attrName() +                                   \
            QStringLiteral(", right: " #attrName " = ") +                      \
            *rightNoteAttributes.attrName();                                   \
        return false;                                                          \
    }

#define CHECK_NOTE_INTEGER_ATTRIBUTE(attrName)                                 \
    CHECK_NOTE_ATTRIBUTE_PRESENCE(attrName)                                    \
    if (leftNoteAttributes.attrName() &&                                       \
        (leftNoteAttributes.attrName() != rightNoteAttributes.attrName()))     \
    {                                                                          \
        error = QStringLiteral("left: " #attrName " = ") +                     \
            QString::number(*leftNoteAttributes.attrName()) +                  \
            QStringLiteral(", right: " #attrName " = ") +                      \
            QString::number(*rightNoteAttributes.attrName());                  \
        return false;                                                          \
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
        if (leftNoteAttributes.applicationData()) {
            const qevercloud::LazyMap & leftLazyMap =
                *leftNoteAttributes.applicationData();

            const qevercloud::LazyMap & rightLazyMap =
                *rightNoteAttributes.applicationData();

            if (leftLazyMap.fullMap().has_value() !=
                rightLazyMap.fullMap().has_value())
            {
                QTextStream strm(&error);
                strm << "left: application data full map is set = "
                     << (leftLazyMap.fullMap() ? "true" : "false")
                     << ", right: application data full map is set = "
                     << (rightLazyMap.fullMap() ? "true" : "false");
                return false;
            }

            if (leftLazyMap.fullMap() &&
                (*leftLazyMap.fullMap() != *rightLazyMap.fullMap()))
            {
                error = QStringLiteral(
                    "left and right notes' application data "
                    "full maps are not equal");
                return false;
            }
        }
    }

    if (lhs.resources().has_value() != rhs.resources().has_value()) {
        error = QStringLiteral("left: has resources = ") +
            (lhs.resources() ? QStringLiteral("true")
                             : QStringLiteral("false")) +
            QStringLiteral(", right: has resources = ") +
            (rhs.resources() ? QStringLiteral("true")
                             : QStringLiteral("false"));
        return false;
    }

    if (lhs.resources()) {
        auto leftResources = *lhs.resources();
        auto rightResources = *rhs.resources();

        const auto numResources = leftResources.size();
        if (numResources != rightResources.size()) {
            error = QStringLiteral("left note has ") +
                QString::number(numResources) +
                QStringLiteral(" resources while the right one has ") +
                QString::number(rightResources.size()) +
                QStringLiteral(" resources");
            return false;
        }

        for (auto lit = leftResources.constBegin(),
                  lend = leftResources.constEnd(),
                  rit = rightResources.constBegin(),
                  rend = rightResources.constEnd();
             lit != lend && rit != rend; ++lit, ++rit)
        {
            const auto & leftResource = *lit;
            const auto & rightResource = *rit;

            if (Q_UNLIKELY(!leftResource.mime())) {
                error = QStringLiteral("left note's resource has no mime");
                return false;
            }

            if (Q_UNLIKELY(!rightResource.mime())) {
                error = QStringLiteral("right note's resource has no mime");
                return false;
            }

            if (leftResource.mime() != rightResource.mime()) {
                QTextStream strm{&error};
                strm << "left and right resource's mime types don't match: "
                     << "left = " << *leftResource.mime()
                     << ", right = " << *rightResource.mime();
                return false;
            }

            if (Q_UNLIKELY(
                    !(leftResource.data() && leftResource.data()->body())))
            {
                error = QStringLiteral("left note's resource has no data body");
                return false;
            }

            if (Q_UNLIKELY(
                    !(rightResource.data() && rightResource.data()->body())))
            {
                error =
                    QStringLiteral("right note's resource has no data body");
                return false;
            }

            if (*leftResource.data()->body() != *rightResource.data()->body()) {
                error = QStringLiteral(
                    "left and right resources' data bodies don't match");
                return false;
            }

            if (leftResource.width() != rightResource.width()) {
                error = QStringLiteral("left resource has width = ") +
                    (leftResource.width() ? QStringLiteral("true")
                                          : QStringLiteral("false")) +
                    QStringLiteral(", right resource has width = ") +
                    (rightResource.width() ? QStringLiteral("true")
                                           : QStringLiteral("false"));
                return false;
            }

            if (leftResource.width() &&
                (*leftResource.width() != *rightResource.width()))
            {
                error = QStringLiteral("left resource width = ") +
                    QString::number(*leftResource.width()) +
                    QStringLiteral(", right resource width = ") +
                    QString::number(*rightResource.width());
                return false;
            }

            if (leftResource.height() != rightResource.height()) {
                error = QStringLiteral("left resource has height = ") +
                    (leftResource.height() ? QStringLiteral("true")
                                           : QStringLiteral("false")) +
                    QStringLiteral(", right resource has height = ") +
                    (rightResource.height() ? QStringLiteral("true")
                                            : QStringLiteral("false"));
                return false;
            }

            if (leftResource.height() &&
                (*leftResource.height() != *rightResource.height()))
            {
                error = QStringLiteral("left resource height = ") +
                    QString::number(*leftResource.height()) +
                    QStringLiteral(", right resource height = ") +
                    QString::number(*rightResource.height());
                return false;
            }

            if ((leftResource.recognition() &&
                 leftResource.recognition()->body()) !=
                (rightResource.recognition() &&
                 rightResource.recognition()->body()))
            {
                QTextStream strm(&error);
                strm << "left resource has recognition data body = "
                     << ((leftResource.recognition() &&
                          leftResource.recognition()->body())
                             ? "true"
                             : "false")
                     << ", right resource has recognition data body = "
                     << ((rightResource.recognition() &&
                          rightResource.recognition()->body())
                             ? "true"
                             : "false");
                return false;
            }

            if (leftResource.recognition() &&
                leftResource.recognition()->body())
            {
                const auto leftRecognitionBody =
                    QString::fromUtf8(*leftResource.recognition()->body())
                        .simplified();

                const auto rightRecognitionBody =
                    QString::fromUtf8(*rightResource.recognition()->body())
                        .simplified();

                if (leftRecognitionBody != rightRecognitionBody) {
                    error = QStringLiteral(
                        "left and right resources' recognition data bodies "
                        "don't match");
                    return false;
                }
            }

            if ((leftResource.alternateData() &&
                 leftResource.alternateData()->body()) !=
                (rightResource.alternateData() &&
                 rightResource.alternateData()->body()))
            {
                QTextStream strm(&error);
                strm << "left resource has alternate data body = "
                     << ((leftResource.alternateData() &&
                          leftResource.alternateData()->body())
                             ? "true"
                             : "false")
                     << ", right resource has alternate data body = "
                     << ((rightResource.alternateData() &&
                          rightResource.alternateData()->body())
                             ? "true"
                             : "false");
                return false;
            }

            if ((leftResource.alternateData() &&
                 leftResource.alternateData()->body()) &&
                (*leftResource.alternateData()->body() !=
                 *rightResource.alternateData()->body()))
            {
                error = QStringLiteral(
                    "left and right resources' alternate data bodies don't "
                    "match");
                return false;
            }

            if (leftResource.attributes().has_value() !=
                rightResource.attributes().has_value())
            {
                QTextStream strm(&error);
                strm << "left resource has resource attributes = "
                     << (leftResource.attributes() ? "true" : "false")
                     << ", right resource has resource attributes = "
                     << (rightResource.attributes() ? "true" : "false");
                return false;
            }

            if (leftResource.attributes()) {
                const auto & leftResourceAttributes =
                    *leftResource.attributes();

                const auto & rightResourceAttributes =
                    *rightResource.attributes();

#define CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName)                            \
    if (leftResourceAttributes.attrName().has_value() !=                       \
        rightResourceAttributes.attrName().has_value())                        \
    {                                                                          \
        error = QStringLiteral("left resource: has " #attrName " = ") +        \
            (leftResourceAttributes.attrName() ? QStringLiteral("true")        \
                                               : QStringLiteral("false")) +    \
            QStringLiteral(", right resource: has " #attrName " = ") +         \
            (rightResourceAttributes.attrName() ? QStringLiteral("true")       \
                                                : QStringLiteral("false"));    \
        return false;                                                          \
    }

#define CHECK_RESOURCE_DOUBLE_ATTRIBUTE(attrName)                              \
    CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName)                                \
    if (leftResourceAttributes.attrName() &&                                   \
        (std::fabs(                                                            \
             *leftResourceAttributes.attrName() -                              \
             *rightResourceAttributes.attrName()) > 1.0e-9))                   \
    {                                                                          \
        error = QStringLiteral("left resource: " #attrName " = ") +            \
            QString::number(*leftResourceAttributes.attrName()) +              \
            QStringLiteral(", right resource: " #attrName " = ") +             \
            QString::number(*rightResourceAttributes.attrName());              \
        return false;                                                          \
    }

#define CHECK_RESOURCE_STRING_ATTRIBUTE(attrName)                              \
    CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName)                                \
    if (leftResourceAttributes.attrName() &&                                   \
        (*leftResourceAttributes.attrName() !=                                 \
         *rightResourceAttributes.attrName()))                                 \
    {                                                                          \
        error = QStringLiteral("left resource: " #attrName " = ") +            \
            *leftResourceAttributes.attrName() +                               \
            QStringLiteral(", right resource: " #attrName " = ") +             \
            *rightResourceAttributes.attrName();                               \
        return false;                                                          \
    }

#define CHECK_RESOURCE_INTEGER_ATTRIBUTE(attrName)                             \
    CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName)                                \
    if (leftResourceAttributes.attrName() &&                                   \
        (*leftResourceAttributes.attrName() !=                                 \
         *rightResourceAttributes.attrName()))                                 \
    {                                                                          \
        error = QStringLiteral("left resource: " #attrName " = ") +            \
            QString::number(*leftResourceAttributes.attrName()) +              \
            QStringLiteral(", right resource: " #attrName " = ") +             \
            QString::number(*rightResourceAttributes.attrName());              \
        return false;                                                          \
    }

#define CHECK_RESOURCE_BOOLEAN_ATTRIBUTE(attrName)                             \
    CHECK_RESOURCE_ATTRIBUTE_PRESENCE(attrName)                                \
    if (leftResourceAttributes.attrName() &&                                   \
        (*leftResourceAttributes.attrName() !=                                 \
         *rightResourceAttributes.attrName()))                                 \
    {                                                                          \
        error = QStringLiteral("left resource: " #attrName " = ") +            \
            QString::number(*leftResourceAttributes.attrName()) +              \
            QStringLiteral(", right resource: " #attrName " = ") +             \
            QString::number(*rightResourceAttributes.attrName());              \
        return false;                                                          \
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
                if (leftResourceAttributes.applicationData()) {
                    const auto & leftLazyMap =
                        *leftResourceAttributes.applicationData();

                    const auto & rightLazyMap =
                        *rightResourceAttributes.applicationData();

                    if (leftLazyMap.fullMap().has_value() !=
                        rightLazyMap.fullMap().has_value())
                    {
                        QTextStream strm{&error};
                        strm << "left resource: application data "
                             << "full map is set = "
                             << (leftLazyMap.fullMap() ? "true" : "false")
                             << ", right resource: application "
                             << "data full map is set = "
                             << (rightLazyMap.fullMap() ? "true" : "false");
                        return false;
                    }

                    if (leftLazyMap.fullMap() &&
                        (*leftLazyMap.fullMap() != *rightLazyMap.fullMap()))
                    {
                        error = QStringLiteral(
                            "left and right resources' application data full "
                            "maps are not equal");
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

[[nodiscard]] bool compareNotes(
    const QList<qevercloud::Note> & originalNotes,
    const QList<qevercloud::Note> & importedNotes, QString & error)
{
    const auto numNotes = originalNotes.size();
    if (numNotes != importedNotes.size()) {
        error = QStringLiteral(
            "The number of original and imported notes doesn't match");
        return false;
    }

    for (auto oit = originalNotes.constBegin(), oend = originalNotes.constEnd(),
              iit = importedNotes.constBegin(), iend = importedNotes.constEnd();
         oit != oend && iit != iend; ++oit, ++iit)
    {
        const auto & originalNote = *oit;
        const auto & importedNote = *iit;
        if (!compareNoteContents(originalNote, importedNote, error)) {
            return false;
        }
    }

    return true;
}

void setupSampleNote(qevercloud::Note & note)
{
    note.setTitle(QStringLiteral("Simple note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // NOTE: rounding the timestamp to ensure the msec would all be zero
    timestamp /= 1000;
    timestamp *= 1000;

    note.setCreated(timestamp);
    note.setUpdated(timestamp);

    if (!note.attributes()) {
        note.setAttributes(qevercloud::NoteAttributes{});
    }

    auto & noteAttributes = *note.mutableAttributes();
    noteAttributes.setSource(QStringLiteral("The magnificent author"));
    noteAttributes.setAuthor(QStringLiteral("Very cool guy"));
    noteAttributes.setPlaceName(QStringLiteral("bathroom"));
    noteAttributes.setContentClass(QStringLiteral("average"));
    noteAttributes.setSubjectDate(timestamp);
}

void setupSampleNoteV2(qevercloud::Note & note)
{
    note.setTitle(QStringLiteral("My cool note"));
    note.setContent(
        QStringLiteral("<en-note><h2>Rock hard</h2>"
                       "<div>Rock free</div>"
                       "<div>All day, all night</div></en-note>"));

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // NOTE: rounding the timestamp to ensure the msec would all be zero
    timestamp /= 1000;
    timestamp *= 1000;

    note.setCreated(timestamp);
    note.setUpdated(timestamp);

    if (!note.attributes()) {
        note.setAttributes(qevercloud::NoteAttributes{});
    }

    auto & noteAttributes = *note.mutableAttributes();
    noteAttributes.setSubjectDate(timestamp);
    noteAttributes.setLatitude(23.48);
    noteAttributes.setLongitude(72.11);
    noteAttributes.setAltitude(52.36);
    noteAttributes.setAuthor(QStringLiteral("The creator"));
    noteAttributes.setSource(QStringLiteral("Brain"));
    noteAttributes.setSourceURL(QStringLiteral("https://www.google.com"));
    noteAttributes.setSourceApplication(QCoreApplication::applicationName());
    noteAttributes.setReminderOrder(2);
    noteAttributes.setReminderTime(timestamp + 2000);
    noteAttributes.setReminderDoneTime(timestamp + 3000);
    noteAttributes.setPlaceName(QStringLiteral("shower"));
    noteAttributes.setContentClass(QStringLiteral("awesome"));

    noteAttributes.setApplicationData(qevercloud::LazyMap{});

    auto & appData = *noteAttributes.mutableApplicationData();

    appData.setKeysOnly(QSet<QString>{});
    Q_UNUSED(appData.mutableKeysOnly()->insert(QStringLiteral("key1")))
    Q_UNUSED(appData.mutableKeysOnly()->insert(QStringLiteral("key2")))
    Q_UNUSED(appData.mutableKeysOnly()->insert(QStringLiteral("key3")))

    appData.setFullMap(QMap<QString, QString>{});
    (*appData.mutableFullMap())[QStringLiteral("key1")] =
        QStringLiteral("value1");
    (*appData.mutableFullMap())[QStringLiteral("key2")] =
        QStringLiteral("value2");
    (*appData.mutableFullMap())[QStringLiteral("key3")] =
        QStringLiteral("value3");
}

void setupNoteTags(
    qevercloud::Note & note, QHash<QString, QString> & tagNamesByTagLocalIds)
{
    qevercloud::Tag tag1, tag2, tag3;
    tag1.setName(QStringLiteral("First tag"));
    tag2.setName(QStringLiteral("Second tag"));
    tag3.setName(QStringLiteral("Third tag"));

    note.setTagLocalIds(
        QStringList{} << tag1.localId() << tag2.localId() << tag3.localId());

    tagNamesByTagLocalIds[tag1.localId()] = *tag1.name();
    tagNamesByTagLocalIds[tag2.localId()] = *tag2.name();
    tagNamesByTagLocalIds[tag3.localId()] = *tag3.name();
}

void setupNoteTagsV2(
    qevercloud::Note & note, QHash<QString, QString> & tagNamesByTagLocalIds)
{
    qevercloud::Tag tag1, tag2;
    tag1.setName(QStringLiteral("Cool tag"));
    tag2.setName(QStringLiteral("Even cooler tag"));

    note.setTagLocalIds(QStringList{} << tag1.localId() << tag2.localId());

    tagNamesByTagLocalIds[tag1.localId()] = *tag1.name();
    tagNamesByTagLocalIds[tag2.localId()] = *tag2.name();
}

void bindTagsWithNotes(
    QList<qevercloud::Note> & importedNotes,
    const QHash<QString, QString> & tagNamesByTagLocalIds)
{
    for (auto & note: importedNotes) {
        const QList<QString> tagNames =
            note.tagNames().value_or(QList<QString>{});

        for (auto tagNameIt = tagNames.constBegin(),
                  tagNameEnd = tagNames.constEnd();
             tagNameIt != tagNameEnd; ++tagNameIt)
        {
            const QString & tagName = *tagNameIt;

            // Linear search, not nice but ok on this tiny set
            for (auto tagNamesByTagLocalIdsIt =
                          tagNamesByTagLocalIds.constBegin(),
                      tagNamesByTagLocalIdsEnd =
                          tagNamesByTagLocalIds.constEnd();
                 tagNamesByTagLocalIdsIt != tagNamesByTagLocalIdsEnd;
                 ++tagNamesByTagLocalIdsIt)
            {
                if (tagNamesByTagLocalIdsIt.value() == tagName) {
                    QStringList tagLocalIds = note.tagLocalIds();
                    tagLocalIds << tagNamesByTagLocalIdsIt.key();
                    note.setTagLocalIds(tagLocalIds);
                }
            }
        }
    }
}

[[nodiscard]] bool setupNoteResources(qevercloud::Note & note, QString & error)
{
    qevercloud::Resource firstResource;
    firstResource.setData(qevercloud::Data{});

    QString sampleDataBody = QStringLiteral("XXXXXXXXXXXXXXXXXXXXXXXXXXXXX");
    firstResource.mutableData()->setBody(sampleDataBody.toLocal8Bit());

    firstResource.mutableData()->setBodyHash(QCryptographicHash::hash(
        *firstResource.data()->body(), QCryptographicHash::Md5));

    firstResource.mutableData()->setSize(firstResource.data()->body()->size());

    firstResource.setMime(QStringLiteral("application/text-plain"));

    firstResource.setAttributes(qevercloud::ResourceAttributes{});

    auto & firstResourceAttributes = *firstResource.mutableAttributes();

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // NOTE: rounding the timestamp to ensure the msec would all be zero
    timestamp /= 1000;
    timestamp *= 1000;

    firstResourceAttributes.setTimestamp(timestamp);
    firstResourceAttributes.setCameraMake(QStringLiteral("Canon. Or Nixon"));
    firstResourceAttributes.setFileName(QStringLiteral("Huh?"));
    firstResourceAttributes.setAttachment(false);

    qevercloud::Resource secondResource;

    QFile imageResourceFile{QStringLiteral(":/tests/life_to_blame.jpg")};
    bool res = imageResourceFile.open(QIODevice::ReadOnly);
    if (Q_UNLIKELY(!res)) {
        error = QStringLiteral(
            "Failed to open the qrc resource file with "
            "sample image resource data");
        return false;
    }

    QByteArray imageResourceDataBody = imageResourceFile.readAll();

    secondResource.setData(qevercloud::Data{});

    secondResource.mutableData()->setBody(imageResourceDataBody);

    secondResource.mutableData()->setBodyHash(QCryptographicHash::hash(
        imageResourceDataBody, QCryptographicHash::Md5));

    secondResource.mutableData()->setSize(imageResourceDataBody.size());

    secondResource.setWidth(640);
    secondResource.setHeight(480);

    secondResource.setMime(QStringLiteral("image/jpg"));

    secondResource.setAttributes(qevercloud::ResourceAttributes{});
    auto & secondResourceAttributes = *secondResource.mutableAttributes();

    secondResourceAttributes.setSourceURL(
        QStringLiteral("https://www.google.ru"));

    secondResourceAttributes.setFileName(imageResourceFile.fileName());
    secondResourceAttributes.setAttachment(true);
    secondResourceAttributes.setLatitude(53.02);
    secondResourceAttributes.setLongitude(43.16);
    secondResourceAttributes.setAltitude(28.92);
    secondResourceAttributes.setRecoType(QStringLiteral("Fake"));

    QFile fakeRecognitionDataFile{
        QStringLiteral(":/tests/recoIndex-all-in-one-example.xml")};

    res = fakeRecognitionDataFile.open(QIODevice::ReadOnly);
    if (Q_UNLIKELY(!res)) {
        error = QStringLiteral(
            "Failed to open the qrc resource file with "
            "sample resource recognition data");
        return false;
    }

    QByteArray recognitionDataBody = fakeRecognitionDataFile.readAll();

    secondResource.setRecognition(qevercloud::Data{});
    secondResource.mutableRecognition()->setBody(recognitionDataBody);
    secondResource.mutableRecognition()->setBodyHash(
        QCryptographicHash::hash(recognitionDataBody, QCryptographicHash::Md5));
    secondResource.mutableRecognition()->setSize(recognitionDataBody.size());

    QList<qevercloud::Resource> resources;
    resources.reserve(2);
    resources << firstResource;
    resources << secondResource;

    note.setResources(resources);
    return true;
}

void setupNoteResourcesV2(qevercloud::Note & note)
{
    qevercloud::Resource resource;
    resource.setData(qevercloud::Data{});

    QString sampleDataBody =
        QStringLiteral("Suppose this would be some meaningless piece of text");

    resource.mutableData()->setBody(sampleDataBody.toLocal8Bit());

    resource.mutableData()->setBodyHash(QCryptographicHash::hash(
        *resource.data()->body(), QCryptographicHash::Md5));

    resource.mutableData()->setSize(resource.data()->body()->size());

    resource.setMime(QStringLiteral("application/text-plain"));

    resource.setAttributes(qevercloud::ResourceAttributes{});
    auto & resourceAttributes = *resource.mutableAttributes();

    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    // NOTE: rounding the timestamp to ensure the msec would all be zero
    timestamp /= 1000;
    timestamp *= 1000;

    resourceAttributes.setSourceURL(QStringLiteral("https://www.google.com"));
    resourceAttributes.setTimestamp(timestamp);
    resourceAttributes.setLatitude(52.43);
    resourceAttributes.setLongitude(23.46);
    resourceAttributes.setAltitude(82.13);
    resourceAttributes.setCameraMake(QStringLiteral("something"));
    resourceAttributes.setFileName(QStringLiteral("None"));
    resourceAttributes.setAttachment(true);

    resourceAttributes.setApplicationData(qevercloud::LazyMap{});
    auto & appData = *resourceAttributes.mutableApplicationData();

    appData.setKeysOnly(QSet<QString>{});
    Q_UNUSED(appData.mutableKeysOnly()->insert(QStringLiteral("resKey1")))
    Q_UNUSED(appData.mutableKeysOnly()->insert(QStringLiteral("resKey2")))
    Q_UNUSED(appData.mutableKeysOnly()->insert(QStringLiteral("resKey3")))
    Q_UNUSED(appData.mutableKeysOnly()->insert(QStringLiteral("resKey4")))

    appData.setFullMap(QMap<QString, QString>{});

    (*appData.mutableFullMap())[QStringLiteral("resKey1")] =
        QStringLiteral("resVal1");

    (*appData.mutableFullMap())[QStringLiteral("resKey2")] =
        QStringLiteral("resVal2");

    (*appData.mutableFullMap())[QStringLiteral("resKey3")] =
        QStringLiteral("resVal3");

    (*appData.mutableFullMap())[QStringLiteral("resKey4")] =
        QStringLiteral("resVal4");

    note.setResources(QList<qevercloud::Resource>() << resource);
}

} // namespace

class EnexConverterTest : public testing::Test
{
protected:
    const IConverterPtr m_converter = createConverter();
};

TEST_F(
    EnexConverterTest,
    ExportSingleNoteWithoutTagsAndResourcesToEnexAndImportBack)
{
    qevercloud::Note note;
    setupSampleNote(note);

    const auto notes = QList<qevercloud::Note>{} << note;
    QHash<QString, QString> tagNamesByTagLocalIds;

    auto exportRes = m_converter->exportNotesToEnex(
        notes, tagNamesByTagLocalIds, IConverter::EnexExportTags::Yes);
    ASSERT_TRUE(exportRes.isValid())
        << exportRes.error().nonLocalizedString().toStdString();

    auto importRes = m_converter->importEnex(exportRes.get());
    ASSERT_TRUE(importRes.isValid())
        << importRes.error().nonLocalizedString().toStdString();

    QString error;
    const bool res = compareNotes(notes, importRes.get(), error);
    EXPECT_TRUE(res) << error.toStdString();
}

TEST_F(
    EnexConverterTest,
    ExportSingleNoteWithTagsButNoResourcesToEnexAndImportBack)
{
    qevercloud::Note note;
    setupSampleNote(note);

    QHash<QString, QString> tagNamesByTagLocalIds;
    setupNoteTags(note, tagNamesByTagLocalIds);

    const auto notes = QList<qevercloud::Note>{} << note;

    auto exportRes = m_converter->exportNotesToEnex(
        notes, tagNamesByTagLocalIds, IConverter::EnexExportTags::Yes);
    ASSERT_TRUE(exportRes.isValid())
        << exportRes.error().nonLocalizedString().toStdString();

    auto importRes = m_converter->importEnex(exportRes.get());
    ASSERT_TRUE(importRes.isValid())
        << importRes.error().nonLocalizedString().toStdString();

    bindTagsWithNotes(importRes.get(), tagNamesByTagLocalIds);

    QString error;
    const bool res = compareNotes(notes, importRes.get(), error);
    EXPECT_TRUE(res) << error.toStdString();
}

TEST_F(
    EnexConverterTest,
    exportSingleNoteWithResourcesButNoTagsToEnexAndImportBack)
{
    qevercloud::Note note;
    setupSampleNote(note);

    {
        QString error;
        ASSERT_TRUE(setupNoteResources(note, error)) << error.toStdString();
    }

    const auto notes = QList<qevercloud::Note>{} << note;
    QHash<QString, QString> tagNamesByTagLocalIds;

    auto exportRes = m_converter->exportNotesToEnex(
        notes, tagNamesByTagLocalIds, IConverter::EnexExportTags::Yes);
    ASSERT_TRUE(exportRes.isValid())
        << exportRes.error().nonLocalizedString().toStdString();

    auto importRes = m_converter->importEnex(exportRes.get());
    ASSERT_TRUE(importRes.isValid())
        << importRes.error().nonLocalizedString().toStdString();

    QString error;
    const bool res = compareNotes(notes, importRes.get(), error);
    EXPECT_TRUE(res) << error.toStdString();
}

TEST_F(
    EnexConverterTest, ExportSingleNoteWithTagsAndResourcesToEnexAndImportBack)
{
    qevercloud::Note note;
    setupSampleNote(note);

    {
        QString error;
        ASSERT_TRUE(setupNoteResources(note, error)) << error.toStdString();
    }

    QHash<QString, QString> tagNamesByTagLocalIds;
    setupNoteTags(note, tagNamesByTagLocalIds);

    const auto notes = QList<qevercloud::Note>{} << note;

    auto exportRes = m_converter->exportNotesToEnex(
        notes, tagNamesByTagLocalIds, IConverter::EnexExportTags::Yes);
    ASSERT_TRUE(exportRes.isValid())
        << exportRes.error().nonLocalizedString().toStdString();

    auto importRes = m_converter->importEnex(exportRes.get());
    ASSERT_TRUE(importRes.isValid())
        << importRes.error().nonLocalizedString().toStdString();

    bindTagsWithNotes(importRes.get(), tagNamesByTagLocalIds);

    QString error;
    const bool res = compareNotes(notes, importRes.get(), error);
    EXPECT_TRUE(res) << error.toStdString();
}

TEST_F(
    EnexConverterTest, ExportSingleNoteWithTagsToEnexButSkipTagsAndImportBack)
{
    qevercloud::Note note;
    setupSampleNote(note);

    QHash<QString, QString> tagNamesByTagLocalIds;
    setupNoteTags(note, tagNamesByTagLocalIds);

    auto notes = QList<qevercloud::Note>{} << note;

    auto exportRes = m_converter->exportNotesToEnex(
        notes, tagNamesByTagLocalIds, IConverter::EnexExportTags::No);
    ASSERT_TRUE(exportRes.isValid())
        << exportRes.error().nonLocalizedString().toStdString();

    auto importRes = m_converter->importEnex(exportRes.get());
    ASSERT_TRUE(importRes.isValid())
        << importRes.error().nonLocalizedString().toStdString();

    ASSERT_EQ(importRes.get().size(), 1);
    ASSERT_FALSE(importRes.get()[0].tagNames());

    notes[0].setTagLocalIds(QStringList{});

    QString error;
    const bool res = compareNotes(notes, importRes.get(), error);
    EXPECT_TRUE(res) << error.toStdString();
}

TEST_F(EnexConverterTest, ExportMultipleNotesWithTagsAndResourcesAndImportBack)
{
    qevercloud::Note firstNote;
    setupSampleNote(firstNote);

    qevercloud::Note secondNote;
    setupSampleNoteV2(secondNote);

    qevercloud::Note thirdNote;
    thirdNote.setContent(
        QStringLiteral("<en-note><h1>Quick note</h1></en-note>"));

    QHash<QString, QString> tagNamesByTagLocalIds;
    setupNoteTags(firstNote, tagNamesByTagLocalIds);
    setupNoteTagsV2(secondNote, tagNamesByTagLocalIds);

    {
        QString error;
        ASSERT_TRUE(setupNoteResources(thirdNote, error))
            << error.toStdString();
    }

    setupNoteResourcesV2(secondNote);

    auto notes = QList<qevercloud::Note>{} << firstNote << secondNote
                                           << thirdNote;

    auto exportRes = m_converter->exportNotesToEnex(
        notes, tagNamesByTagLocalIds, IConverter::EnexExportTags::Yes);
    ASSERT_TRUE(exportRes.isValid())
        << exportRes.error().nonLocalizedString().toStdString();

    auto importRes = m_converter->importEnex(exportRes.get());
    ASSERT_TRUE(importRes.isValid())
        << importRes.error().nonLocalizedString().toStdString();

    bindTagsWithNotes(importRes.get(), tagNamesByTagLocalIds);

    QString error;
    const bool res = compareNotes(notes, importRes.get(), error);
    EXPECT_TRUE(res) << error.toStdString();
}

class ImportEnexTest :
    public EnexConverterTest,
    public testing::WithParamInterface<int>
{};

constexpr std::array gSampleEnexIndexes{1, 2, 3, 4};

INSTANTIATE_TEST_SUITE_P(
    ImportEnexTestInstance, ImportEnexTest,
    testing::ValuesIn(gSampleEnexIndexes));

TEST_P(ImportEnexTest, ImportEnex)
{
    QFile sampleEnexFile{
        QString::fromUtf8(":/tests/SampleEnex%1.enex").arg(GetParam())};

    ASSERT_TRUE(sampleEnexFile.open(QIODevice::ReadOnly));
    const auto sampleEnex = QString::fromUtf8(sampleEnexFile.readAll());

    const auto res = m_converter->importEnex(sampleEnex);
    ASSERT_TRUE(res.isValid())
        << res.error().nonLocalizedString().toStdString();

    ASSERT_EQ(res.get().size(), 1);
}

} // namespace quentier::enml::tests
