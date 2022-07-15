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

#include <qt5keychain/keychain.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

namespace quentier {

namespace {

IKeychainService::ErrorCode translateErrorCode(
    const QKeychain::Error errorCode) noexcept
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

} // namespace

QtKeychainService::~QtKeychainService() noexcept = default;

QFuture<void> QtKeychainService::writePassword(
    QString service, QString key, QString password) // NOLINT
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    auto job = std::make_unique<QKeychain::WritePasswordJob>(service);
    job->setKey(key);
    job->setTextData(password);

    QObject::connect(
        job.get(),
        &QKeychain::WritePasswordJob::finished,
        job.get(),
        [promise](QKeychain::Job * job)
        {
            Q_ASSERT(job);
            const auto errorCode = job->error();
            if (errorCode != QKeychain::NoError) {
                promise->setException(
                    IKeychainService::Exception(translateErrorCode(errorCode)));
            }

            promise->finish();
        });

    job->start();
    Q_UNUSED(job.release());
    return future;
}

QFuture<QString> QtKeychainService::readPassword(
    QString service, QString key) const // NOLINT
{
    auto promise = std::make_shared<QPromise<QString>>();
    auto future = promise->future();

    promise->start();

    auto job = std::make_unique<QKeychain::ReadPasswordJob>(service);
    job->setKey(key);

    QObject::connect(
        job.get(),
        &QKeychain::ReadPasswordJob::finished,
        job.get(),
        [promise](QKeychain::Job * job)
        {
            Q_ASSERT(job);

            auto * readPasswordJob =
                qobject_cast<QKeychain::ReadPasswordJob*>(job);
            Q_ASSERT(readPasswordJob);

            const auto errorCode = job->error();
            if (errorCode == QKeychain::NoError) {
                promise->addResult(readPasswordJob->textData());
            }
            else {
                promise->setException(
                    IKeychainService::Exception(translateErrorCode(errorCode)));
            }

            promise->finish();
        });

    job->start();
    Q_UNUSED(job.release());
    return future;
}

QFuture<void> QtKeychainService::deletePassword(QString service, QString key) // NOLINT
{
    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();

    promise->start();

    auto job = std::make_unique<QKeychain::DeletePasswordJob>(service);
    job->setKey(key);

    QObject::connect(
        job.get(),
        &QKeychain::DeletePasswordJob::finished,
        job.get(),
        [promise](QKeychain::Job * job)
        {
            Q_ASSERT(job);
            const auto errorCode = job->error();
            if (errorCode != QKeychain::NoError) {
                promise->setException(
                    IKeychainService::Exception(translateErrorCode(errorCode)));
            }

            promise->finish();
        });

    job->start();
    Q_UNUSED(job.release());
    return future;
}

} // namespace quentier
