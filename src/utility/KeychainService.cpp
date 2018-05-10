/*
 * Copyright 2018 Dmitry Ivanov
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
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

KeychainService::KeychainService(QObject * parent) :
    IKeychainService(parent)
{}

KeychainService::~KeychainService()
{
    for(auto it = m_readPasswordJobs.begin(), end = m_readPasswordJobs.end(); it != end; ++it) {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_readPasswordJobs.clear();

    for(auto it = m_writePasswordJobs.begin(), end = m_writePasswordJobs.end(); it != end; ++it) {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_writePasswordJobs.clear();

    for(auto it = m_deletePasswordJobs.begin(), end = m_deletePasswordJobs.end(); it != end; ++it) {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_deletePasswordJobs.clear();
}

QUuid KeychainService::startWritePasswordJob(const QString & service, const QString & key, const QString & password)
{
    QUuid jobId = QUuid::createUuid();
    QKeychain::WritePasswordJob * pJob = new QKeychain::WritePasswordJob(service);
    pJob->setKey(key);
    pJob->setTextData(password);
    pJob->setAutoDelete(false);
    m_writePasswordJobs[pJob] = jobId;
    QObject::connect(pJob, QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(KeychainService,onWritePasswordJobFinished,QKeychain::Job*));
    QNDEBUG(QStringLiteral("Starting write password job for service ") << service << QStringLiteral("; key = ")
            << key << QStringLiteral(", job id = ") << jobId);
    pJob->start();
    return jobId;
}

QUuid KeychainService::startReadPasswordJob(const QString & service, const QString & key)
{
    QUuid jobId = QUuid::createUuid();
    QKeychain::ReadPasswordJob * pJob = new QKeychain::ReadPasswordJob(service);
    pJob->setAutoDelete(false);
    pJob->setKey(key);
    m_readPasswordJobs[pJob] = jobId;
    QObject::connect(pJob, QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(KeychainService,onReadPasswordJobFinished,QKeychain::Job*));
    QNDEBUG(QStringLiteral("Starting read password job for service ") << service << QStringLiteral("; key = ")
            << key << QStringLiteral(", job id = ") << jobId);
    pJob->start();
    return jobId;
}

QUuid KeychainService::startDeletePasswordJob(const QString & service, const QString & key)
{
    QUuid jobId = QUuid::createUuid();
    QKeychain::DeletePasswordJob * pJob = new QKeychain::DeletePasswordJob(service);
    pJob->setAutoDelete(false);
    pJob->setKey(key);
    m_deletePasswordJobs[pJob] = jobId;
    QObject::connect(pJob, QNSIGNAL(QKeychain::DeletePasswordJob,finished,QKeychain::Job*),
                     this, QNSLOT(KeychainService,onDeletePasswordJobFinished,QKeychain::Job*));
    QNDEBUG(QStringLiteral("Starting delete password job for service ") << service << QStringLiteral("; key = ")
            << key << QStringLiteral(", job id = ") << jobId);
    pJob->start();
    return jobId;
}

void KeychainService::onWritePasswordJobFinished(QKeychain::Job * pJob)
{
    QNDEBUG(QStringLiteral("KeychainService::onWritePasswordJobFinished"));

    QKeychain::WritePasswordJob * pWritePasswordJob = qobject_cast<QKeychain::WritePasswordJob*>(pJob);
    auto it = m_writePasswordJobs.find(pWritePasswordJob);
    if (Q_UNLIKELY(it == m_writePasswordJobs.end())) {
        QNWARNING(QStringLiteral("Failed to find the write password job's corresponding job id"));
        return;
    }

    QUuid jobId = it.value();
    ErrorCode::type errorCode = translateErrorCode(pWritePasswordJob->error());
    ErrorString errorDescription(pWritePasswordJob->errorString());

    QObject::disconnect(pWritePasswordJob, QNSIGNAL(QKeychain::WritePasswordJob,finished,QKeychain::Job*),
                        this, QNSLOT(KeychainService,onWritePasswordJobFinished,QKeychain::Job*));
    pWritePasswordJob->deleteLater();
    m_writePasswordJobs.erase(it);

    QNDEBUG(QStringLiteral("Finished write password job with id ") << jobId
            << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);
    Q_EMIT writePasswordJobFinished(jobId, errorCode, errorDescription);
}

void KeychainService::onReadPasswordJobFinished(QKeychain::Job * pJob)
{
    QNDEBUG(QStringLiteral("KeychainService::onReadPasswordJobFinished"));

    QKeychain::ReadPasswordJob * pReadPasswordJob = qobject_cast<QKeychain::ReadPasswordJob*>(pJob);
    auto it = m_readPasswordJobs.find(pReadPasswordJob);
    if (Q_UNLIKELY(it == m_readPasswordJobs.end())) {
        QNWARNING(QStringLiteral("Failed to find the read password job's corresponding job id"));
        return;
    }

    QUuid jobId = it.value();
    ErrorCode::type errorCode = translateErrorCode(pReadPasswordJob->error());

    ErrorString errorDescription;
    if (pReadPasswordJob->error() == QKeychain::EntryNotFound) {
        errorDescription.setBase(QT_TR_NOOP("Unexpectedly missing OAuth token in the keychain"));
        errorDescription.details() = pReadPasswordJob->errorString();
    }
    else {
        errorDescription.setBase(pReadPasswordJob->errorString());
    }
    
    QString password = pReadPasswordJob->textData();

    QObject::disconnect(pReadPasswordJob, QNSIGNAL(QKeychain::ReadPasswordJob,finished,QKeychain::Job*),
                        this, QNSLOT(KeychainService,onReadPasswordJobFinished,QKeychain::Job*));
    pReadPasswordJob->deleteLater();
    m_readPasswordJobs.erase(it);

    QNDEBUG(QStringLiteral("Finished read password job with id ") << jobId
            << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);
    Q_EMIT readPasswordJobFinished(jobId, errorCode, errorDescription, password);
}

void KeychainService::onDeletePasswordJobFinished(QKeychain::Job * pJob)
{
    QNDEBUG(QStringLiteral("KeychainService::onDeletePasswordJobFinished"));

    QKeychain::DeletePasswordJob * pDeletePasswordJob = qobject_cast<QKeychain::DeletePasswordJob*>(pJob);
    auto it = m_deletePasswordJobs.find(pDeletePasswordJob);
    if (Q_UNLIKELY(it == m_deletePasswordJobs.end())) {
        QNWARNING(QStringLiteral("Failed to find the delete password job's corresponding job id"));
        return;
    }

    QUuid jobId = it.value();
    ErrorCode::type errorCode = translateErrorCode(pDeletePasswordJob->error());
    ErrorString errorDescription(pDeletePasswordJob->errorString());

    QObject::disconnect(pDeletePasswordJob, QNSIGNAL(QKeychain::DeletePasswordJob,finished,QKeychain::Job*),
                        this, QNSLOT(KeychainService,onDeletePasswordJobFinished,QKeychain::Job*));
    pDeletePasswordJob->deleteLater();
    m_deletePasswordJobs.erase(it);

    QNDEBUG(QStringLiteral("Finished delete password job with id ") << jobId
            << QStringLiteral(", error code = ") << errorCode
            << QStringLiteral(", error description = ") << errorDescription);
    Q_EMIT deletePasswordJobFinished(jobId, errorCode, errorDescription);
}

IKeychainService::ErrorCode::type KeychainService::translateErrorCode(const QKeychain::Error errorCode) const
{
    switch(errorCode)
    {
    case QKeychain::NoError:
        return ErrorCode::NoError;
    case QKeychain::EntryNotFound:
        return ErrorCode::EntryNotFound;
    case QKeychain::CouldNotDeleteEntry:
        return ErrorCode::CouldNotDeleteEntry;
    case QKeychain::AccessDeniedByUser:
        return ErrorCode::AccessDeniedByUser;
    case QKeychain::AccessDenied:
        return ErrorCode::AccessDenied;
    case QKeychain::NoBackendAvailable:
        return ErrorCode::NoBackendAvailable;
    case QKeychain::NotImplemented:
        return ErrorCode::NotImplemented;
    case QKeychain::OtherError:
    default:
        return ErrorCode::OtherError;
    }
}

} // namespace quentier
