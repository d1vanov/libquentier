/*
 * Copyright 2020 Dmitry Ivanov
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

#include <qevercloud/generated/Constants.h>
#include <qevercloud/generated/types/LinkedNotebook.h>
#include <qevercloud/generated/types/Note.h>
#include <qevercloud/generated/types/Notebook.h>
#include <qevercloud/generated/types/Tag.h>
#include <qevercloud/generated/types/User.h>

#include <quentier/types/ErrorString.h>
#include <quentier/types/Validation.h>
#include <quentier/utility/Checks.h>

#include <QRegularExpression>

namespace quentier {

bool checkLinkedNotebook(
    const qevercloud::LinkedNotebook & linkedNotebook,
    ErrorString & errorDescription) noexcept
{
    if (!linkedNotebook.guid()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks", "Linked notebook's guid is not set"));

        return false;
    }

    if (!checkGuid(*linkedNotebook.guid())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks", "Linked notebook's guid is invalid"));

        errorDescription.details() = *linkedNotebook.guid();
        return false;
    }

    if (linkedNotebook.shareName()) {
        if (linkedNotebook.shareName()->isEmpty()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
                "Linked notebook's custom name is empty"));

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
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
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
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Both note's local id and guid are empty"));

        return false;
    }

    if (note.guid() && !checkGuid(*note.guid())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks", "Note's guid is invalid"));

        errorDescription.details() = *note.guid();
        return false;
    }

    if (note.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*note.updateSequenceNum()))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Note's update sequence number is invalid"));

        errorDescription.details() =
            QString::number(*note.updateSequenceNum());

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
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
                "Note's content length is invalid"));

            errorDescription.details() = QString::number(contentSize);
            return false;
        }
    }

    if (note.contentHash()) {
        int contentHashSize = note.contentHash()->size();

        if (contentHashSize != qevercloud::EDAM_HASH_LEN) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
                "Note's content hash size is invalid"));

            errorDescription.details() = QString::number(contentHashSize);
            return false;
        }
    }

    if (note.notebookGuid() &&
        !checkGuid(*note.notebookGuid()))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks", "Note's notebook guid is invalid"));

        errorDescription.details() = *note.notebookGuid();
        return false;
    }

    if (note.tagGuids()) {
        int numTagGuids = note.tagGuids()->size();

        if (numTagGuids > qevercloud::EDAM_NOTE_TAGS_MAX) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks", "Note has too many tags"));

            errorDescription.details() = QString::number(numTagGuids);
            return false;
        }
    }

    if (note.resources()) {
        int numResources = note.resources()->size();

        if (numResources > qevercloud::EDAM_NOTE_RESOURCES_MAX) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks", "Note has too many resources"));

            errorDescription.details() =
                QString::number(qevercloud::EDAM_NOTE_RESOURCES_MAX);

            return false;
        }
    }

    if (note.attributes()) {
        const auto & attributes = *note.attributes();

        ErrorString error(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Note attributes field has invalid size"));

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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage:type_checks",
                    "Note attributes' content class has invalid size"));

                errorDescription.details() = QString::number(contentClassSize);
                return false;
            }
        }

        if (attributes.applicationData()) {
            const auto & applicationData = *attributes.applicationData();

            if (applicationData.keysOnly()) {
                for (const auto & key: qAsConst(*applicationData.keysOnly()))
                {
                    int keySize = key.size();
                    if ((keySize <
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) ||
                        (keySize >
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX))
                    {
                        errorDescription.setBase(QT_TRANSLATE_NOOP(
                            "local_storage:type_checks",
                            "Note's attributes application data "
                            "has invalid key (in keysOnly part)"));

                        errorDescription.details() = key;
                        return false;
                    }
                }
            }

            if (applicationData.fullMap()) {
                for (auto it = applicationData.fullMap()->constBegin(),
                     end = applicationData.fullMap()->constEnd(); it != end;
                     ++it)
                {
                    int keySize = it.key().size();
                    if ((keySize <
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MIN) ||
                        (keySize >
                         qevercloud::EDAM_APPLICATIONDATA_NAME_LEN_MAX))
                    {
                        errorDescription.setBase(QT_TRANSLATE_NOOP(
                            "local_storage:type_checks",
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
                            "local_storage:type_checks",
                            "Note's attributes application data "
                            "has invalid value size"));

                        errorDescription.details() = it.value();
                        return false;
                    }

                    int sumSize = keySize + valueSize;
                    if (sumSize >
                        qevercloud::EDAM_APPLICATIONDATA_ENTRY_LEN_MAX) {
                        errorDescription.setBase(QT_TRANSLATE_NOOP(
                            "local_storage:type_checks",
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

bool checkNotebook(
    const qevercloud::Notebook & notebook,
    ErrorString & errorDescription) noexcept
{
    if (notebook.localId().isEmpty() && !notebook.guid()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Both notebook's local id and guid are not set"));
        return false;
    }

    if (notebook.guid() && !checkGuid(*notebook.guid())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
             "local_storage:type_checks", "Notebook's guid is invalid"));

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

    if (!linkedNotebookGuid.isEmpty() && !checkGuid(linkedNotebookGuid))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Notebook's linked notebook guid is invalid"));

        errorDescription.details() = linkedNotebookGuid;
        return false;
    }

    if (notebook.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*notebook.updateSequenceNum()))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Notebook's update sequence number is invalid"));

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
             ::qAsConst(*notebook.sharedNotebooks()))
        {
            if (!sharedNotebook.id()) {
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage:type_checks",
                    "Notebook has shared notebook without share id set"));

                return false;
            }

            if (sharedNotebook.notebookGuid() &&
                !checkGuid(*sharedNotebook.notebookGuid()))
            {
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "NotebookData",
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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "NotebookData",
                    "Description for business notebook has invalid size"));

                errorDescription.details() =
                    *notebook.businessNotebook()->notebookDescription();

                return false;
            }
        }
    }

    return true;
}

bool checkTag(
    const qevercloud::Tag & tag, ErrorString & errorDescription) noexcept
{
    if (tag.localId().isEmpty() && !tag.guid()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Both tag's local id and guid are empty"));

        return false;
    }

    if (tag.guid() && !checkGuid(*tag.guid())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
             "local_storage:type_checks", "Tag's guid is invalid"));

        errorDescription.details() = *tag.guid();
        return false;
    }

    if (const auto it =
        tag.localData().constFind(QStringLiteral("linkedNotebookGuid"));
        it != tag.localData().constEnd() && !checkGuid(it.value().toString()))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Tag's linked notebook guid is invalid"));

        errorDescription.details() = it.value().toString();
        return false;
    }

    if (tag.name() &&
        !validateTagName(*tag.name(), &errorDescription))
    {
        return false;
    }

    if (tag.updateSequenceNum() &&
        !checkUpdateSequenceNumber(*tag.updateSequenceNum()))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks",
            "Tag's update sequence number is invalid"));

        errorDescription.details() = QString::number(*tag.updateSequenceNum());
        return false;
    }

    if (tag.parentGuid() && !checkGuid(*tag.parentGuid())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "local_storage:type_checks", "Tag's parent guid is invalid"));

        errorDescription.details() = *tag.parentGuid();
        return false;
    }

    return true;
}

bool checkUser(
    const qevercloud::User & user, ErrorString & errorDescription) noexcept
{
    if (!user.id()) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("local_storage:type_checks", "User id is not set"));
        return false;
    }

    if (user.username()) {
        const QString & username = *user.username();
        int usernameSize = username.size();

        if ((usernameSize > qevercloud::EDAM_USER_USERNAME_LEN_MAX) ||
            (usernameSize < qevercloud::EDAM_USER_USERNAME_LEN_MIN))
        {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks", "User's name has invalid size"));

            errorDescription.details() = username;
            return false;
        }

        QRegularExpression usernameRegExp(qevercloud::EDAM_USER_USERNAME_REGEX);
        if (!usernameRegExp.match(username).isValid()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
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
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
                "User's displayed name has invalid size"));

            errorDescription.details() = name;
            return false;
        }

        QRegularExpression nameRegExp(qevercloud::EDAM_USER_NAME_REGEX);
        if (!nameRegExp.match(name).isValid()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
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
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks", "User's timezone has invalid size"));

            errorDescription.details() = timezone;
            return false;
        }

        QRegularExpression timezoneRegExp(qevercloud::EDAM_TIMEZONE_REGEX);
        if (!timezoneRegExp.match(timezone).isValid()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "local_storage:type_checks",
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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage:type_checks",
                    "User's default location name has invalid size"));

                errorDescription.details() = defaultLocationName;
                return false;
            }
        }

        if (attributes.viewedPromotions()) {
            const QStringList & viewedPromotions =
                *attributes.viewedPromotions();

            for (const auto & viewedPromotion: qAsConst(viewedPromotions)) {
                int viewedPromotionSize = viewedPromotion.size();
                if ((viewedPromotionSize >
                     qevercloud::EDAM_ATTRIBUTE_LEN_MAX) ||
                    (viewedPromotionSize < qevercloud::EDAM_ATTRIBUTE_LEN_MIN))
                {
                    errorDescription.setBase(QT_TRANSLATE_NOOP(
                        "local_storage:type_checks",
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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage:type_checks",
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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "User", "User recent mailed addresses size is invalid"));

                errorDescription.details() =
                    QString::number(numRecentMailedAddresses);

                return false;
            }

            for (const auto & recentMailedAddress:
                 qAsConst(recentMailedAddresses)) {
                int recentMailedAddressSize = recentMailedAddress.size();
                if ((recentMailedAddressSize >
                     qevercloud::EDAM_ATTRIBUTE_LEN_MAX) ||
                    (recentMailedAddressSize <
                     qevercloud::EDAM_ATTRIBUTE_LEN_MIN))
                {
                    errorDescription.setBase(QT_TRANSLATE_NOOP(
                        "local_storage:type_checks",
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
                errorDescription.setBase(QT_TRANSLATE_NOOP(
                    "local_storage:type_checks",
                    "User's comments have invalid size"));

                errorDescription.details() = QString::number(commentsSize);
                return false;
            }
        }
    }

    return true;
}

} // namespace quentier
