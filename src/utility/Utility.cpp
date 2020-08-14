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

#include "../note_editor/NoteEditorLocalStorageBroker.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/utility/Utility.h>

#include <QApplication>
#include <QDir>
#include <QFileInfoList>
#include <QStyleFactory>
#include <QUrl>

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <limits>
#include <memory>
#include <string>

#include <Lmcons.h>
#include <QtGui/qwindowdefs_win.h>
#include <qwindowdefs.h>
#include <windows.h>

#define SECURITY_WIN32
#include <security.h>

#else // defined Q_OS_WIN

#include <cstdio>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#if QT_VERSION >= 0x050000
#include <QStandardPaths>
#endif

#endif // defined Q_OS_WIN

#include <QDesktopServices>
#include <QFile>

#include <qt5qevercloud/QEverCloud.h>

#include <ctime>
#include <limits>
#include <time.h>

#include <cstring>
#include <fstream>
#include <string>

namespace quentier {

void initializeLibquentier()
{
    qevercloud::initializeQEverCloud();

    registerMetatypes();

    // Ensure the instance is created now and not later
    Q_UNUSED(NoteEditorLocalStorageBroker::instance())

#ifdef QUENTIER_USE_QT_WEB_ENGINE
    // Attempt to workaround https://bugreports.qt.io/browse/QTBUG-40765
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
#endif
}

bool checkUpdateSequenceNumber(const int32_t updateSequenceNumber)
{
    return !(
        (updateSequenceNumber < 0) ||
        (updateSequenceNumber == std::numeric_limits<int32_t>::min()) ||
        (updateSequenceNumber == std::numeric_limits<int32_t>::max()));
}

const QString printableDateTimeFromTimestamp(
    const qint64 timestamp, const DateTimePrint::Options options,
    const char * customFormat)
{
    if (Q_UNLIKELY(timestamp < 0)) {
        return QString::number(timestamp);
    }

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
    Q_UNUSED(localTm)
    std::tm * tm = nullptr;

#ifdef _MSC_VER
    // MSVC's localtime is thread-safe since MSVC 2005
    tm = std::localtime(&t);
#else // ifdef _MSC_VER
#ifdef __MINGW32__
    // MinGW lacks localtime_r but uses MS's localtime instead which is told to
    // be thread-safe but it's still not re-entrant.
    // So, can at best hope it won't cause problems too often
    tm = localtime(&t);
#else // POSIX
    tm = &localTm;
    Q_UNUSED(localtime_r(&t, tm))
#endif
#endif // ifdef _MSC_VER

    if (Q_UNLIKELY(!tm)) {
        return QString::number(timestamp);
    }

    const size_t maxBufSize = 100;
    char buffer[maxBufSize];
    const char * format = "%Y-%m-%d %H:%M:%S";
    size_t size = strftime(
        buffer, maxBufSize, (customFormat ? customFormat : format), tm);

    result += QString::fromLocal8Bit(buffer, static_cast<int>(size));

    if (options & DateTimePrint::IncludeMilliseconds) {
        qint64 msecPart = timestamp - t * 1000;
        result += QStringLiteral(".");
        result += QString::fromUtf8("%1").arg(
            msecPart, 3, 10, QChar::fromLatin1('0'));
    }

#if !defined(_MSC_VER) && !defined(__MINGW32__)
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
    return QApplication::style();
}

const QString humanReadableSize(const quint64 bytes)
{
    QStringList list;
    list << QStringLiteral("Kb") << QStringLiteral("Mb") << QStringLiteral("Gb")
         << QStringLiteral("Tb");

    QStringListIterator it(list);
    QString unit = QStringLiteral("bytes");

    double num = static_cast<double>(bytes);
    while (num >= 1024.0 && it.hasNext()) {
        unit = it.next();
        num /= 1024.0;
    }

    QString result = QString::number(num, 'f', 2);
    result += QStringLiteral(" ");
    result += unit;

    return result;
}

const QString getExistingFolderDialog(
    QWidget * parent, const QString & title, const QString & initialFolder,
    QFileDialog::Options options)
{
    auto pFileDialog = std::make_unique<QFileDialog>(parent);
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

const QString relativePathFromAbsolutePath(
    const QString & absolutePath, const QString & relativePathRootFolder)
{
    QNDEBUG("utility", "relativePathFromAbsolutePath: " << absolutePath);

    int position = absolutePath.indexOf(
        relativePathRootFolder, 0,
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        Qt::CaseInsensitive
#else
        Qt::CaseSensitive
#endif
    );
    if (position < 0) {
        QNINFO(
            "utility",
            "Can't find folder " << relativePathRootFolder << " within path "
                                 << absolutePath);
        return {};
    }

    // NOTE: additional symbol for slash
    return absolutePath.mid(position + relativePathRootFolder.length() + 1);
}

const QString getCurrentUserName()
{
    QNDEBUG("utility", "getCurrentUserName");

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
            "utility",
            "Native platform API failed to provide "
                << "the username, trying environment variables fallback");

        userName = QString::fromLocal8Bit(qgetenv("USER"));
        if (userName.isEmpty()) {
            userName = QString::fromLocal8Bit(qgetenv("USERNAME"));
        }
    }

