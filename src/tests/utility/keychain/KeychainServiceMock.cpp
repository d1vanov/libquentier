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

#include "KeychainServiceMock.h"

#include <QMetaObject>

#include <stdexcept>

namespace quentier {
namespace test {

KeychainServiceMock::KeychainServiceMock(QObject * parent) :
    IKeychainService(parent)
{}

void KeychainServiceMock::setWritePasswordHandler(WritePasswordHandler handler)
{
    m_writePasswordHandler = handler;
}

void KeychainServiceMock::setReadPasswordHandler(ReadPasswordHandler handler)
{
    m_readPasswordHandler = handler;
}

void KeychainServiceMock::setDeletePasswordHandler(
    DeletePasswordHandler handler)
{
    m_deletePasswordHandler = handler;
}

QUuid KeychainServiceMock::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    if (!m_writePasswordHandler) {
        throw std::logic_error{
            "KeychainServiceMock: write password handler is not set"};
    }

    const auto res = m_writePasswordHandler(service, key, password);

    QMetaObject::invokeMethod(
        this, "writePasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, res.m_requestId), Q_ARG(ErrorCode, res.m_errorCode),
        Q_ARG(ErrorString, res.m_errorDescription));

    return res.m_requestId;
}

QUuid KeychainServiceMock::startReadPasswordJob(
    const QString & service, const QString & key)
{
    if (!m_readPasswordHandler) {
        throw std::logic_error{
            "KeychainServiceMock: read password handler is not set"};
    }

    const auto res = m_readPasswordHandler(service, key);

    QMetaObject::invokeMethod(
        this, "readPasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, res.m_requestId), Q_ARG(ErrorCode, res.m_errorCode),
        Q_ARG(ErrorString, res.m_errorDescription),
        Q_ARG(QString, res.m_password));

    return res.m_requestId;
}

QUuid KeychainServiceMock::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    if (!m_deletePasswordHandler) {
        throw std::logic_error{
            "KeychainServiceMock: delete password handler is not set"};
    }

    const auto res = m_deletePasswordHandler(service, key);

    QMetaObject::invokeMethod(
        this, "deletePasswordJobFinished", Qt::QueuedConnection,
        Q_ARG(QUuid, res.m_requestId), Q_ARG(ErrorCode, res.m_errorCode),
        Q_ARG(ErrorString, res.m_errorDescription));

    return res.m_requestId;
}

} // namespace test
} // namespace quentier
