/*
 * Copyright 2023 Dmitry Ivanov
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

#include "../utils/ExceptionUtils.h"
#include "Checks.h"

#include <qevercloud/Constants.h>
#include <qevercloud/exceptions/builders/EDAMUserExceptionBuilder.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Tag.h>

#include <QRegularExpression>

namespace quentier::synchronization::tests::note_store {

namespace {

[[nodiscard]] std::optional<qevercloud::EDAMUserException> checkAppDataKey(
    const QString & key, const QRegularExpression & keyRegExp)
{
    if (key.size() < qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::LIMIT_REACHED,
            QStringLiteral("ApplicationData"));
    }

    if (key.size() > qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::LIMIT_REACHED,
            QStringLiteral("ApplicationData"));
    }

    if (!keyRegExp.match(key).hasMatch()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::LIMIT_REACHED,
            QStringLiteral("ApplicationData"));
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<qevercloud::EDAMUserException> checkAppData(
    const qevercloud::LazyMap & appData)
{
    static const QRegularExpression keyRegExp{
        qevercloud::EDAM_APPLICATIONDATA_NAME_REGEX};

    static const QRegularExpression valueRegExp{
        qevercloud::EDAM_APPLICATIONDATA_VALUE_REGEX};

    if (appData.keysOnly()) {
        if (appData.keysOnly()->size() > qevercloud::EDAM_ATTRIBUTE_LIST_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("ApplicationData"));
        }

        for (auto it = appData.keysOnly()->constBegin(),
                  end = appData.keysOnly()->constEnd();
             it != end; ++it)
        {
            const QString & key = *it;
            if (auto exc = checkAppDataKey(key, keyRegExp)) {
                return exc;
            }
        }
    }

    if (appData.fullMap()) {
        if (appData.fullMap()->size() > qevercloud::EDAM_ATTRIBUTE_MAP_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("ApplicationData"));
        }

        for (auto it = appData.fullMap()->constBegin(),
                  end = appData.fullMap()->constEnd();
             it != end; ++it)
        {
            const QString & key = it.key();
            if (auto exc = checkAppDataKey(key, keyRegExp)) {
                return exc;
            }

            const QString & value = it.value();

            if (value.size() < qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MIN) {
                return utils::createUserException(
                    qevercloud::EDAMErrorCode::LIMIT_REACHED,
                    QStringLiteral("ApplicationData"));
            }

            if (value.size() > qevercloud::EDAM_APPLICATIONDATA_VALUE_LEN_MAX) {
                return utils::createUserException(
                    qevercloud::EDAMErrorCode::LIMIT_REACHED,
                    QStringLiteral("ApplicationData"));
            }

            if (!valueRegExp.match(value).hasMatch()) {
                return utils::createUserException(
                    qevercloud::EDAMErrorCode::LIMIT_REACHED,
                    QStringLiteral("ApplicationData"));
            }
        }
    }

    return std::nullopt;
}

} // namespace

std::optional<qevercloud::EDAMUserException> checkNotebook(
    const qevercloud::Notebook & notebook)
{
    if (!notebook.name()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    const QString & notebookName = *notebook.name();
    if (notebookName.size() < qevercloud::EDAM_NOTEBOOK_NAME_LEN_MIN) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    if (notebookName.size() > qevercloud::EDAM_NOTEBOOK_NAME_LEN_MAX) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    if (notebookName != notebookName.trimmed()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    static const QRegularExpression notebookNameRegex{
        qevercloud::EDAM_NOTEBOOK_NAME_REGEX};

    if (!notebookNameRegex.match(notebookName).hasMatch()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Notebook.name"));
    }

    if (notebook.stack()) {
        const QString & notebookStack = *notebook.stack();

        if (notebookStack.size() < qevercloud::EDAM_NOTEBOOK_STACK_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }

        if (notebookStack.size() > qevercloud::EDAM_NOTEBOOK_STACK_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }

        if (notebookStack != notebookStack.trimmed()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }

        static const QRegularExpression notebookStackRegex{
            qevercloud::EDAM_NOTEBOOK_STACK_REGEX};

        if (!notebookStackRegex.match(notebookStack).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Notebook.stack"));
        }
    }

    if (notebook.published() && notebook.publishing()->uri()) {
        const QString & publishingUri = *notebook.publishing()->uri();

        if (publishingUri.size() < qevercloud::EDAM_PUBLISHING_URI_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.uri"));
        }

        if (publishingUri.size() > qevercloud::EDAM_PUBLISHING_URI_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.uri"));
        }

        for (const auto & it: qevercloud::EDAM_PUBLISHING_URI_PROHIBITED) {
            if (publishingUri == it) {
                return utils::createUserException(
                    qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                    QStringLiteral("Publishing.uri"));
            }
        }

        static const QRegularExpression publishingUriRegExp{
            qevercloud::EDAM_PUBLISHING_URI_REGEX};

        if (!publishingUriRegExp.match(publishingUri).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.uri"));
        }
    }

    if (notebook.publishing() && notebook.publishing()->publicDescription()) {
        const QString & description =
            *notebook.publishing()->publicDescription();

        if (description.size() <
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.publicDescription"));
        }

        if (description.size() >
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.publicDescription"));
        }

        static const QRegularExpression publishingDescriptionRegExp{
            qevercloud::EDAM_PUBLISHING_DESCRIPTION_REGEX};

        if (!publishingDescriptionRegExp.match(description).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Publishing.publicDescription"));
        }
    }

    return std::nullopt;
}

std::optional<qevercloud::EDAMUserException> checkNote(
    const qevercloud::Note & note, const quint32 maxNumResourcesPerNote,
    const quint32 maxTagsPerNote)
{
    if (note.title()) {
        const QString & title = *note.title();

        if (title.size() < qevercloud::EDAM_NOTE_TITLE_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Note.title"));
        }

        if (title.size() > qevercloud::EDAM_NOTE_TITLE_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Note.title"));
        }

        static const QRegularExpression noteTitleRegExp{
            qevercloud::EDAM_NOTE_TITLE_REGEX};

        if (!noteTitleRegExp.match(title).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Note.title"));
        }
    }

    if (note.content()) {
        const QString & content = *note.content();

        if (content.size() < qevercloud::EDAM_NOTE_CONTENT_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Note.content"));
        }

        if (content.size() > qevercloud::EDAM_NOTE_CONTENT_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Note.content"));
        }
    }

    if (note.tagGuids() &&
        note.tagGuids()->size() > static_cast<int>(maxTagsPerNote))
    {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::LIMIT_REACHED,
            QStringLiteral("Note.tagGuids"));
    }

    if (note.active().value_or(false) && note.deleted()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::DATA_CONFLICT,
            QStringLiteral("Note.deleted"));
    }

    if (note.resources() && !note.resources()->isEmpty()) {
        const auto & resources = *note.resources();
        if (resources.size() > static_cast<int>(maxNumResourcesPerNote)) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("Note.resources"));
        }

        for (const auto & resource: qAsConst(resources)) {
            if (!resource.data() || !resource.data()->body()) {
                return utils::createUserException(
                    qevercloud::EDAMErrorCode::DATA_REQUIRED,
                    QStringLiteral("Resource.data"));
            }

            if (resource.data()->body()->size() >
                qevercloud::EDAM_RESOURCE_SIZE_MAX_FREE) {
                return utils::createUserException(
                    qevercloud::EDAMErrorCode::LIMIT_REACHED,
                    QStringLiteral("Resource.data.size"));
            }

            if (resource.mime()) {
                const QString & mime = *resource.mime();

                if (mime.size() < qevercloud::EDAM_MIME_LEN_MIN) {
                    return utils::createUserException(
                        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                        QStringLiteral("Resource.mime"));
                }

                if (mime.size() > qevercloud::EDAM_MIME_LEN_MAX) {
                    return utils::createUserException(
                        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                        QStringLiteral("Resource.mime"));
                }

                static const QRegularExpression mimeRegExp{
                    qevercloud::EDAM_MIME_REGEX};

                if (!mimeRegExp.match(mime).hasMatch()) {
                    return utils::createUserException(
                        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                        QStringLiteral("Resource.mime"));
                }
            }

            if (resource.attributes()) {
                const auto & attributes = *resource.attributes();

                if (attributes.sourceURL() &&
                    ((attributes.sourceURL()->size() <
                      qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
                     (attributes.sourceURL()->size() >
                      qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
                {
                    return utils::createUserException(
                        qevercloud::EDAMErrorCode::LIMIT_REACHED,
                        QStringLiteral("ResourceAttribute.sourceURL"));
                }

                if (attributes.cameraMake() &&
                    ((attributes.cameraMake()->size() <
                      qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
                     (attributes.cameraMake()->size() >
                      qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
                {
                    return utils::createUserException(
                        qevercloud::EDAMErrorCode::LIMIT_REACHED,
                        QStringLiteral("ResourceAttribute.cameraMake"));
                }

                if (attributes.cameraModel() &&
                    ((attributes.cameraModel()->size() <
                      qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
                     (attributes.cameraModel()->size() >
                      qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
                {
                    return utils::createUserException(
                        qevercloud::EDAMErrorCode::LIMIT_REACHED,
                        QStringLiteral("ResourceAttribute.cameraModel"));
                }

                if (attributes.applicationData()) {
                    if (auto exc = checkAppData(*attributes.applicationData()))
                    {
                        return exc;
                    }
                }
            }
        }
    }

    if (note.attributes()) {
        const auto & attributes = *note.attributes();

        if (attributes.author() &&
            ((attributes.author()->size() <
              qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
             (attributes.author()->size() >
              qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
        {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("NoteAttribute.author"));
        }

        if (attributes.source() &&
            ((attributes.source()->size() <
              qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
             (attributes.source()->size() >
              qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
        {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("NoteAttribute.source"));
        }

        if (attributes.sourceURL() &&
            ((attributes.sourceURL()->size() <
              qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
             (attributes.sourceURL()->size() >
              qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
        {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("NoteAttribute.sourceURL"));
        }

        if (attributes.sourceApplication() &&
            ((attributes.sourceApplication()->size() <
              qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
             (attributes.sourceApplication()->size() >
              qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
        {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("NoteAttribute.sourceApplication"));
        }

        if (attributes.placeName() &&
            ((attributes.placeName()->size() <
              qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
             (attributes.placeName()->size() >
              qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
        {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("NoteAttribute.placeName"));
        }

        if (attributes.contentClass() &&
            ((attributes.contentClass()->size() <
              qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
             (attributes.contentClass()->size() >
              qevercloud::EDAM_ATTRIBUTE_LEN_MAX)))
        {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::LIMIT_REACHED,
                QStringLiteral("NoteAttribute.contentClass"));
        }

        if (attributes.applicationData()) {
            if (auto exc = checkAppData(*attributes.applicationData())) {
                return exc;
            }
        }
    }

    return std::nullopt;
}

std::optional<qevercloud::EDAMUserException> checkTag(
    const qevercloud::Tag & tag)
{
    if (!tag.name()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Tag.name"));
    }

    const auto & name = *tag.name();

    if (name.size() < qevercloud::EDAM_TAG_NAME_LEN_MIN) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Tag.name"));
    }

    if (name.size() > qevercloud::EDAM_TAG_NAME_LEN_MAX) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Tag.name"));
    }

    static const QRegularExpression tagNameRegExp{
        qevercloud::EDAM_TAG_NAME_REGEX};

    if (!tagNameRegExp.match(name).hasMatch()) {
        return utils::createUserException(
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
            QStringLiteral("Tag.name"));
    }

    if (tag.parentGuid()) {
        const auto & parentGuid = *tag.parentGuid();

        if (parentGuid.size() < qevercloud::EDAM_GUID_LEN_MIN) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Tag.parentGuid"));
        }

        if (parentGuid.size() > qevercloud::EDAM_GUID_LEN_MAX) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Tag.parentGuid"));
        }

        static const QRegularExpression guidRegExp{qevercloud::EDAM_GUID_REGEX};
        if (!guidRegExp.match(parentGuid).hasMatch()) {
            return utils::createUserException(
                qevercloud::EDAMErrorCode::BAD_DATA_FORMAT,
                QStringLiteral("Tag.parentGuid"));
        }
    }

    return std::nullopt;
}

} // namespace quentier::synchronization::tests::note_store
