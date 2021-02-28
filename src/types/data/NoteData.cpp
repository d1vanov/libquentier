/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "NoteData.h"

#include <quentier/enml/ENMLConverter.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Note.h>
#include <quentier/utility/Checks.h>
#include <quentier/utility/Compat.h>

#include <QXmlStreamReader>

namespace quentier {

namespace {

////////////////////////////////////////////////////////////////////////////////

void initListFields(qevercloud::Note & note)
{
    note.tagGuids = QList<qevercloud::Guid>();
    note.resources = QList<qevercloud::Resource>();
    note.sharedNotes = QList<qevercloud::SharedNote>();
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

NoteData::NoteData() : FavoritableDataElementData()
{
    initListFields(m_qecNote);
}

NoteData::NoteData(const qevercloud::Note & other) :
    FavoritableDataElementData(), m_qecNote(other), m_resourcesAdditionalInfo(),
    m_notebookLocalUid(), m_tagLocalUids(), m_thumbnailData()
{
    if (!m_qecNote.tagGuids.isSet()) {
        m_qecNote.tagGuids = QList<qevercloud::Guid>();
    }

    if (!m_qecNote.resources.isSet()) {
        m_qecNote.resources = QList<qevercloud::Resource>();
    }
    else {
        const auto & resources = m_qecNote.resources.ref();
        m_resourcesAdditionalInfo.reserve(resources.size());

        for (int i = 0, size = resources.size(); i < size; ++i) {
            m_resourcesAdditionalInfo.push_back(ResourceAdditionalInfo());
            ResourceAdditionalInfo & info = m_resourcesAdditionalInfo.back();
            info.isDirty = false;
            info.localUid = UidGenerator::Generate();
        }
    }

    if (!m_qecNote.sharedNotes.isSet()) {
        m_qecNote.sharedNotes = QList<qevercloud::SharedNote>();
    }
}

bool NoteData::ResourceAdditionalInfo::operator==(
    const NoteData::ResourceAdditionalInfo & other) const
{
    return (localUid == other.localUid) && (isDirty == other.isDirty);
}

bool NoteData::containsToDoImpl(const bool checked) const
{
    if (!m_qecNote.content.isSet()) {
        return false;
    }

    QXmlStreamReader reader(m_qecNote.content.ref());

    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartElement() &&
            (reader.name() == QStringLiteral("en-todo"))) {
            const QXmlStreamAttributes attributes = reader.attributes();
            if (checked && attributes.hasAttribute(QStringLiteral("checked")) &&
                (attributes.value(QStringLiteral("checked")) ==
                 QStringLiteral("true")))
            {
                return true;
            }

            if (!checked &&
                (!attributes.hasAttribute(QStringLiteral("checked")) ||
                 (attributes.value(QStringLiteral("checked")) ==
                  QStringLiteral("false"))))
            {
                return true;
            }
        }
    }

    return false;
}

bool NoteData::containsEncryption() const
{
    if (!m_qecNote.content.isSet()) {
        return false;
    }

    QXmlStreamReader reader(m_qecNote.content.ref());
    while (!reader.atEnd()) {
        Q_UNUSED(reader.readNext());

        if (reader.isStartElement() &&
            (reader.name() == QStringLiteral("en-crypt"))) {
            return true;
        }
    }

    return false;
}

void NoteData::setContent(const QString & content)
{
    if (!content.isEmpty()) {
        m_qecNote.content = content;
    }
    else {
        m_qecNote.content.clear();
    }
}

void NoteData::clear()
{
    m_qecNote = qevercloud::Note();
    initListFields(m_qecNote);

    m_resourcesAdditionalInfo.clear();
    m_notebookLocalUid.clear();
    m_tagLocalUids.clear();
    m_thumbnailData.clear();
}

bool NoteData::checkParameters(ErrorString & errorDescription) const
{
    if (m_qecNote.guid.isSet() && !checkGuid(m_qecNote.guid.ref())) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("NoteData", "Note's guid is invalid"));

