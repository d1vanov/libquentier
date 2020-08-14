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

#include "AccountData.h"

#include <qt5qevercloud/generated/Constants.h>

namespace quentier {

AccountData::AccountData()
{
    if (m_accountType != Account::Type::Local) {
        switchEvernoteAccountType(m_evernoteAccountType);
    }
}

void AccountData::switchEvernoteAccountType(
    const Account::EvernoteAccountType evernoteAccountType)
{
    m_evernoteAccountType = evernoteAccountType;
    m_mailLimitDaily = mailLimitDaily();
    m_noteSizeMax = noteSizeMax();
    m_resourceSizeMax = resourceSizeMax();
    m_linkedNotebookMax = linkedNotebookMax();
    m_noteCountMax = noteCountMax();
    m_notebookCountMax = notebookCountMax();
    m_tagCountMax = tagCountMax();
    m_noteTagCountMax = noteTagCountMax();
    m_savedSearchCountMax = savedSearchCountMax();
    m_noteResourceCountMax = noteResourceCountMax();
}

void AccountData::setEvernoteAccountLimits(
    const qevercloud::AccountLimits & limits)
{
    m_mailLimitDaily =
        (limits.userMailLimitDaily.isSet() ? limits.userMailLimitDaily.ref()
                                           : mailLimitDaily());

    m_noteSizeMax =
        (limits.noteSizeMax.isSet() ? limits.noteSizeMax.ref() : noteSizeMax());

    m_resourceSizeMax =
        (limits.resourceSizeMax.isSet() ? limits.resourceSizeMax.ref()
                                        : resourceSizeMax());

    m_linkedNotebookMax =
        (limits.userLinkedNotebookMax.isSet()
             ? limits.userLinkedNotebookMax.ref()
             : linkedNotebookMax());

    m_noteCountMax =
        (limits.userNoteCountMax.isSet() ? limits.userNoteCountMax.ref()
                                         : noteCountMax());

    m_notebookCountMax =
        (limits.userNotebookCountMax.isSet() ? limits.userNotebookCountMax.ref()
                                             : notebookCountMax());

    m_tagCountMax =
        (limits.userTagCountMax.isSet() ? limits.userTagCountMax.ref()
                                        : tagCountMax());

    m_noteTagCountMax =
        (limits.noteTagCountMax.isSet() ? limits.noteTagCountMax.ref()
                                        : noteTagCountMax());

    m_savedSearchCountMax =
        (limits.userSavedSearchesMax.isSet() ? limits.userSavedSearchesMax.ref()
                                             : savedSearchCountMax());

    m_noteResourceCountMax =
        (limits.noteResourceCountMax.isSet() ? limits.noteResourceCountMax.ref()
                                             : noteResourceCountMax());
}

qint32 AccountData::mailLimitDaily() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    switch (m_evernoteAccountType) {
    case Account::EvernoteAccountType::Premium:
        return qevercloud::EDAM_USER_MAIL_LIMIT_DAILY_PREMIUM;
    default:
        return qevercloud::EDAM_USER_MAIL_LIMIT_DAILY_FREE;
    }
}

qint64 AccountData::noteSizeMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint64>::max();
    }

    switch (m_evernoteAccountType) {
    case Account::EvernoteAccountType::Premium:
        return qevercloud::EDAM_NOTE_SIZE_MAX_PREMIUM;
    default:
        return qevercloud::EDAM_NOTE_SIZE_MAX_FREE;
    }
}

qint64 AccountData::resourceSizeMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint64>::max();
    }

    switch (m_evernoteAccountType) {
    case Account::EvernoteAccountType::Premium:
        return qevercloud::EDAM_RESOURCE_SIZE_MAX_PREMIUM;
    default:
        return qevercloud::EDAM_RESOURCE_SIZE_MAX_FREE;
    }
}

qint32 AccountData::linkedNotebookMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    switch (m_evernoteAccountType) {
    case Account::EvernoteAccountType::Premium:
        return qevercloud::EDAM_USER_LINKED_NOTEBOOK_MAX_PREMIUM;
    default:
        return qevercloud::EDAM_USER_LINKED_NOTEBOOK_MAX;
    }
}

qint32 AccountData::noteCountMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    switch (m_evernoteAccountType) {
    case Account::EvernoteAccountType::Business:
        return qevercloud::EDAM_BUSINESS_NOTES_MAX;
    default:
        return qevercloud::EDAM_USER_NOTES_MAX;
    }
}

qint32 AccountData::notebookCountMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    switch (m_evernoteAccountType) {
    case Account::EvernoteAccountType::Business:
        return qevercloud::EDAM_BUSINESS_NOTEBOOKS_MAX;
    default:
        return qevercloud::EDAM_USER_NOTEBOOKS_MAX;
    }
}

qint32 AccountData::tagCountMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    switch (m_evernoteAccountType) {
    case Account::EvernoteAccountType::Business:
        return qevercloud::EDAM_BUSINESS_TAGS_MAX;
    default:
        return qevercloud::EDAM_USER_TAGS_MAX;
    }
}

qint32 AccountData::noteTagCountMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    return qevercloud::EDAM_NOTE_TAGS_MAX;
}

qint32 AccountData::savedSearchCountMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    return qevercloud::EDAM_USER_SAVED_SEARCHES_MAX;
}

qint32 AccountData::noteResourceCountMax() const
{
    if (m_accountType == Account::Type::Local) {
        return std::numeric_limits<qint32>::max();
    }

    return qevercloud::EDAM_NOTE_RESOURCES_MAX;
}

} // namespace quentier
