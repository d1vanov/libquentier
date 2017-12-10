/*
 * Copyright 2016 Dmitry Ivanov
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

#include <quentier/utility/Utility.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/RegisterMetatypes.h>
#include <QStyleFactory>
#include <QApplication>
#include <QScopedPointer>
#include <QUrl>

#ifdef Q_OS_WIN

#define NOMINMAX
#include <limits>

#include <qwindowdefs.h>
#include <QtGui/qwindowdefs_win.h>
#include <windows.h>
#include <Lmcons.h>

#define SECURITY_WIN32
#include <security.h>

#else

#if defined Q_OS_MAC
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#include <QMacStyle>
#endif // QT_VERSION
#else // defined Q_OS_MAC
#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#include <QPlastiqueStyle>
#endif
#endif // defined Q_OS_MAX

#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

#if QT_VERSION >= 0x050000
#include <QStandardPaths>
#endif

#endif // defined Q_OS_WIN

#include <QDesktopServices>
#include <QFile>

#include <limits>
#include <ctime>
#include <time.h>

namespace quentier {

void initializeLibquentier()
{
    registerMetatypes();
}

bool checkUpdateSequenceNumber(const int32_t updateSequenceNumber)
{
    return !( (updateSequenceNumber < 0) ||
              (updateSequenceNumber == std::numeric_limits<int32_t>::min()) ||
              (updateSequenceNumber == std::numeric_limits<int32_t>::max()) );
}

const QString printableDateTimeFromTimestamp(const qint64 timestamp, const DateTimePrint::Options options,
                                             const char * customFormat)
{
    QString result;

    if (options & DateTimePrint::IncludeNumericTimestamp) {
        result += QString::number(timestamp);
        result += QStringLiteral(" (");
    }

    // NOTE: deliberately avoiding the use of QDateTime here as this function
    // would be potentially called from several threads and QDateTime::toString
    // has the potential to randomly crash in such environments, see e.g.
    // https://bugreports.qt.io/browse/QTBUG-49473

    std::time_t t(timestamp / 1000);
    std::tm localTm;
    std::tm * tm = Q_NULLPTR;

#ifdef _MSC_VER
#if _MSC_VER >= 1400
    // MSVC's localtime is thread-safe since MSVC 2005
    tm = std::localtime(&t);
    Q_UNUSED(localTm);
#else
#error "Too old MSVC version to reliably build libquentier
#endif
#else // POSIX
    tm = &localTm;
    Q_UNUSED(localtime_r(&t, tm))
#endif

    const size_t maxBufSize = 100;
    char buffer[maxBufSize];
    const char * format = "%Y-%m-%d %H:%M:%S";
    size_t size = strftime(buffer, maxBufSize, (customFormat ? customFormat : format) , tm);

    result += QString::fromLocal8Bit(buffer, static_cast<int>(size));

    if (options & DateTimePrint::IncludeMilliseconds) {
        qint64 msecPart = timestamp - t * 1000;
        result += QStringLiteral(".");
        result += QString::fromUtf8("%1").arg(msecPart, 3, 10, QChar::fromLatin1('0'));
    }

#ifndef _MSC_VER
    if (options & DateTimePrint::IncludeTimezone) {
        const char * timezone = tm->tm_zone;
        if (timezone) {
            result += QStringLiteral(" ");
            result += QString::fromLocal8Bit(timezone);
        }
    }
#endif

    if (options & DateTimePrint::IncludeNumericTimestamp) {
        result += QStringLiteral(")");
    }

    return result;
}

QStyle * applicationStyle()
{
    QStyle * appStyle = QApplication::style();
    if (appStyle) {
        return appStyle;
    }

    QNINFO("Application style is empty, will try to deduce some default style");

#ifdef Q_OS_WIN
    // FIXME: figure out why QWindowsStyle doesn't compile
    return Q_NULLPTR;
#else

#if defined(Q_OS_MAC) && QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
    return new QMacStyle;
#else

    const QStringList styleNames = QStyleFactory::keys();
#if !defined(Q_OS_MAC) && (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
    if (styleNames.isEmpty()) {
        QNINFO(QStringLiteral("No valid styles were found in QStyleFactory! Fallback to the last resort of plastique style"));
        return new QPlastiqueStyle;
    }

    const QString & firstStyle = styleNames.first();
    return QStyleFactory::create(firstStyle);
#else
    return Q_NULLPTR;
#endif // !defined(Q_OS_MAC) && (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))

#endif // defined(Q_OS_MAC) && QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
#endif // Q_OS_WIN
}

const QString humanReadableSize(const quint64 bytes)
{
    QStringList list;
    list << QStringLiteral("Kb") << QStringLiteral("Mb") << QStringLiteral("Gb") << QStringLiteral("Tb");

    QStringListIterator it(list);
    QString unit = QStringLiteral("bytes");

    double num = static_cast<double>(bytes);
    while(num >= 1024.0 && it.hasNext()) {
        unit = it.next();
        num /= 1024.0;
    }

    QString result = QString::number(num, 'f', 2);
    result += QStringLiteral(" ");
    result += unit;

    return result;
}

const QString getExistingFolderDialog(QWidget * parent, const QString & title,
                                      const QString & initialFolder,
                                      QFileDialog::Options options)
{
    QScopedPointer<QFileDialog> pFileDialog(new QFileDialog(parent));
    if (parent) {
        pFileDialog->setWindowModality(Qt::WindowModal);
    }

    pFileDialog->setWindowTitle(title);
    pFileDialog->setDirectory(initialFolder);
    pFileDialog->setOptions(options);
    int res = pFileDialog->exec();
    if (res == QDialog::Accepted) {
        return pFileDialog->directory().absolutePath();
    }
    else {
        return QString();
    }
}

const QString relativePathFromAbsolutePath(const QString & absolutePath, const QString & relativePathRootFolder)
{
    QNDEBUG(QStringLiteral("relativePathFromAbsolutePath: ") << absolutePath);

    int position = absolutePath.indexOf(relativePathRootFolder, 0,
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
                                        Qt::CaseInsensitive
#else
                                        Qt::CaseSensitive
#endif
                                        );
    if (position < 0) {
        QNINFO(QStringLiteral("Can't find folder ") << relativePathRootFolder << QStringLiteral(" within path ") << absolutePath);
        return QString();
    }

    return absolutePath.mid(position + relativePathRootFolder.length() + 1);   // NOTE: additional symbol for slash
}

const QString getCurrentUserName()
{
    QNDEBUG(QStringLiteral("getCurrentUserName"));

    QString userName;

#if defined(Q_OS_WIN)
    TCHAR acUserName[UNLEN+1];
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

    if (userName.isEmpty())
    {
        QNTRACE(QStringLiteral("Native platform API failed to provide the username, trying environment variables fallback"));

        userName = QString::fromLocal8Bit(qgetenv("USER"));
        if (userName.isEmpty()) {
            userName = QString::fromLocal8Bit(qgetenv("USERNAME"));
        }
    }

    QNTRACE(QStringLiteral("Username = ") << userName);
    return userName;
}

const QString getCurrentUserFullName()
{
    QNDEBUG(QStringLiteral("getCurrentUserFullName"));

    QString userFullName;

#if defined(Q_OS_WIN)
    TCHAR acUserName[UNLEN+1];
    DWORD nUserName = sizeof(acUserName);
    bool res = GetUserNameEx(NameDisplay, acUserName, &nUserName);
    if (res) {
        userFullName = QString::fromWCharArray(acUserName);
    }

    if (userFullName.isEmpty())
    {
        // GetUserNameEx with NameDisplay format doesn't work when the computer is offline.
        // It's serious. Take a look here: http://stackoverflow.com/a/2997257
        // I've never had any serious business with WinAPI but I nearly killed myself
        // with a facepalm when I figured this thing out. God help Microsoft - nothing else will.
        //
        // Falling back to the login name
        userFullName = getCurrentUserName();
     }
#else
    uid_t uid = geteuid();
    struct passwd * pw = getpwuid(uid);
    if (Q_LIKELY(pw))
    {
        struct passwd * pwf = getpwnam(pw->pw_name);
        if (Q_LIKELY(pwf)) {
            userFullName = QString::fromLocal8Bit(pwf->pw_gecos);
        }
    }

    // NOTE: some Unix systems put more than full user name into this field but also something about the location etc.
    // The convention is to use comma to split the values of different kind and the user's full name is the first one
    int commaIndex = userFullName.indexOf(QChar::fromLatin1(','));
    if (commaIndex > 0) {   // NOTE: not >= but >
        userFullName.truncate(commaIndex);
    }
#endif

    return userFullName;
}

void openUrl(const QUrl & url)
{
    QNDEBUG(QStringLiteral("openUrl: ") << url);
    QDesktopServices::openUrl(url);
}

bool removeFile(const QString & filePath)
{
    QNDEBUG(QStringLiteral("removeFile: ") << filePath);

    QFile file(filePath);
    file.close();   // NOTE: this line seems to be mandatory on Windows
    bool res = file.remove();
    if (res) {
        QNTRACE(QStringLiteral("Successfully removed file ") << filePath);
        return true;
    }

#ifdef Q_OS_WIN
    if (filePath.endsWith(QStringLiteral(".lnk"))) {
        // NOTE: there appears to be a bug in Qt for Windows, QFile::remove returns false
        // for any *.lnk files even though the files are actually getting removed
        QNTRACE(QStringLiteral("Skipping the reported failure at removing the .lnk file"));
        return true;
    }
#endif

    QNWARNING(QStringLiteral("Cannot remove file ") << filePath << QStringLiteral(": ") << file.errorString()
              << QStringLiteral(", error code ") << file.error());
    return false;
}

} // namespace quentier
