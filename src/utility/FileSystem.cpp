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
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Compat.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/Size.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#endif // defined Q_OS_WIN

namespace quentier {

const QString relativePathFromAbsolutePath(
    const QString & absolutePath, const QString & relativePathRootFolder)
{
    QNDEBUG(
        "utility:filesystem", "relativePathFromAbsolutePath: " << absolutePath);

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
            "utility:filesystem",
            "Can't find folder " << relativePathRootFolder << " within path "
                                 << absolutePath);
        return {};
    }

    // NOTE: additional symbol for slash
    return absolutePath.mid(position + relativePathRootFolder.length() + 1);
}

bool removeFile(const QString & filePath)
{
    QNDEBUG("utility:filesystem", "removeFile: " << filePath);

    QFile file(filePath);
    file.close(); // NOTE: this line seems to be mandatory on Windows
    bool res = file.remove();
    if (res) {
        QNTRACE("utility:filesystem", "Successfully removed file " << filePath);
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
            "utility:filesystem",
            "Skipping the reported failure at removing the .lnk file");
        return true;
    }
#endif

    QNWARNING(
        "utility:filesystem",
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
