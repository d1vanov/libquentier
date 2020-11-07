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

#include "QtKeychainWrapper.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

QtKeychainWrapper::QtKeychainWrapper() = default;

QtKeychainWrapper::~QtKeychainWrapper()
{
    for (auto it = m_readPasswordJobs.begin(), end = m_readPasswordJobs.end();
         it != end; ++it)
    {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_readPasswordJobs.clear();

    for (auto it = m_writePasswordJobs.begin(), end = m_writePasswordJobs.end();
         it != end; ++it)
    {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_writePasswordJobs.clear();

    for (auto it = m_deletePasswordJobs.begin(),
              end = m_deletePasswordJobs.end();
         it != end; ++it)
    {
        it.key()->disconnect();
        it.key()->deleteLater();
    }
    m_deletePasswordJobs.clear();
}

void QtKeychainWrapper::onStartWritePasswordJob(
    QUuid jobId, QString service, QString key, QString password)
{
    auto * pJob = new QKeychain::WritePasswordJob(service, this);
    pJob->setKey(key);
    pJob->setTextData(password);
    pJob->setAutoDelete(false);

    m_writePasswordJobs[pJob] = jobId;

    QObject::connect(
        pJob, &QKeychain::WritePasswordJob::finished, this,
        &QtKeychainWrapper::onWritePasswordJobFinished);

    QNDEBUG(
        "utiity:qtkeychain",
        "Starting write password job for service "
            << service << "; key = " << key << ", job id = " << jobId);

    pJob->start();
}

void QtKeychainWrapper::onStartReadPasswordJob(
    QUuid jobId, QString service, QString key)
{
    auto * pJob = new QKeychain::ReadPasswordJob(service, this);
    pJob->setAutoDelete(false);
    pJob->setKey(key);

    m_readPasswordJobs[pJob] = jobId;

    QObject::connect(
        pJob, &QKeychain::ReadPasswordJob::finished, this,
        &QtKeychainWrapper::onReadPasswordJobFinished);

    QNDEBUG(
        "utiity:qtkeychain",
        "Starting read password job for service "
            << service << "; key = " << key << ", job id = " << jobId);

    pJob->start();
}

void QtKeychainWrapper::onStartDeletePasswordJob(
    QUuid jobId, QString service, QString key)
{
    auto * pJob = new QKeychain::DeletePasswordJob(service, this);
    pJob->setAutoDelete(false);
    pJob->setKey(key);

    m_deletePasswordJobs[pJob] = jobId;

    QObject::connect(
        pJob, &QKeychain::DeletePasswordJob::finished, this,
        &QtKeychainWrapper::onDeletePasswordJobFinished);

    QNDEBUG(
        "utiity:qtkeychain",
        "Starting delete password job for service "
            << service << "; key = " << key << ", job id = " << jobId);

    pJob->start();
}

void QtKeychainWrapper::onWritePasswordJobFinished(QKeychain::Job * pJob)
{
    QNDEBUG(
        "utiity:qtkeychain", "QtKeychainWrapper::onWritePasswordJobFinished");

    auto * pWritePasswordJob =
        qobject_cast<QKeychain::WritePasswordJob *>(pJob);
    auto it = m_writePasswordJobs.find(pWritePasswordJob);
    if (Q_UNLIKELY(it == m_writePasswordJobs.end())) {
        QNWARNING(
            "utiity:qtkeychain",
            "Failed to find the write password "
                << "job's corresponding id");
        return;
    }

    QUuid jobId = it.value();
    auto errorCode = translateErrorCode(pWritePasswordJob->error());
    ErrorString errorDescription(pWritePasswordJob->errorString());

    QObject::disconnect(
        pWritePasswordJob, &QKeychain::WritePasswordJob::finished, this,
        &QtKeychainWrapper::onWritePasswordJobFinished);

    pWritePasswordJob->deleteLater();
    m_writePasswordJobs.erase(it);

    QNDEBUG(
        "utiity:qtkeychain",
        "Finished write password job with id "
            << jobId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    Q_EMIT writePasswordJobFinished(jobId, errorCode, errorDescription);
}

void QtKeychainWrapper::onReadPasswordJobFinished(QKeychain::Job * pJob)
{
    QNDEBUG(
        "utiity:qtkeychain", "QtKeychainWrapper::onReadPasswordJobFinished");

    auto * pReadPasswordJob = qobject_cast<QKeychain::ReadPasswordJob *>(pJob);
    auto it = m_readPasswordJobs.find(pReadPasswordJob);
    if (Q_UNLIKELY(it == m_readPasswordJobs.end())) {
        QNWARNING(
            "utiity:qtkeychain",
            "Failed to find the read password job's "
                << "corresponding id");
        return;
    }

    QUuid jobId = it.value();
    auto errorCode = translateErrorCode(pReadPasswordJob->error());

    ErrorString errorDescription;

    if (pReadPasswordJob->error() == QKeychain::EntryNotFound) {
        errorDescription.setBase(
            QT_TR_NOOP("Unexpectedly missing OAuth token in the keychain"));

        errorDescription.details() = pReadPasswordJob->errorString();
    }
    else {
        errorDescription.setBase(pReadPasswordJob->errorString());
    }

    QString password = pReadPasswordJob->textData();

    QObject::disconnect(
        pReadPasswordJob, &QKeychain::ReadPasswordJob::finished, this,
        &QtKeychainWrapper::onReadPasswordJobFinished);

    pReadPasswordJob->deleteLater();
    m_readPasswordJobs.erase(it);

    QNDEBUG(
        "utiity:qtkeychain",
        "Finished read password job with id "
            << jobId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    Q_EMIT readPasswordJobFinished(
        jobId, errorCode, errorDescription, password);
}

void QtKeychainWrapper::onDeletePasswordJobFinished(QKeychain::Job * pJob)
{
    QNDEBUG(
        "utiity:qtkeychain", "QtKeychainWrapper::onDeletePasswordJobFinished");

    auto * pDeletePasswordJob =
        qobject_cast<QKeychain::DeletePasswordJob *>(pJob);

    auto it = m_deletePasswordJobs.find(pDeletePasswordJob);
    if (Q_UNLIKELY(it == m_deletePasswordJobs.end())) {
        QNWARNING(
            "utiity:qtkeychain",
            "Failed to find the delete password "
                << "job's corresponding id");
        return;
    }

    QUuid jobId = it.value();
    auto errorCode = translateErrorCode(pDeletePasswordJob->error());
    ErrorString errorDescription(pDeletePasswordJob->errorString());

    QObject::disconnect(
        pDeletePasswordJob, &QKeychain::DeletePasswordJob::finished, this,
        &QtKeychainWrapper::onDeletePasswordJobFinished);

    pDeletePasswordJob->deleteLater();
    m_deletePasswordJobs.erase(it);

    QNDEBUG(
        "utiity:qtkeychain",
        "Finished delete password job with id "
            << jobId << ", error code = " << errorCode
            << ", error description = " << errorDescription);

    Q_EMIT deletePasswordJobFinished(jobId, errorCode, errorDescription);
}

IKeychainService::ErrorCode QtKeychainWrapper::translateErrorCode(
    const QKeychain::Error errorCode) const
{
    using ErrorCode = IKeychainService::ErrorCode;

    switch (errorCode) {
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
