/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#include "FakeKeychainService.h"

#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>

#include <QTimerEvent>

namespace quentier {

FakeKeychainService::FakeKeychainService(QObject * parent) :
    IKeychainService(parent)
{}

FakeKeychainService::~FakeKeychainService() = default;

QFuture<void> FakeKeychainService::writePassword(
    QString service, QString key, QString password)
{
    ApplicationSettings appSettings;
    appSettings.beginGroup(service);
    appSettings.setValue(key, password);
    appSettings.endGroup();
    return threading::makeReadyFuture();
}

QFuture<QString> FakeKeychainService::readPassword(
    QString service, QString key) const
{
    ApplicationSettings appSettings;
    appSettings.beginGroup(service);
    QString password = appSettings.value(key).toString();
    appSettings.endGroup();

    if (password.isEmpty()) {
        return threading::makeExceptionalFuture<QString>(
            Exception{ErrorCode::EntryNotFound});
    }

    return threading::makeReadyFuture<QString>(std::move(password));
}

QFuture<void> FakeKeychainService::deletePassword(
    QString service, QString key)
{
    ApplicationSettings appSettings;
    appSettings.beginGroup(service);

    if (appSettings.contains(key)) {
        appSettings.remove(key);
        appSettings.endGroup();
        return threading::makeReadyFuture();
    }

    appSettings.endGroup();
    return threading::makeExceptionalFuture<QString>(
        Exception{ErrorCode::EntryNotFound});
}

QUuid FakeKeychainService::startWritePasswordJob(
    const QString & service, const QString & key, const QString & password)
{
    ApplicationSettings appSettings;
    appSettings.beginGroup(service);
    appSettings.setValue(key, password);
    appSettings.endGroup();

    const auto requestId = QUuid::createUuid();
    const int timerId = startTimer(0);
    m_writePasswordRequestIdByTimerId[timerId] = requestId;
    return requestId;
}

QUuid FakeKeychainService::startReadPasswordJob(
    const QString & service, const QString & key)
{
    ApplicationSettings appSettings;
    appSettings.beginGroup(service);
    QString password = appSettings.value(key).toString();
    appSettings.endGroup();

    const auto requestId = QUuid::createUuid();
    const int timerId = startTimer(0);

    m_readPasswordRequestIdWithPasswordByTimerId[timerId] =
        std::make_pair(requestId, std::move(password));

    return requestId;
}

QUuid FakeKeychainService::startDeletePasswordJob(
    const QString & service, const QString & key)
{
    ApplicationSettings appSettings;
    appSettings.beginGroup(service);

    bool keyFound = false;
    if (appSettings.contains(key)) {
        appSettings.remove(key);
        keyFound = true;
    }

    appSettings.endGroup();

    const auto requestId = QUuid::createUuid();
    const int timerId = startTimer(0);

    m_deletePasswordRequestIdByTimerId[timerId] =
        std::make_pair(requestId, keyFound);

    return requestId;
}

void FakeKeychainService::timerEvent(QTimerEvent * pEvent)
{
    const int timerId = pEvent->timerId();

    const auto writeIt = m_writePasswordRequestIdByTimerId.find(timerId);
    if (writeIt != m_writePasswordRequestIdByTimerId.end()) {
        pEvent->accept();
        killTimer(timerId);

        Q_EMIT writePasswordJobFinished(
            writeIt.value(), IKeychainService::ErrorCode::NoError,
            ErrorString{});

        Q_UNUSED(m_writePasswordRequestIdByTimerId.erase(writeIt))
        return;
    }

    const auto readIt =
        m_readPasswordRequestIdWithPasswordByTimerId.find(timerId);

    if (readIt != m_readPasswordRequestIdWithPasswordByTimerId.end()) {
        pEvent->accept();
        killTimer(timerId);

        const auto errorCode =
            (readIt.value().second.isEmpty()
                 ? IKeychainService::ErrorCode::EntryNotFound
                 : IKeychainService::ErrorCode::NoError);

        Q_EMIT readPasswordJobFinished(
            readIt.value().first, errorCode, ErrorString{},
            readIt.value().second);

        Q_UNUSED(m_readPasswordRequestIdWithPasswordByTimerId.erase(readIt))
        return;
    }

    const auto deleteIt = m_deletePasswordRequestIdByTimerId.find(timerId);
    if (deleteIt != m_deletePasswordRequestIdByTimerId.end()) {
        pEvent->accept();
        killTimer(timerId);

        auto errorCode =
            (deleteIt.value().second
                 ? IKeychainService::ErrorCode::NoError
                 : IKeychainService::ErrorCode::EntryNotFound);

        Q_EMIT deletePasswordJobFinished(
            deleteIt.value().first, errorCode, ErrorString{});

        Q_UNUSED(m_deletePasswordRequestIdByTimerId.erase(deleteIt))
        return;
    }
}

} // namespace quentier
