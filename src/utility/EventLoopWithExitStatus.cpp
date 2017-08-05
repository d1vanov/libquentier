/*
 * Copyright 2016 Dmitry Ivanov
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

namespace quentier {

EventLoopWithExitStatus::EventLoopWithExitStatus(QObject * parent) :
    QEventLoop(parent),
    m_exitStatus(ExitStatus::Success),
    m_errorDescription()
{}

EventLoopWithExitStatus::ExitStatus::type EventLoopWithExitStatus::exitStatus() const
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
    QEventLoop::exit(m_exitStatus);
}

void EventLoopWithExitStatus::exitAsFailure()
{
    m_exitStatus = ExitStatus::Failure;
    QEventLoop::exit(m_exitStatus);
}

void EventLoopWithExitStatus::exitAsTimeout()
{
    m_exitStatus = ExitStatus::Timeout;
    QEventLoop::exit(m_exitStatus);
}

void EventLoopWithExitStatus::exitAsFailureWithError(QString errorDescription)
{
    m_errorDescription = ErrorString(errorDescription);
    m_exitStatus = ExitStatus::Failure;
    QEventLoop::exit(m_exitStatus);
}

void EventLoopWithExitStatus::exitAsFailureWithErrorString(ErrorString errorDescription)
{
    m_errorDescription = errorDescription;
    m_exitStatus = ExitStatus::Failure;
    QEventLoop::exit(m_exitStatus);
}

} // namespace quentier
