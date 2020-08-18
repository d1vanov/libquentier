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

#include <quentier/utility/EventLoopWithExitStatus.h>

#include <QDebug>
#include <QTextStream>

#define PRINT_EXIT_STATUS(strm, status)                                        \
    using ExitStatus = EventLoopWithExitStatus::ExitStatus;                    \
    switch (status) {                                                          \
    case ExitStatus::Success:                                                  \
        strm << "Success";                                                     \
        break;                                                                 \
    case ExitStatus::Failure:                                                  \
        strm << "Failure";                                                     \
        break;                                                                 \
    case ExitStatus::Timeout:                                                  \
        strm << "Timeout";                                                     \
        break;                                                                 \
    default:                                                                   \
        strm << "Unknown (" << static_cast<qint64>(status) << ")";             \
        break;                                                                 \
    }

namespace quentier {

EventLoopWithExitStatus::EventLoopWithExitStatus(QObject * parent) :
    QEventLoop(parent), m_exitStatus(ExitStatus::Success), m_errorDescription()
{}

EventLoopWithExitStatus::ExitStatus EventLoopWithExitStatus::exitStatus() const
{
    return m_exitStatus;
}

const ErrorString & EventLoopWithExitStatus::errorDescription() const
{
    return m_errorDescription;
}

void EventLoopWithExitStatus::exitAsSuccess()
{
    m_exitStatus = ExitStatus::Success;
    QEventLoop::exit(static_cast<int>(m_exitStatus));
}

void EventLoopWithExitStatus::exitAsFailure()
{
    m_exitStatus = ExitStatus::Failure;
    QEventLoop::exit(static_cast<int>(m_exitStatus));
}

void EventLoopWithExitStatus::exitAsTimeout()
{
    m_exitStatus = ExitStatus::Timeout;
    QEventLoop::exit(static_cast<int>(m_exitStatus));
}

void EventLoopWithExitStatus::exitAsFailureWithError(QString errorDescription)
{
    m_errorDescription = ErrorString(errorDescription);
    m_exitStatus = ExitStatus::Failure;
    QEventLoop::exit(static_cast<int>(m_exitStatus));
}

void EventLoopWithExitStatus::exitAsFailureWithErrorString(
    ErrorString errorDescription)
{
    m_errorDescription = errorDescription;
    m_exitStatus = ExitStatus::Failure;
    QEventLoop::exit(static_cast<int>(m_exitStatus));
}

QDebug & operator<<(
    QDebug & dbg, const EventLoopWithExitStatus::ExitStatus status)
{
    PRINT_EXIT_STATUS(dbg, status)
    return dbg;
}

QTextStream & operator<<(
    QTextStream & strm, const EventLoopWithExitStatus::ExitStatus status)
{
    PRINT_EXIT_STATUS(strm, status);
    return strm;
}

} // namespace quentier
