/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include <quentier/exception/IQuentierException.h>

#include <cstddef>
#include <cstring>
#include <ostream>

namespace quentier {

#define INIT_WHAT_MESSAGE()                                                    \
    const QByteArray bytes = m_message.nonLocalizedString().toLocal8Bit();     \
    const auto size = bytes.size();                                            \
    if (size >= 0) {                                                           \
        std::size_t usize = static_cast<std::size_t>(size);                    \
        m_whatMessage = new char[usize + 1];                                   \
        Q_UNUSED(strncpy(m_whatMessage, bytes.constData(), usize))             \
        m_whatMessage[usize] = '\0';                                           \
    }

IQuentierException::IQuentierException(ErrorString message) :
    m_message(std::move(message)){INIT_WHAT_MESSAGE()}

    IQuentierException::~IQuentierException() noexcept
{
    delete[] m_whatMessage;
}

IQuentierException::IQuentierException(const IQuentierException & other) :
    m_message(other.m_message){INIT_WHAT_MESSAGE()}

    IQuentierException
    & IQuentierException::operator=(const IQuentierException & other)
{
    if (this != &other) {
        m_message = other.m_message;

        delete m_whatMessage;
        m_whatMessage = nullptr;
        INIT_WHAT_MESSAGE()
    }

    return *this;
}

ErrorString IQuentierException::errorMessage() const
{
    return m_message;
}

QString IQuentierException::localizedErrorMessage() const
{
    return m_message.localizedString();
}

QString IQuentierException::nonLocalizedErrorMessage() const
{
    return m_message.nonLocalizedString();
}

const char * IQuentierException::what() const noexcept
{
    return m_whatMessage;
}

QTextStream & IQuentierException::print(QTextStream & strm) const
{
    strm << "\n <" << exceptionDisplayName() << ">";
    strm << "\n message: " << m_message.nonLocalizedString();
    return strm;
}

} // namespace quentier
