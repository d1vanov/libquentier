/*
 * Copyright 2017-2025 Dmitry Ivanov
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

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/StandardPaths.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QStandardPaths>

namespace quentier::utility {

QString applicationPersistentStoragePath(bool * nonStandardLocation)
{
    const QByteArray envOverride = qgetenv(gLibquentierPersistenceStoragePath);

    if (!envOverride.isEmpty()) {
        if (nonStandardLocation) {
            *nonStandardLocation = true;
        }

        return QString::fromLocal8Bit(envOverride);
    }

#if defined(Q_OS_MAC) || defined(Q_OS_WIN)
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else // Linux, BSD-derivatives etc
    QString storagePath;
    storagePath =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);

    storagePath +=
        QStringLiteral("/.") + QCoreApplication::applicationName().toLower();

    return storagePath;
#endif
}

QString accountPersistentStoragePath(const Account & account)
{
    QString storagePath = applicationPersistentStoragePath();
    if (Q_UNLIKELY(storagePath.isEmpty())) {
        return {};
    }

    if (Q_UNLIKELY(account.isEmpty())) {
        return {};
    }

    QString accountName = account.name();
    if (Q_UNLIKELY(accountName.isEmpty())) {
        return {};
    }

    if (account.type() == Account::Type::Local) {
        storagePath += QStringLiteral("/LocalAccounts/");
        storagePath += accountName;
    }
    else {
        storagePath += QStringLiteral("/EvernoteAccounts/");
        storagePath += accountName;
        storagePath += QStringLiteral("_");
        storagePath += account.evernoteHost();
        storagePath += QStringLiteral("_");
        storagePath += QString::number(account.id());
    }

    return storagePath;
}

QString applicationTemporaryStoragePath()
{
    QString path =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);

    path += QStringLiteral("/") + QCoreApplication::applicationName();
    return path;
}

QString homePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}

QString documentsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

} // namespace quentier::utility
