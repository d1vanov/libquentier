/*
 * Copyright 2017-2020 Dmitry Ivanov
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

namespace quentier {

const QString applicationPersistentStoragePath(bool * pNonStandardLocation)
{
    QByteArray envOverride = qgetenv(LIBQUENTIER_PERSISTENCE_STORAGE_PATH);
    if (!envOverride.isEmpty()) {
        if (pNonStandardLocation) {
            *pNonStandardLocation = true;
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

const QString accountPersistentStoragePath(const Account & account)
{
    QString storagePath = applicationPersistentStoragePath();
    if (Q_UNLIKELY(storagePath.isEmpty())) {
        return storagePath;
    }

    if (Q_UNLIKELY(account.isEmpty())) {
        return storagePath;
    }

    QString accountName = account.name();
    if (Q_UNLIKELY(accountName.isEmpty())) {
        return storagePath;
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

const QString applicationTemporaryStoragePath()
{
    QString path;
    path = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    path += QStringLiteral("/") + QCoreApplication::applicationName();
    return path;
}

const QString homePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
}

const QString documentsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

} // namespace quentier
