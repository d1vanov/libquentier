/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include <quentier/exception/ApplicationSettingsInitializationException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/StandardPaths.h>

#include <QApplication>

namespace quentier {

namespace {

////////////////////////////////////////////////////////////////////////////////

QString defaultApplicationStoragePath(const QString & settingsName)
{
    QString storagePath = applicationPersistentStoragePath();
    if (Q_UNLIKELY(storagePath.isEmpty())) {
        throw ApplicationSettingsInitializationException(
            ErrorString(QT_TRANSLATE_NOOP(
                "ApplicationSettings",
                "Can't create ApplicationSettings instance: "
                "no persistent storage path")));
    }

    storagePath += QStringLiteral("/settings/");

    if (!settingsName.isEmpty()) {
        storagePath += settingsName;
    }
    else {
        QString appName = QApplication::applicationName();
        if (!appName.isEmpty()) {
            storagePath += appName;
        }
        else {
            storagePath += QStringLiteral("config");
        }
    }

    storagePath += QStringLiteral(".ini");
    return storagePath;
}

QString accountApplicationStoragePath(
    const Account & account, const QString & settingsName)
{
    QString accountName = account.name();
    if (Q_UNLIKELY(accountName.isEmpty())) {
        QNWARNING(
            "utility",
            "Detected attempt to create ApplicationSettings "
            "for account with empty name");
        throw ApplicationSettingsInitializationException(
            ErrorString(QT_TRANSLATE_NOOP(
                "ApplicationSettings",
                "Can't create ApplicationSettings instance: "
                "the account name is empty")));
    }

    QString storagePath = accountPersistentStoragePath(account);
    if (Q_UNLIKELY(storagePath.isEmpty())) {
        throw ApplicationSettingsInitializationException(
            ErrorString(QT_TRANSLATE_NOOP(
                "ApplicationSettings",
                "Can't create ApplicationSettings instance: "
                "no account persistent storage path")));
    }

    storagePath += QStringLiteral("/settings/");

    if (!settingsName.isEmpty()) {
        storagePath += settingsName;

        if (!settingsName.endsWith(QStringLiteral(".ini"))) {
            storagePath += QStringLiteral(".ini");
        }
    }
    else {
        storagePath += QStringLiteral("config.ini");
    }

    return storagePath;
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

ApplicationSettings::ApplicationSettings(const QString & settingsName) :
    QSettings(defaultApplicationStoragePath(settingsName), QSettings::IniFormat)
{}

ApplicationSettings::ApplicationSettings(
    const Account & account, const QString & settingsName) :
    QSettings(
        accountApplicationStoragePath(account, settingsName),
        QSettings::IniFormat)
{}

ApplicationSettings::ApplicationSettings(
    const Account & account, const char * settingsName,
    const int settingsNameSize) :
    QSettings(
        accountApplicationStoragePath(
            account, QString::fromUtf8(settingsName, settingsNameSize)),
        QSettings::IniFormat)
{}

ApplicationSettings::~ApplicationSettings() {}

void ApplicationSettings::beginGroup(const QString & prefix)
{
    QSettings::beginGroup(prefix);
}

void ApplicationSettings::beginGroup(const char * prefix, const int size)
{
    QSettings::beginGroup(QString::fromUtf8(prefix, size));
}

int ApplicationSettings::beginReadArray(const QString & prefix)
{
    return QSettings::beginReadArray(prefix);
}

int ApplicationSettings::beginReadArray(const char * prefix, const int size)
{
    return QSettings::beginReadArray(QString::fromUtf8(prefix, size));
}

void ApplicationSettings::beginWriteArray(
    const QString & prefix, const int size)
{
    QSettings::beginWriteArray(prefix, size);
}

void ApplicationSettings::beginWriteArray(
    const char * prefix, const int arraySize, const int prefixSize)
{
    QSettings::beginWriteArray(
        QString::fromUtf8(prefix, prefixSize), arraySize);
}

bool ApplicationSettings::contains(const QString & key) const
{
    return QSettings::contains(key);
}

bool ApplicationSettings::contains(const char * key, const int size) const
{
    return QSettings::contains(QString::fromUtf8(key, size));
}

void ApplicationSettings::remove(const QString & key)
{
    QSettings::remove(key);
}

void ApplicationSettings::remove(const char * key, const int size)
{
    QSettings::remove(QString::fromUtf8(key, size));
}

void ApplicationSettings::setValue(const QString & key, const QVariant & value)
{
    QSettings::setValue(key, value);
}

void ApplicationSettings::setValue(
    const char * key, const QVariant & value, const int keySize)
{
    QSettings::setValue(QString::fromUtf8(key, keySize), value);
}

QVariant ApplicationSettings::value(
    const QString & key, const QVariant & defaultValue) const
{
    return QSettings::value(key, defaultValue);
}

QVariant ApplicationSettings::value(
    const char * key, const QVariant & defaultValue, const int keySize) const
{
    return QSettings::value(QString::fromUtf8(key, keySize), defaultValue);
}

QTextStream & ApplicationSettings::print(QTextStream & strm) const
{
    auto allStoredKeys = QSettings::allKeys();

    for (const auto & key: qAsConst(allStoredKeys)) {
        auto value = QSettings::value(key);
        strm << QStringLiteral("Key: ") << key << QStringLiteral("; Value: ")
             << value.toString() << QStringLiteral("\n;");
    }

    return strm;
}

} // namespace quentier
