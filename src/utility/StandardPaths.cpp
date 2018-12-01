/*
 * Copyright 2017 Dmitry Ivanov
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

#include <quentier/utility/StandardPaths.h>
#include <quentier/logging/QuentierLogger.h>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QStandardPaths>
#endif

#include <QDesktopServices>
#include <QCoreApplication>

namespace quentier {

const QString applicationPersistentStoragePath(bool * pNonStandardLocation)
{
    QByteArray envOverride = qgetenv(LIBQUENTIER_PERSISTENCE_STORAGE_PATH);
    if (!envOverride.isEmpty())
    {
        if (pNonStandardLocation) {
            *pNonStandardLocation = true;
        }

        return QString::fromLocal8Bit(envOverride);
    }

#if defined(Q_OS_MAC) || defined (Q_OS_WIN)
    // FIXME: clarify in which version the enum item was actually renamed
    // Seriously, WTF is going on? Why the API gets changed within the major release?
    // Who is the moron who has authorized that?
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0)
    return QStandardPaths::writableLocation(QStandardPaths::DataLocation);
#else
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#endif
#else
    return QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#endif
#else // Linux, BSD-derivatives etc
    QString storagePath;
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    storagePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
#else
    storagePath = QDesktopServices::storageLocation(QDesktopServices::HomeLocation);
#endif
    storagePath += QStringLiteral("/.") + QCoreApplication::applicationName().toLower();
    return storagePath;
#endif // Q_OS_<smth>
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

    if (account.type() == Account::Type::Local)
    {
        storagePath += QStringLiteral("/LocalAccounts/");
        storagePath += accountName;
    }
    else
    {
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
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    path = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
#else
    path = QDesktopServices::storageLocation(QDesktopServices::TempLocation);
#endif
    path += QStringLiteral("/") + QCoreApplication::applicationName();
    return path;
}

const QString homePath()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    return QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
#else
    return QDesktopServices::storageLocation(QDesktopServices::HomeLocation);
#endif
}

const QString documentsPath()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#else
    return QDesktopServices::storageLocation(QDesktopServices::DocumentsLocation);
#endif
}

} // namespace quentier
