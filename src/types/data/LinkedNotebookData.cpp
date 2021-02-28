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

#include "LinkedNotebookData.h"

#include <quentier/utility/Checks.h>

namespace quentier {

LinkedNotebookData::LinkedNotebookData(
    const qevercloud::LinkedNotebook & other) :
    QSharedData(),
    m_qecLinkedNotebook(other)
{}

LinkedNotebookData::LinkedNotebookData(qevercloud::LinkedNotebook && other) :
    QSharedData(), m_qecLinkedNotebook(std::move(other))
{}

void LinkedNotebookData::clear()
{
    m_qecLinkedNotebook = qevercloud::LinkedNotebook();
}

bool LinkedNotebookData::checkParameters(ErrorString & errorDescription) const
{
    if (!m_qecLinkedNotebook.guid.isSet()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "LinkedNotebookData", "Linked notebook's guid is not set"));

        return false;
    }
    else if (!checkGuid(m_qecLinkedNotebook.guid.ref())) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "LinkedNotebookData", "Linked notebook's guid is invalid"));

        errorDescription.details() = m_qecLinkedNotebook.guid.ref();
        return false;
    }

    if (m_qecLinkedNotebook.shareName.isSet()) {
        if (m_qecLinkedNotebook.shareName->isEmpty()) {
            errorDescription.setBase(QT_TRANSLATE_NOOP(
                "LinkedNotebookData",
                "Linked notebook's custom name is empty"));

            return false;
        }
        else {
            QLatin1Char spaceChar(' ');
            const QString & name = m_qecLinkedNotebook.shareName.ref();
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
                    "LinkedNotebookData",
                    "Linked notebook's custom name must contain non-whitespace "
                    "characters"));

                return false;
            }
        }
    }

    return true;
}

bool LinkedNotebookData::operator==(const LinkedNotebookData & other) const
{
    return (m_qecLinkedNotebook == other.m_qecLinkedNotebook) &&
        (m_isDirty == other.m_isDirty);
}

bool LinkedNotebookData::operator!=(const LinkedNotebookData & other) const
{
    return !(*this == other);
}

} // namespace quentier
