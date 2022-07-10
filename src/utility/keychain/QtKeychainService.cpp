/*
 * Copyright 2018-2022 Dmitry Ivanov
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

#include "QtKeychainService.h"
#include "QtKeychainWrapper.h"

namespace quentier {

QtKeychainService::QtKeychainService(QObject * parent) :
    IKeychainService(parent), m_pQtKeychainWrapper(new QtKeychainWrapper)
{
    QObject::connect(
        this, &QtKeychainService::notifyStartWritePasswordJob,
        m_pQtKeychainWrapper, &QtKeychainWrapper::onStartWritePasswordJob,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &QtKeychainService::notifyStartReadPasswordJob,
        m_pQtKeychainWrapper, &QtKeychainWrapper::onStartReadPasswordJob,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        this, &QtKeychainService::notifyStartDeletePasswordJob,
        m_pQtKeychainWrapper, &QtKeychainWrapper::onStartDeletePasswordJob,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pQtKeychainWrapper, &QtKeychainWrapper::writePasswordJobFinished,
        this, &QtKeychainService::writePasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pQtKeychainWrapper, &QtKeychainWrapper::readPasswordJobFinished, this,
        &QtKeychainService::readPasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));

    QObject::connect(
        m_pQtKeychainWrapper, &QtKeychainWrapper::deletePasswordJobFinished,
        this, &QtKeychainService::deletePasswordJobFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

QtKeychainService::~QtKeychainService() noexcept
{
    m_pQtKeychainWrapper->disconnect();
    m_pQtKeychainWrapper->deleteLater();
}

QFuture<void> QtKeychainService::writePassword(
    QString service, QString key, QString password)
{
    return m_pQtKeychainWrapper->writePassword(
        std::move(service), std::move(key), std::move(password));
}

QFuture<QString> QtKeychainService::readPassword(
    QString service, QString key) const
{
    return m_pQtKeychainWrapper->readPassword(
        std::move(service), std::move(key));
}

QFuture<void> QtKeychainService::deletePassword(QString service, QString key)
{
    return m_pQtKeychainWrapper->deletePassword(
        std::move(service), std::move(key));
}

QUuid QtKeychainService::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    const QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartWritePasswordJob(jobId, service, key, password);
    return jobId;
}

QUuid QtKeychainService::startReadPasswordJob(
    const QString & service, const QString & key)
{
    const QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartReadPasswordJob(jobId, service, key);
    return jobId;
}

QUuid QtKeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    const QUuid jobId = QUuid::createUuid();
    Q_EMIT notifyStartDeletePasswordJob(jobId, service, key);
    return jobId;
}

} // namespace quentier
