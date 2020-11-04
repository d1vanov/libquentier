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

#include "TagData.h"

#include <quentier/types/Tag.h>
#include <quentier/utility/Checks.h>

namespace quentier {

TagData::TagData(const qevercloud::Tag & other) :
    FavoritableDataElementData(), m_qecTag(other)
{}

TagData::TagData(qevercloud::Tag && other) :
    FavoritableDataElementData(), m_qecTag(std::move(other))
{}

void TagData::clear()
{
    m_qecTag = qevercloud::Tag();
    m_linkedNotebookGuid.clear();
}

bool TagData::checkParameters(ErrorString & errorDescription) const
{
    if (m_qecTag.guid.isSet() && !checkGuid(m_qecTag.guid.ref())) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("TagData", "Tag's guid is invalid"));

        errorDescription.details() = m_qecTag.guid;
        return false;
    }

    if (m_linkedNotebookGuid.isSet() && !checkGuid(m_linkedNotebookGuid.ref()))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "TagData", "Tag's linked notebook guid is invalid"));

        errorDescription.details() = m_linkedNotebookGuid;
        return false;
    }

    if (m_qecTag.name.isSet() &&
        !Tag::validateName(m_qecTag.name.ref(), &errorDescription))
    {
        return false;
    }

    if (m_qecTag.updateSequenceNum.isSet() &&
        !checkUpdateSequenceNumber(m_qecTag.updateSequenceNum))
    {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "TagData", "Tag's update sequence number is invalid"));

        errorDescription.details() =
            QString::number(m_qecTag.updateSequenceNum);

        return false;
    }

    if (m_qecTag.parentGuid.isSet() && !checkGuid(m_qecTag.parentGuid.ref())) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("TagData", "Tag's parent guid is invalid"));

        errorDescription.details() = m_qecTag.parentGuid;
        return false;
    }

    return true;
}

bool TagData::operator==(const TagData & other) const
{
    return (m_qecTag == other.m_qecTag) && (m_isDirty == other.m_isDirty) &&
        (m_isLocal == other.m_isLocal) &&
        (m_isFavorited == other.m_isFavorited) &&
        m_linkedNotebookGuid.isEqual(other.m_linkedNotebookGuid) &&
        m_parentLocalUid.isEqual(other.m_parentLocalUid);
}

bool TagData::operator!=(const TagData & other) const
{
    return !(*this == other);
}

} // namespace quentier
