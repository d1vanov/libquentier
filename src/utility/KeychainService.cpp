/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include "KeychainService.h"
#include "QtKeychainWrapper.h"

namespace quentier {

KeychainService::KeychainService(QObject * parent) :
    IKeychainService(parent),
    m_pQtKeychainWrapper(new QtKeychainWrapper)
{
    QObject::connect(
        this,
        &KeychainService::notifyStartWritePasswordJob,
        m_pQtKeychainWrapper,
        &QtKeychainWrapper::onStartWritePasswordJob,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this,
        &KeychainService::notifyStartReadPasswordJob,
        m_pQtKeychainWrapper,
        &QtKeychainWrapper::onStartReadPasswordJob,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this,
        &KeychainService::notifyStartDeletePasswordJob,
        m_pQtKeychainWrapper,
        &QtKeychainWrapper::onStartDeletePasswordJob,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pQtKeychainWrapper,
        &QtKeychainWrapper::writePasswordJobFinished,
        this,
        &KeychainService::writePasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pQtKeychainWrapper,
        &QtKeychainWrapper::readPasswordJobFinished,
        this,
        &KeychainService::readPasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pQtKeychainWrapper,
        &QtKeychainWrapper::deletePasswordJobFinished,
        this,
        &KeychainService::deletePasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

KeychainService::~KeychainService()
{
    m_pQtKeychainWrapper->disconnect();
    m_pQtKeychainWrapper->deleteLater();
}

QUuid KeychainService::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartWritePasswordJob(jobId, service, key, password);
    return jobId;
}

QUuid KeychainService::startReadPasswordJob(
    const QString & service, const QString & key)
{
    QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartReadPasswordJob(jobId, service, key);
    return jobId;
}

QUuid KeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartDeletePasswordJob(jobId, service, key);
    return jobId;
}

} // namespace quentier
