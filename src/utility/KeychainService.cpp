/*
 * Copyright 2018-2019 Dmitry Ivanov
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
    QObject::connect(this,
                     QNSIGNAL(KeychainService,notifyStartWritePasswordJob,
                              QUuid,QString,QString,QString),
                     m_pQtKeychainWrapper,
                     QNSLOT(QtKeychainWrapper,onStartWritePasswordJob,
                            QUuid,QString,QString,QString),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(this,
                     QNSIGNAL(KeychainService,notifyStartReadPasswordJob,
                              QUuid,QString,QString),
                     m_pQtKeychainWrapper,
                     QNSLOT(QtKeychainWrapper,onStartReadPasswordJob,
                            QUuid,QString,QString),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(this,
                     QNSIGNAL(KeychainService,notifyStartDeletePasswordJob,
                              QUuid,QString,QString),
                     m_pQtKeychainWrapper,
                     QNSLOT(QtKeychainWrapper,onStartDeletePasswordJob,
                            QUuid,QString,QString),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(m_pQtKeychainWrapper,
                     QNSIGNAL(QtKeychainWrapper,writePasswordJobFinished,
                              QUuid,ErrorCode::type,ErrorString),
                     this,
                     QNSIGNAL(KeychainService,writePasswordJobFinished,
                              QUuid,ErrorCode::type,ErrorString),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(m_pQtKeychainWrapper,
                     QNSIGNAL(QtKeychainWrapper,readPasswordJobFinished,
                              QUuid,ErrorCode::type,ErrorString,QString),
                     this,
                     QNSIGNAL(KeychainService,readPasswordJobFinished,
                              QUuid,ErrorCode::type,ErrorString,QString),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
    QObject::connect(m_pQtKeychainWrapper,
                     QNSIGNAL(QtKeychainWrapper,deletePasswordJobFinished,
                              QUuid,ErrorCode::type,ErrorString),
                     this,
                     QNSIGNAL(KeychainService,deletePasswordJobFinished,
                              QUuid,ErrorCode::type,ErrorString),
                     Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

KeychainService::~KeychainService()
{
    m_pQtKeychainWrapper->disconnect();
    m_pQtKeychainWrapper->deleteLater();
}

QUuid KeychainService::startWritePasswordJob(const QString & service,
                                             const QString & key,
                                             const QString & password)
{
    QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartWritePasswordJob(jobId, service, key, password);
    return jobId;
}

QUuid KeychainService::startReadPasswordJob(const QString & service,
                                            const QString & key)
{
    QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartReadPasswordJob(jobId, service, key);
    return jobId;
}

QUuid KeychainService::startDeletePasswordJob(const QString & service,
                                              const QString & key)
{
    QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartDeletePasswordJob(jobId, service, key);
    return jobId;
}

} // namespace quentier
