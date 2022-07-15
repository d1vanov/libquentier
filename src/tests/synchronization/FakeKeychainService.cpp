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

#include "FakeKeychainService.h"

#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>

#include <QTimerEvent>

namespace quentier {

FakeKeychainService::~FakeKeychainService() noexcept = default;

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

} // namespace quentier