        errorDescription.details() = m_qecNote.guid.ref();
        return false;
    }

    if (m_qecNote.updateSequenceNum.isSet() &&
        !checkUpdateSequenceNumber(m_qecNote.updateSequenceNum))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "NoteData", "Note's update sequence number is invalid"));

        errorDescription.details() =
            QString::number(m_qecNote.updateSequenceNum.ref());

        return false;
    }

    if (m_qecNote.title.isSet()) {
        bool res =
            Note::validateTitle(m_qecNote.title.ref(), &errorDescription);
        if (!res) {
            return false;
        }
    }

    if (m_qecNote.content.isSet()) {
        int contentSize = m_qecNote.content->size();

        if ((contentSize < qevercloud::EDAM_NOTE_CONTENT_LEN_MIN) ||
            (contentSize > qevercloud::EDAM_NOTE_CONTENT_LEN_MAX))
        {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "NoteData", "Note's content length is invalid"));

            errorDescription.details() = QString::number(contentSize);
            return false;
        }
    }

    if (m_qecNote.contentHash.isSet()) {
        int contentHashSize = m_qecNote.contentHash->size();

        if (contentHashSize != qevercloud::EDAM_HASH_LEN) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "NoteData", "Note's content hash size is invalid"));

            errorDescription.details() = QString::number(contentHashSize);
            return false;
        }
    }

    if (m_qecNote.notebookGuid.isSet() &&
        !checkGuid(m_qecNote.notebookGuid.ref())) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("NoteData", "Note's notebook guid is invalid"));

        errorDescription.details() = m_qecNote.notebookGuid.ref();
        return false;
    }

    if (m_qecNote.tagGuids.isSet()) {
        int numTagGuids = m_qecNote.tagGuids->size();

        if (numTagGuids > qevercloud::EDAM_NOTE_TAGS_MAX) {
            errorDescription.setBase(
                QT_TRANSLATE_NOOP("NoteData", "Note has too many tags"));

            errorDescription.details() = QString::number(numTagGuids);
            return false;
        }
    }

    if (m_qecNote.resources.isSet()) {
        int numResources = m_qecNote.resources->size();

        if (numResources > qevercloud::EDAM_NOTE_RESOURCES_MAX) {
            errorDescription.setBase(
                QT_TRANSLATE_NOOP("NoteData", "Note has too many resources"));

            errorDescription.details() =
                QString::number(qevercloud::EDAM_NOTE_RESOURCES_MAX);

            return false;
        }
    }

    if (m_qecNote.attributes.isSet()) {
        const qevercloud::NoteAttributes & attributes = m_qecNote.attributes;

        ErrorString error(QT_TRANSLATE_NOOP(
            "NoteData", "Note attributes field has invalid size"));

#define CHECK_NOTE_ATTRIBUTE(name)                                             \
    if (attributes.name.isSet()) {                                             \
        int name##Size = attributes.name->size();                              \
        if ((name##Size < qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||               \
            (name##Size > qevercloud::EDAM_ATTRIBUTE_LEN_MAX))                 \
        {                                                                      \
            error.details() = QStringLiteral(#name);                           \
            errorDescription = error;                                          \
            return false;                                                      \
        }                                                                      \
    }

        CHECK_NOTE_ATTRIBUTE(author);
        CHECK_NOTE_ATTRIBUTE(source);
        CHECK_NOTE_ATTRIBUTE(sourceURL);
        CHECK_NOTE_ATTRIBUTE(sourceApplication);

#undef CHECK_NOTE_ATTRIBUTE

        if (attributes.contentClass.isSet()) {
            int contentClassSize = attributes.contentClass->size();
            if ((contentClassSize <
                 qevercloud::EDAM_NOTE_CONTENT_CLASS_LEN_MIN) ||
                (contentClassSize >
                 qevercloud::EDAM_NOTE_CONTENT_CLASS_LEN_MAX))
            {
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "NoteData",
                    "Note attributes' content class has invalid size"));

                errorDescription.details() = QString::number(contentClassSize);
                return false;
            }
        }

        if (attributes.applicationData.isSet()) {
            const qevercloud::LazyMap & applicationData =
                attributes.applicationData;

            if (applicationData.keysOnly.isSet()) {
                for (const auto & key: qAsConst(applicationData.keysOnly.ref()))
                {
                    int keySize = key.size();
                    if ((keySize <
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) ||
                        (keySize >
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX))
                    {
                        errorDescription.setBase(QT_TRANSLATE_NOOP(
                            "NoteData",
                            "Note's attributes application data "
                            "has invalid key (in keysOnly part)"));

                        errorDescription.details() = key;
                        return false;
                    }
                }
            }

            if (applicationData.fullMap.isSet()) {
                for (const auto it: qevercloud::toRange(
                         qAsConst(applicationData.fullMap.ref())))
                {
                    int keySize = it.key().size();
                    if ((keySize <
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) ||
                        (keySize >
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX))
                    {
                        errorDescription.setBase(QT_TRANSLATE_NOOP(
                            "NoteData",
                            "Note's attributes application data "
                            "has invalid key (in fullMap part)"));

                        errorDescription.details() = it.key();
                        return false;
                    }

                    int valueSize = it.value().size();
                    if ((valueSize <
                         qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MIN) ||
                        (valueSize >
                         qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MAX))
                    {
                        errorDescription.setBase(QT_TRANSLATE_NOOP(
                            "NoteData",
                            "Note's attributes application data "
                            "has invalid value size"));

                        errorDescription.details() = it.value();
                        return false;
                    }

                    int sumSize = keySize + valueSize;
                    if (sumSize >
                        qevercloud::EDAM_APPLICATIONDATA_ENTRY_LEN_MAX) {
                        errorDescription.setBase(QT_TRANSLATE_NOOP(
                            "NoteData",
                            "Note's attributes application data "
                            "has invalid sum entry size"));

                        errorDescription.details() = QString::number(sumSize);
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

QString NoteData::plainText(ErrorString * pErrorMessage) const
{
    if (!m_qecNote.content.isSet()) {
        if (pErrorMessage) {
            pErrorMessage->setBase(
                QT_TRANSLATE_NOOP("NoteData", "Note content is not set"));
        }

        return QString();
    }

    QString plainText;
    ErrorString error;

    bool res = ENMLConverter::noteContentToPlainText(
        m_qecNote.content.ref(), plainText, error);

    if (!res) {
        QNWARNING("types:data", error);
        if (pErrorMessage) {
            *pErrorMessage = error;
        }

        return QString();
    }

    return plainText;
}

QStringList NoteData::listOfWords(ErrorString * pErrorMessage) const
{
    QStringList result;
    ErrorString error;

    bool res = ENMLConverter::noteContentToListOfWords(
        m_qecNote.content.ref(), result, error);

    if (!res) {
        QNWARNING("types:data", error);
        if (pErrorMessage) {
            *pErrorMessage = error;
        }

        return {};
    }

    return result;
}

std::pair<QString, QStringList> NoteData::plainTextAndListOfWords(
    ErrorString * pErrorMessage) const
{
    std::pair<QString, QStringList> result;
    ErrorString error;

    bool res = ENMLConverter::noteContentToListOfWords(
        m_qecNote.content.ref(), result.second, error, &result.first);

    if (!res) {
        QNWARNING("types:data", error);
        if (pErrorMessage) {
            *pErrorMessage = error;
        }

        return {};
    }

    return result;
}

} // namespace quentier
