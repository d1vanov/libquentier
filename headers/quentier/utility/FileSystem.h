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

#ifndef LIB_QUENTIER_UTILITY_FILE_SYSTEM_H
#define LIB_QUENTIER_UTILITY_FILE_SYSTEM_H

#include <quentier/utility/Linkage.h>

#include <QString>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ErrorString)

/**
 * relativePathFromAbsolutePath converts the absolute path to a relative one
 * with respect to the given folder name
 *
 * @param absolutePath                  The absolute path for which
 *                                      the corresponding relative path is
 *                                      needed
 * @param relativePathRootFolderPath    The path to the root directory with
 *                                      respect to which the relative path is
 *                                      needed
 * @return                              The relative path corresponding to
 *                                      the input absolute path and root dir
 *                                      path
 */
const QString QUENTIER_EXPORT relativePathFromAbsolutePath(
    const QString & absolutePath, const QString & relativePathRootFolderPath);

/**
 * removeFile removes the file specified by path; in case of file removal error
 * prints warning into the log; works around some platform specific quirks
 *
 * @param filePath      The path to the file which needs to be removed
 * @return              True if the file was removed successfully, false
 *                      otherwise
 */
bool QUENTIER_EXPORT removeFile(const QString & filePath);

/**
 * removeDir removes the directory specified by path recursively, with all its
 * contents; in case of dir removal error prints warning into the log;
 * workarounds the lack of QDir::removeRecursively in Qt 4.
 *
 * @param dirPath       The path to the directory which needs to be removed
 * @return              True if the directory was removed successfully, false
 *                      otherwise
 */
bool QUENTIER_EXPORT removeDir(const QString & dirPath);

/**
 * readFileContents reads the entire contents of a file into QByteArray which
 * is then returned from the function. This function workarounds some quirks of
 * QFile::readAll and hence can be used instead of it.
 *
 * @param filePath          The path to the file which contents are to be read
 * @param errorDescription  The textual description of the error in case of I/O
 *                          error, empty otherwise
 * @return                  QByteArray with file's contents read into memory or
 *                          empty QByteArray in case of I/O error
 */
QByteArray QUENTIER_EXPORT
readFileContents(const QString & filePath, ErrorString & errorDescription);

/**
 * renameFile renames file with absolute path "from" to file with absolute
 * path "to". This function handles the case when file "to" already exists. On
 * Linux and Mac this function simply calls "rename" from the standard C library
 * while on Windows it calls MoveFileExW with flags MOVEFILE_COPY_ALLOWED,
 * MOVEFILE_REPLACE_EXISTING and MOVEFILE_WRITE_THROUGH
 *
 * @param from              The absolute file path of the file to be renamed
 * @param to                The absolute target file path
 * @param errorDescription  The textual description of the error in case of
 *                          inability to rename the file
 * @return                  True if file was successfully renamed, false
 *                          otherwise
 */
bool QUENTIER_EXPORT renameFile(
    const QString & from, const QString & to, ErrorString & errorDescription);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_FILE_SYSTEM_H
