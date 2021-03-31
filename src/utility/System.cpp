/*
 * Copyright 2020 Dmitry Ivanov
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
#include <quentier/utility/SuppressWarnings.h>
#include <quentier/utility/System.h>

#include <QDesktopServices>
#include <QStandardPaths>

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <limits>
#include <memory>
#include <string>

SAVE_WARNINGS

MSVC_SUPPRESS_WARNING(4005)

#include <Lmcons.h>
#include <QtGui/qwindowdefs_win.h>
#include <qwindowdefs.h>
#include <windows.h>

#define SECURITY_WIN32
#include <security.h>

RESTORE_WARNINGS

#else // defined Q_OS_WIN

#include <cstdio>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#endif // defined Q_OS_WIN

namespace quentier {

QString getCurrentUserName()
{
    QNDEBUG("utility:system", "getCurrentUserName");

    QString userName;

#if defined(Q_OS_WIN)
    TCHAR acUserName[UNLEN + 1];
    DWORD nUserName = sizeof(acUserName);
    if (GetUserName(acUserName, &nUserName)) {
        userName = QString::fromWCharArray(acUserName);
    }
#else
    uid_t uid = geteuid();
    struct passwd * pw = getpwuid(uid);
    if (pw) {
        userName = QString::fromLocal8Bit(pw->pw_name);
    }
#endif

    if (userName.isEmpty()) {
        QNTRACE(
            "utility:system",
            "Native platform API failed to provide "
                << "the username, trying environment variables fallback");

        userName = QString::fromLocal8Bit(qgetenv("USER"));
        if (userName.isEmpty()) {
            userName = QString::fromLocal8Bit(qgetenv("USERNAME"));
        }
    }

    QNTRACE("utility:system", "Username = " << userName);
    return userName;
}

QString getCurrentUserFullName()
{
    QNDEBUG("utility:system", "getCurrentUserFullName");

    QString userFullName;

#if defined(Q_OS_WIN)
    TCHAR acUserName[UNLEN + 1];
    DWORD nUserName = sizeof(acUserName);
    bool res = GetUserNameEx(NameDisplay, acUserName, &nUserName);
    if (res) {
        userFullName = QString::fromWCharArray(acUserName);
    }

    if (userFullName.isEmpty()) {
        /**
         * GetUserNameEx with NameDisplay format doesn't work when the computer
         * is offline. It's serious. Take a look here:
         * http://stackoverflow.com/a/2997257
         * I've never had any serious business with WinAPI but I nearly killed
         * myself with a facepalm when I figured this thing out. God help
         * Microsoft - nothing else will.
         *
         * Falling back to the login name
         */
        userFullName = getCurrentUserName();
    }
#else
    uid_t uid = geteuid();
    struct passwd * pw = getpwuid(uid);
    if (Q_LIKELY(pw)) {
        struct passwd * pwf = getpwnam(pw->pw_name);
        if (Q_LIKELY(pwf)) {
            userFullName = QString::fromLocal8Bit(pwf->pw_gecos);
        }
    }

    /**
     * NOTE: some Unix systems put more than full user name into this field but
     * also something about the location etc. The convention is to use comma to
     * split the values of different kind and the user's full name is the first
     * one
     */
    int commaIndex = userFullName.indexOf(QChar::fromLatin1(','));
    if (commaIndex > 0) { // NOTE: not >= but >
        userFullName.truncate(commaIndex);
    }
#endif

    return userFullName;
}

void openUrl(const QUrl & url)
{
    QNDEBUG("utility:system", "openUrl: " << url);
    QDesktopServices::openUrl(url);
}

} // namespace quentier