    QNTRACE("utility", "Username = " << userName);
    return userName;
}

const QString getCurrentUserFullName()
{
    QNDEBUG("utility", "getCurrentUserFullName");

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
    QNDEBUG("utility", "openUrl: " << url);
    QDesktopServices::openUrl(url);
}

bool removeFile(const QString & filePath)
{
    QNDEBUG("utility", "removeFile: " << filePath);

    QFile file(filePath);
    file.close(); // NOTE: this line seems to be mandatory on Windows
    bool res = file.remove();
    if (res) {
        QNTRACE("utility", "Successfully removed file " << filePath);
        return true;
    }

#ifdef Q_OS_WIN
    if (filePath.endsWith(QStringLiteral(".lnk"))) {
        /**
         * NOTE: there appears to be a bug in Qt for Windows, QFile::remove
         * returns false for any *.lnk files even though the files are actually
         * getting removed
         */
        QNTRACE(
            "utility",
            "Skipping the reported failure at removing "
                << "the .lnk file");
        return true;
    }
#endif

    QNWARNING(
        "utility",
        "Cannot remove file " << filePath << ": " << file.errorString()
                              << ", error code " << file.error());
    return false;
}

bool removeDirImpl(const QString & dirPath)
{
    bool result = true;
    QDir dir(dirPath);

    if (dir.exists()) {
        QFileInfoList dirContents = dir.entryInfoList(
            QDir::NoDotAndDotDot | QDir::System | QDir::Hidden | QDir::AllDirs |
                QDir::Files,
            QDir::DirsFirst);

        for (const auto & info: qAsConst(dirContents)) {
            if (info.isDir()) {
                result = removeDirImpl(info.absoluteFilePath());
            }
            else {
                result = removeFile(info.absoluteFilePath());
            }

            if (!result) {
                return result;
            }
        }

        result = dir.rmpath(dirPath);
        if (!result) {
            // NOTE: QDir::rmpath seems to occasionally return failure on
            // Windows while in reality the method works
            if (!dir.exists()) {
                result = true;
            }
            else {
                // Ok, it truly didn't work, let's try a hack then
                QFile dirFile(dirPath);
                QFile::Permissions originalPermissions = dirFile.permissions();
                dirFile.setPermissions(QFile::WriteOther);
                result = dir.rmpath(dirPath);
                if (!result && !dir.exists()) {
                    // If still failure, revert the original permissions
                    dirFile.setPermissions(originalPermissions);
                }
                else if (!dir.exists()) {
                    result = true;
                }
            }
        }
    }

    return result;
}

bool removeDir(const QString & dirPath)
{
    return removeDirImpl(dirPath);
}

QByteArray readFileContents(
    const QString & filePath, ErrorString & errorDescription)
{
    QByteArray result;
    errorDescription.clear();

    std::ifstream istrm;
    istrm.open(
        QDir::toNativeSeparators(filePath).toStdString(), std::ifstream::in);
    if (!istrm.good()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "readFileContents",
            "Failed to read file contents, could not open the file "
            "for reading"));

        errorDescription.details() = QString::fromLocal8Bit(strerror(errno));
        return result;
    }

    istrm.seekg(0, std::ios::end);
    std::streamsize length = istrm.tellg();
    if (length > std::numeric_limits<int>::max()) {
        errorDescription.setBase(QT_TRANSLATE_NOOP(
            "readFileContents",
            "Failed to read file contents, file is too large"));

        errorDescription.details() =
            humanReadableSize(static_cast<quint64>(length));
        return result;
    }

    istrm.seekg(0, std::ios::beg);

    result.resize(static_cast<int>(length));
    istrm.read(result.data(), length);

    return result;
}

bool renameFile(
    const QString & from, const QString & to, ErrorString & errorDescription)
{
#ifdef Q_OS_WIN

    std::wstring fromW = QDir::toNativeSeparators(from).toStdWString();
    std::wstring toW = QDir::toNativeSeparators(to).toStdWString();
    int res = MoveFileExW(
        fromW.c_str(), toW.c_str(),
        MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING |
            MOVEFILE_WRITE_THROUGH);

    if (res == 0) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("renameFile", "failed to rename file"));

        LPTSTR errorText = NULL;

        Q_UNUSED(FormatMessage(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPTSTR)&errorText, 0, NULL))

        if (errorText != NULL) {
            errorDescription.details() += QString::fromWCharArray(errorText);
            LocalFree(errorText);
            errorText = 0;
        }

        errorDescription.details() += QStringLiteral("; from = ");
        errorDescription.details() += from;
        errorDescription.details() += QStringLiteral(", to = ");
        errorDescription.details() += to;
        return false;
    }

    return true;

#else  // Q_OS_WIN

    int res = rename(from.toUtf8().constData(), to.toUtf8().constData());
    if (res != 0) {
        errorDescription.setBase(
            QT_TRANSLATE_NOOP("renameFile", "failed to rename file"));

        errorDescription.details() += QString::fromUtf8(strerror(errno));
        errorDescription.details() += QStringLiteral("; from = ");
        errorDescription.details() += from;
        errorDescription.details() += QStringLiteral(", to = ");
        errorDescription.details() += to;
        return false;
    }

    return true;
#endif // Q_OS_WIN
}

} // namespace quentier
