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

#include <quentier/utility/Checks.h>

#include <qt5qevercloud/generated/Constants.h>

#include <limits>

namespace quentier {

bool checkGuid(const QString & guid)
{
    qint32 guidSize = static_cast<qint32>(guid.size());

    if (guidSize < qevercloud::EDAM_GUID_LEN_MIN) {
        return false;
    }

    if (guidSize > qevercloud::EDAM_GUID_LEN_MAX) {
        return false;
    }

    return true;
}

bool checkUpdateSequenceNumber(const int32_t updateSequenceNumber)
{
    return !(
        (updateSequenceNumber < 0) ||
        (updateSequenceNumber == std::numeric_limits<int32_t>::min()) ||
        (updateSequenceNumber == std::numeric_limits<int32_t>::max()));
}

} // namespace quentier
