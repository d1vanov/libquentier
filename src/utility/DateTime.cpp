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

#include <quentier/utility/DateTime.h>

#include <QString>

#include <ctime>

namespace quentier {

const QString printableDateTimeFromTimestamp(
    const qint64 timestamp, const DateTimePrint::Options options,
    const char * customFormat)
{
    if (Q_UNLIKELY(timestamp < 0)) {
        return QString::number(timestamp);
    }

    QString result;

    if (options & DateTimePrint::IncludeNumericTimestamp) {
        result += QString::number(timestamp);
        result += QStringLiteral(" (");
    }

    // NOTE: deliberately avoiding the use of QDateTime here as this function
    // would be potentially called from several threads and QDateTime::toString
    // has the potential to randomly crash in such environments, see e.g.
    // https://bugreports.qt.io/browse/QTBUG-49473

    std::time_t t(timestamp / 1000);
    std::tm localTm;
    Q_UNUSED(localTm)
    std::tm * tm = nullptr;

#ifdef _MSC_VER
    // MSVC's localtime is thread-safe since MSVC 2005
    tm = std::localtime(&t);
#else // POSIX
    tm = &localTm;
    Q_UNUSED(localtime_r(&t, tm))
#endif

    if (Q_UNLIKELY(!tm)) {
        return QString::number(timestamp);
    }

    const size_t maxBufSize = 100;
    char buffer[maxBufSize];
    const char * format = "%Y-%m-%d %H:%M:%S";
    size_t size = strftime(
        buffer, maxBufSize, (customFormat ? customFormat : format), tm);

    result += QString::fromLocal8Bit(buffer, static_cast<int>(size));

    if (options & DateTimePrint::IncludeMilliseconds) {
        qint64 msecPart = timestamp - t * 1000;
        result += QStringLiteral(".");
        result += QString::fromUtf8("%1").arg(
            msecPart, 3, 10, QChar::fromLatin1('0'));
    }

#ifndef _MSC_VER
    if (options & DateTimePrint::IncludeTimezone) {
        const char * timezone = tm->tm_zone;
        if (timezone) {
            result += QStringLiteral(" ");
            result += QString::fromLocal8Bit(timezone);
        }
    }
#endif

    if (options & DateTimePrint::IncludeNumericTimestamp) {
        result += QStringLiteral(")");
    }

    return result;
}

} // namespace quentier
