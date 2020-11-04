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

#ifndef LIB_QUENTIER_UTILITY_DATE_TIME_H
#define LIB_QUENTIER_UTILITY_DATE_TIME_H

#include <quentier/utility/Linkage.h>

#include <QFlags>

namespace quentier {

constexpr int secondsToMilliseconds(int seconds) noexcept
{
    return seconds * 1000;
}

/**
 * @brief The DateTimePrint class simply wraps the enum containing datetime
 * printing options
 */
class QUENTIER_EXPORT DateTimePrint
{
public:
    /**
     * Available printing options for datetime
     */
    enum Option
    {
        /**
         * Include the numeric representation of the timestamp into
         * the printed string
         */
        IncludeNumericTimestamp = 1 << 1,
        /**
         * Include milliseconds into the printed string
         */
        IncludeMilliseconds = 1 << 2,
        /**
         * Include timezone into the printed string
         * WARNING: currently this option has no effect on Windows platform,
         * the timezone is not included anyway.
         */
        IncludeTimezone = 1 << 3
    };
    Q_DECLARE_FLAGS(Options, Option)
};

Q_DECLARE_OPERATORS_FOR_FLAGS(DateTimePrint::Options)

/**
 * printableDateTimeFromTimestamp converts the passed in timestamp into
 * a human readable datetime string
 *
 * @param timestamp             The timestamp to be translated to
 *                              a human readable string
 * @param options               Datetime printing options
 * @param customFormat          The custom format string; internally, if not
 *                              null, it would be passed to strftime function
 *                              declared in <ctime> header of the C++ standard
 *                              library; but beware that the length of
 *                              the printed string is limited by 100 characters
 *                              regardless of the format string
 *
 * @return                      Human readable datetime string corresponding to
 *                              the passed in timestamp
 */
const QString QUENTIER_EXPORT printableDateTimeFromTimestamp(
    const qint64 timestamp,
    DateTimePrint::Options options = DateTimePrint::Options(
        DateTimePrint::IncludeNumericTimestamp |
        DateTimePrint::IncludeMilliseconds | DateTimePrint::IncludeTimezone),
    const char * customFormat = nullptr);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_DATE_TIME_H
