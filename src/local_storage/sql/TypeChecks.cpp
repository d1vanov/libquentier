/*
 * Copyright 2020-2023 Dmitry Ivanov
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

#include "TypeChecks.h"

#include <qevercloud/Constants.h>
#include <qevercloud/types/LinkedNotebook.h>
#include <qevercloud/types/Note.h>
#include <qevercloud/types/Notebook.h>
#include <qevercloud/types/Resource.h>
#include <qevercloud/types/SavedSearch.h>
#include <qevercloud/types/Tag.h>
#include <qevercloud/types/User.h>

#include <quentier/types/ErrorString.h>
#include <quentier/types/Validation.h>
#include <quentier/utility/Checks.h>

#include <QRegularExpression>

#include <utility>

namespace quentier::local_storage::sql {

bool checkLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription) noexcept
{
    if (!linkedNotebook.guid()) {
        errorDescription.setBase(
            QStringLiteral("Linked notebook's guid is not set"));

        return false;
    }

    if (!checkGuid(*linkedNotebook.guid())) {
        errorDescription.setBase(
            QStringLiteral("Linked notebook's guid is invalid"));

        errorDescription.details() = *linkedNotebook.guid();
        return false;
    }

    if (linkedNotebook.shareName()) {
        if (linkedNotebook.shareName()->isEmpty()) {
            errorDescription.setBase(
                QStringLiteral("Linked notebook's custom name is empty"));

            return false;
        }

        QLatin1Char spaceChar(' ');
        const QString & name = *linkedNotebook.shareName();
        const int size = name.size();

        bool nonSpaceCharFound = false;
        for (int i = 0; i < size; ++i) {
            if (name[i] != spaceChar) {
                nonSpaceCharFound = true;
                break;
            }
        }

        if (!nonSpaceCharFound) {
            errorDescription.setBase(QStringLiteral(
                "Linked notebook's custom name must contain non-whitespace "
                "characters"));

            return false;
        }
    }

    return true;
}

bool checkNote(
    const qevercloud::Note & note, ErrorString & errorDescription) noexcept
{
    if (note.localId().isEmpty() && !note.guid()) {
        errorDescription.setBase(
            QStringLiteral("Both note's local id and guid are empty"));

        return false;
    }

    if (note.guid() && !checkGuid(*note.guid())) {
        errorDescription.setBase(QStringLiteral("Note's guid is invalid"));

        errorDescription.details() = *note.guid();
        return false;
    }

    if (note.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*note.updateSequenceNum()))
    {
        errorDescription.setBase(
            QStringLiteral("Note's update sequence number is invalid"));

        errorDescription.details() = QString::number(*note.updateSequenceNum());

        return false;
    }

    if (note.title() && !validateNoteTitle(*note.title(), &errorDescription)) {
        return false;
    }

    if (note.content()) {
        int contentSize = note.content()->size();

        if ((contentSize < qevercloud::EDAM_NOTE_CONTENT_LEN_MIN) ||
            (contentSize > qevercloud::EDAM_NOTE_CONTENT_LEN_MAX))
        {
            errorDescription.setBase(
                QStringLiteral("Note's content length is invalid"));

            errorDescription.details() = QString::number(contentSize);
            return false;
        }
    }

    if (note.contentHash()) {
        int contentHashSize = note.contentHash()->size();

        if (contentHashSize != qevercloud::EDAM_HASH_LEN) {
            errorDescription.setBase(
                QStringLiteral("Note's content hash size is invalid"));

            errorDescription.details() = QString::number(contentHashSize);
            return false;
        }
    }

    if (note.notebookGuid() && !checkGuid(*note.notebookGuid())) {
        errorDescription.setBase(
            QStringLiteral("Note's notebook guid is invalid"));

        errorDescription.details() = *note.notebookGuid();
        return false;
    }

    if (note.tagGuids()) {
        int numTagGuids = note.tagGuids()->size();

        if (numTagGuids > qevercloud::EDAM_NOTE_TAGS_MAX) {
            errorDescription.setBase(QStringLiteral("Note has too many tags"));

            errorDescription.details() = QString::number(numTagGuids);
            return false;
        }
    }

    if (note.resources()) {
        int numResources = note.resources()->size();

        if (numResources > qevercloud::EDAM_NOTE_RESOURCES_MAX) {
            errorDescription.setBase(
                QStringLiteral("Note has too many resources"));

            errorDescription.details() =
                QString::number(qevercloud::EDAM_NOTE_RESOURCES_MAX);

            return false;
        }
    }

    if (note.attributes()) {
        const auto & attributes = *note.attributes();

        ErrorString error{
            QStringLiteral("Note attributes field has invalid size")};

#define CHECK_NOTE_ATTRIBUTE(name)                                             \
    if (attributes.name()) {                                                   \
        int name##Size = attributes.name()->size();                            \
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

        if (attributes.contentClass()) {
            int contentClassSize = attributes.contentClass()->size();
            if ((contentClassSize <
                 qevercloud::EDAM_NOTE_CONTENT_CLASS_LEN_MIN) ||
                (contentClassSize >
                 qevercloud::EDAM_NOTE_CONTENT_CLASS_LEN_MAX))
            {
                errorDescription.setBase(QStringLiteral(
                    "Note attributes' content class has invalid size"));

                errorDescription.details() = QString::number(contentClassSize);
                return false;
            }
        }

        if (attributes.applicationData()) {
            const auto & applicationData = *attributes.applicationData();

            if (applicationData.keysOnly()) {
                for (const auto & key:
                     std::as_const(*applicationData.keysOnly()))
                {
                    int keySize = key.size();
                    if ((keySize <
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) ||
                        (keySize >
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX))
                    {
                        errorDescription.setBase(QStringLiteral(
                            "Note's attributes application data "
                            "has invalid key (in keysOnly part)"));

                        errorDescription.details() = key;
                        return false;
                    }
                }
            }

            if (applicationData.fullMap()) {
                for (auto it = applicationData.fullMap()->constBegin(),
                          end = applicationData.fullMap()->constEnd();
                     it != end; ++it)
                {
                    int keySize = it.key().size();
                    if ((keySize <
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) ||
                        (keySize >
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX))
                    {
                        errorDescription.setBase(QStringLiteral(
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
                        errorDescription.setBase(
                            QStringLiteral("Note's attributes application data "
                                           "has invalid value size"));

                        errorDescription.details() = it.value();
                        return false;
                    }

                    int sumSize = keySize + valueSize;
                    if (sumSize >
                        qevercloud::EDAM_APPLICATIONDATA_ENTRY_LEN_MAX) {
                        errorDescription.setBase(
                            QStringLiteral("Note's attributes application data "
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

bool checkNotebook(
    const qevercloud::Notebook & notebook,
    ErrorString & errorDescription) noexcept
{
    if (notebook.localId().isEmpty() && !notebook.guid()) {
        errorDescription.setBase(
            QStringLiteral("Both notebook's local id and guid are not set"));
        return false;
    }

    if (notebook.guid() && !checkGuid(*notebook.guid())) {
        errorDescription.setBase(QStringLiteral("Notebook's guid is invalid"));

        errorDescription.details() = *notebook.guid();
        return false;
    }

    const QString linkedNotebookGuid = [&notebook] {
        const auto & localData = notebook.localData();

        const auto it =
            localData.constFind(QStringLiteral("linkedNotebookGuid"));

        if (it != localData.constEnd()) {
            return it.value().toString();
        }

        return QString{};
    }();

    if (!linkedNotebookGuid.isEmpty() && !checkGuid(linkedNotebookGuid)) {
        errorDescription.setBase(
            QStringLiteral("Notebook's linked notebook guid is invalid"));

        errorDescription.details() = linkedNotebookGuid;
        return false;
    }

    if (notebook.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*notebook.updateSequenceNum()))
    {
        errorDescription.setBase(
            QStringLiteral("Notebook's update sequence number is invalid"));

        errorDescription.details() =
            QString::number(*notebook.updateSequenceNum());

        return false;
    }

    if (notebook.name() &&
        !validateNotebookName(*notebook.name(), &errorDescription))
    {
        return false;
    }

    if (notebook.sharedNotebooks()) {
        for (const auto & sharedNotebook:
             std::as_const(*notebook.sharedNotebooks()))
        {
            if (!sharedNotebook.id()) {
                errorDescription.setBase(QStringLiteral(
                    "Notebook has shared notebook without share id set"));

                return false;
            }

            if (sharedNotebook.notebookGuid() &&
                !checkGuid(*sharedNotebook.notebookGuid())) {
                errorDescription.setBase(QStringLiteral(
                    "Notebook has shared notebook with invalid guid"));

                errorDescription.details() = *sharedNotebook.notebookGuid();
                return false;
            }
        }
    }

    if (notebook.businessNotebook()) {
        if (notebook.businessNotebook()->notebookDescription()) {
            int businessNotebookDescriptionSize =
                notebook.businessNotebook()->notebookDescription()->size();

            if ((businessNotebookDescriptionSize <
                 qevercloud::EDAM_BUSINESS_NOTEBOOK_DESCRIPTION_LEN_MIN) ||
                (businessNotebookDescriptionSize >
                 qevercloud::EDAM_BUSINESS_NOTEBOOK_DESCRIPTION_LEN_MAX))
            {
                errorDescription.setBase(QStringLiteral(
                    "Description for business notebook has invalid size"));

                errorDescription.details() =
                    *notebook.businessNotebook()->notebookDescription();

                return false;
            }
        }
    }

    return true;
}

bool checkResource(
    const qevercloud::Resource & resource,
    ErrorString & errorDescription) noexcept
{
    if (resource.localId().isEmpty() && !resource.guid()) {
        errorDescription.setBase(
            QStringLiteral("Both resource's local id and guid are empty"));

        return false;
    }

    if (resource.guid() && !checkGuid(*resource.guid())) {
        errorDescription.setBase(QStringLiteral("Resource's guid is invalid"));

        errorDescription.details() = *resource.guid();
        return false;
    }

    if (resource.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*resource.updateSequenceNum()))
    {
        errorDescription.setBase(
            QStringLiteral("Resource's update sequence number is invalid"));

        errorDescription.details() =
            QString::number(*resource.updateSequenceNum());

        return false;
    }

    if (resource.noteGuid() && !checkGuid(*resource.noteGuid())) {
        errorDescription.setBase(
            QStringLiteral("Resource's note guid is invalid"));

        errorDescription.details() = *resource.noteGuid();
        return false;
    }

    if (resource.data() && resource.data()->bodyHash()) {
        const auto hashSize =
            static_cast<qint32>(resource.data()->bodyHash()->size());

        if (hashSize != qevercloud::EDAM_HASH_LEN) {
            errorDescription.setBase(
                QStringLiteral("Resource's data hash has invalid size"));

            errorDescription.details() =
                QString::fromLocal8Bit(*resource.data()->bodyHash());

            return false;
        }
    }

    if (resource.recognition() && resource.recognition()->bodyHash()) {
        const auto hashSize =
            static_cast<qint32>(resource.recognition()->bodyHash()->size());

        if (hashSize != qevercloud::EDAM_HASH_LEN) {
            errorDescription.setBase(QStringLiteral(
                "Resource's recognition data hash has invalid size"));

            errorDescription.details() =
                QString::fromLocal8Bit(*resource.recognition()->bodyHash());

            return false;
        }
    }

    if (resource.alternateData() && resource.alternateData()->bodyHash()) {
        const auto hashSize =
            static_cast<qint32>(resource.alternateData()->bodyHash()->size());

        if (hashSize != qevercloud::EDAM_HASH_LEN) {
            errorDescription.setBase(QStringLiteral(
                "Resource's alternate data hash has invalid size"));

            errorDescription.details() =
                QString::fromLocal8Bit(*resource.alternateData()->bodyHash());

            return false;
        }
    }

    if (resource.mime()) {
        const auto mimeSize = static_cast<qint32>(resource.mime()->size());
        if ((mimeSize < qevercloud::EDAM_MIME_LEN_MIN) ||
            (mimeSize > qevercloud::EDAM_MIME_LEN_MAX))
        {
            errorDescription.setBase(
                QStringLiteral("Resource's mime type has invalid length"));

            errorDescription.details() = *resource.mime();
            return false;
        }
    }

    if (resource.attributes()) {
        if (resource.attributes()->sourceURL()) {
            const auto sourceURLSize =
                static_cast<qint32>(resource.attributes()->sourceURL()->size());

            if ((sourceURLSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
                (sourceURLSize > qevercloud::EDAM_ATTRIBUTE_LEN_MAX))
            {
                errorDescription.setBase(QStringLiteral(
                    "Resource's sourceURL attribute has invalid length"));

                errorDescription.details() =
                    *resource.attributes()->sourceURL();

                return false;
            }
        }

        if (resource.attributes()->cameraMake()) {
            auto cameraMakeSize = static_cast<qint32>(
                resource.attributes()->cameraMake()->size());

            if ((cameraMakeSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
                (cameraMakeSize > qevercloud::EDAM_ATTRIBUTE_LEN_MAX))
            {
                errorDescription.setBase(QStringLiteral(
                    "Resource's cameraMake attribute has invalid length"));

                errorDescription.details() =
                    *resource.attributes()->cameraMake();

                return false;
            }
        }

        if (resource.attributes()->cameraModel()) {
            auto cameraModelSize = static_cast<qint32>(
                resource.attributes()->cameraModel()->size());

            if ((cameraModelSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN) ||
                (cameraModelSize > qevercloud::EDAM_ATTRIBUTE_LEN_MAX))
            {
                errorDescription.setBase(QStringLiteral(
                    "Resource's cameraModel attribute has invalid length"));

                errorDescription.details() =
                    *resource.attributes()->cameraModel();

                return false;
            }
        }
    }

    return true;
}

bool checkSavedSearch(
    const qevercloud::SavedSearch & savedSearch,
    ErrorString & errorDescription) noexcept
{
    if (savedSearch.localId().isEmpty() && !savedSearch.guid()) {
        errorDescription.setBase(
            QStringLiteral("Both saved search's local id and guid are empty"));

        return false;
    }

    if (savedSearch.guid() && !checkGuid(*savedSearch.guid())) {
        errorDescription.setBase(
            QStringLiteral("Saved search's guid is invalid"));

        errorDescription.details() = *savedSearch.guid();
        return false;
    }

    if (savedSearch.name() &&
        !validateSavedSearchName(*savedSearch.name(), &errorDescription))
    {
        return false;
    }

    if (savedSearch.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*savedSearch.updateSequenceNum()))
    {
        errorDescription.setBase(
            QStringLiteral("Saved search's update sequence number is invalid"));

        errorDescription.details() =
            QString::number(*savedSearch.updateSequenceNum());

        return false;
    }

    if (savedSearch.query()) {
        const QString & query = *savedSearch.query();
        int querySize = query.size();

        if ((querySize < qevercloud::EDAM_SEARCH_QUERY_LEN_MIN) ||
            (querySize > qevercloud::EDAM_SEARCH_QUERY_LEN_MAX))
        {
            errorDescription.setBase(QStringLiteral(
                "Saved search's query exceeds the allowed size"));

            errorDescription.details() = query;
            return false;
        }
    }

    if (savedSearch.format() &&
        *savedSearch.format() != qevercloud::QueryFormat::USER)
    {
        errorDescription.setBase(
            QStringLiteral("Saved search has unsupported query format"));

        errorDescription.details() = ToString(*savedSearch.format());
        return false;
    }

    return true;
}

bool checkTag(
    const qevercloud::Tag & tag, ErrorString & errorDescription) noexcept
{
    if (tag.localId().isEmpty() && !tag.guid()) {
        errorDescription.setBase(
            QStringLiteral("Both tag's local id and guid are empty"));

        return false;
    }

    if (tag.guid() && !checkGuid(*tag.guid())) {
        errorDescription.setBase(QStringLiteral("Tag's guid is invalid"));

        errorDescription.details() = *tag.guid();
        return false;
    }

    const QString linkedNotebookGuid =
        tag.linkedNotebookGuid().value_or(QString{});
    if (!linkedNotebookGuid.isEmpty() && !checkGuid(linkedNotebookGuid)) {
        errorDescription.setBase(
            QStringLiteral("Tag's linked notebook guid is invalid"));

        errorDescription.details() = linkedNotebookGuid;
        return false;
    }

    if (tag.name() && !validateTagName(*tag.name(), &errorDescription)) {
        return false;
    }

    if (tag.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*tag.updateSequenceNum()))
    {
        errorDescription.setBase(
            QStringLiteral("Tag's update sequence number is invalid"));

        errorDescription.details() = QString::number(*tag.updateSequenceNum());
        return false;
    }

    if (tag.parentGuid() && !checkGuid(*tag.parentGuid())) {
        errorDescription.setBase(
            QStringLiteral("Tag's parent guid is invalid"));

        errorDescription.details() = *tag.parentGuid();
        return false;
    }

    return true;
}

bool checkUser(
    const qevercloud::User & user, ErrorString & errorDescription) noexcept
{
    if (!user.id()) {
        errorDescription.setBase(QStringLiteral("User id is not set"));
        return false;
    }

    if (user.username()) {
        const QString & username = *user.username();
        int usernameSize = username.size();

        if ((usernameSize > qevercloud::EDAM_USER_USERNAME_LEN_MAX) ||
            (usernameSize < qevercloud::EDAM_USER_USERNAME_LEN_MIN))
        {
            errorDescription.setBase(
                QStringLiteral("User's name has invalid size"));

            errorDescription.details() = username;
            return false;
        }

        QRegularExpression usernameRegExp(qevercloud::EDAM_USER_USERNAME_REGEX);
        if (!usernameRegExp.match(username).isValid()) {
            errorDescription.setBase(QStringLiteral(
                "User's name can contain only \"a-z\" or \"0-9\""
                "or \"-\" but should not start or end with \"-\""));
            return false;
        }
    }

    // NOTE: ignore everything about email because "Third party applications
    // that authenticate using OAuth do not have access to this field"

    if (user.name()) {
        const QString & name = *user.name();
        int nameSize = name.size();

        if ((nameSize > qevercloud::EDAM_USER_NAME_LEN_MAX) ||
            (nameSize < qevercloud::EDAM_USER_NAME_LEN_MIN))
        {
            errorDescription.setBase(
                QStringLiteral("User's displayed name has invalid size"));

            errorDescription.details() = name;
            return false;
        }

        QRegularExpression nameRegExp(qevercloud::EDAM_USER_NAME_REGEX);
        if (!nameRegExp.match(name).isValid()) {
            errorDescription.setBase(QStringLiteral(
                "User's displayed name doesn't match its regular expression. "
                "Consider removing any special characters"));
            return false;
        }
    }

    if (user.timezone()) {
        const QString & timezone = *user.timezone();
        int timezoneSize = timezone.size();

        if ((timezoneSize > qevercloud::EDAM_TIMEZONE_LEN_MAX) ||
            (timezoneSize < qevercloud::EDAM_TIMEZONE_LEN_MIN))
        {
            errorDescription.setBase(
                QStringLiteral("User's timezone has invalid size"));

            errorDescription.details() = timezone;
            return false;
        }

        QRegularExpression timezoneRegExp(qevercloud::EDAM_TIMEZONE_REGEX);
        if (!timezoneRegExp.match(timezone).isValid()) {
            errorDescription.setBase(QStringLiteral(
                "User's timezone doesn't match its regular expression. It must "
                "be encoded as a standard zone ID such as "
                "\"America/Los_Angeles\" or \"GMT+08:00\"."));

            return false;
        }
    }

    if (user.attributes()) {
        const auto & attributes = *user.attributes();

        if (attributes.defaultLocationName()) {
            const QString & defaultLocationName =
                *attributes.defaultLocationName();

            int defaultLocationNameSize = defaultLocationName.size();

            if ((defaultLocationNameSize >
                 qevercloud::EDAM_ATTRIBUTE_LEN_MAX) ||
                (defaultLocationNameSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN))
            {
                errorDescription.setBase(QStringLiteral(
                    "User's default location name has invalid size"));

                errorDescription.details() = defaultLocationName;
                return false;
            }
        }

        if (attributes.viewedPromotions()) {
            const QStringList & viewedPromotions =
                *attributes.viewedPromotions();

            for (const auto & viewedPromotion: std::as_const(viewedPromotions))
            {
                int viewedPromotionSize = viewedPromotion.size();
                if ((viewedPromotionSize >
                     qevercloud::EDAM_ATTRIBUTE_LEN_MAX) ||
                    (viewedPromotionSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN))
                {
                    errorDescription.setBase(QStringLiteral(
                        "User's viewed promotion has invalid size"));

                    errorDescription.details() = viewedPromotion;
                    return false;
                }
            }
        }

        if (attributes.incomingEmailAddress()) {
            const QString & incomingEmailAddress =
                *attributes.incomingEmailAddress();

            int incomingEmailAddressSize = incomingEmailAddress.size();

            if ((incomingEmailAddressSize >
                 qevercloud::EDAM_ATTRIBUTE_LEN_MAX) ||
                (incomingEmailAddressSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN))
            {
                errorDescription.setBase(QStringLiteral(
                    "User's incoming email address has invalid size"));

                errorDescription.details() = incomingEmailAddress;
                return false;
            }
        }

        if (attributes.recentMailedAddresses()) {
            const QStringList & recentMailedAddresses =
                *attributes.recentMailedAddresses();

            int numRecentMailedAddresses = recentMailedAddresses.size();

            if (numRecentMailedAddresses >
                qevercloud::EDAM_USER_RECENT_MAILED_ADDRESSES_MAX)
            {
                errorDescription.setBase(QStringLiteral(
                    "User recent mailed addresses size is invalid"));

                errorDescription.details() =
                    QString::number(numRecentMailedAddresses);

                return false;
            }

            for (const auto & recentMailedAddress:
                 std::as_const(recentMailedAddresses)) {
                int recentMailedAddressSize = recentMailedAddress.size();
                if ((recentMailedAddressSize >
                     qevercloud::EDAM_ATTRIBUTE_LEN_MAX) ||
                    (recentMailedAddressSize <
                     qevercloud::EDAM_ATTRIBUTE_LEN_MIN))
                {
                    errorDescription.setBase(QStringLiteral(
                        "User's recent emailed address has invalid size"));

                    errorDescription.details() = recentMailedAddress;
                    return false;
                }
            }
        }

        if (attributes.comments()) {
            const QString & comments = *attributes.comments();
            int commentsSize = comments.size();

            if ((commentsSize > qevercloud::EDAM_ATTRIBUTE_LEN_MAX) ||
                (commentsSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN))
            {
                errorDescription.setBase(
                    QStringLiteral("User's comments have invalid size"));

                errorDescription.details() = QString::number(commentsSize);
                return false;
            }
        }
    }

    return true;
}

} // namespace quentier::local_storage::sql
