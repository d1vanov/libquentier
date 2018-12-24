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

#ifndef LIB_QUENTIER_UTILITY_UTILITY_H
#define LIB_QUENTIER_UTILITY_UTILITY_H

#include <quentier/utility/Linkage.h>
#include <quentier/types/ErrorString.h>
#include <QtGlobal>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QStyle>
#include <QFileDialog>
#include <QFlags>

#include <cstdint>

/**
 * Convenience macro to translate the number of seconds into milliseconds
 */
#define SEC_TO_MSEC(sec) (sec * 1000)

namespace quentier {

/**
 * initializeLibquentier - the function that needs to be called during the initial stages
 * of application startup for the library to work properly
 */
void QUENTIER_EXPORT initializeLibquentier();

/**
 * checkGuid - checks the valitidy of the input string (QString or other string type)
 *
 * @param guid      The guid to be checked for validity
 * @return          True if the passed in guid is valid, false otherwise
 */
template <class T>
bool checkGuid(const T & guid)
{
    qint32 guidSize = static_cast<qint32>(guid.size());

    if (guidSize < qevercloud::EDAM_GUID_LEN_MIN) {
        return false;
    }

    if (guidSize > qevercloud::EDAM_GUID_LEN_MAX) {
        return false;
    }

    return true;
}

/**
 * checkUpdateSequenceNumber - checks the passed in update sequence number for validity
 *
 * @param updateSequenceNumber  The update sequence number to be checked for validity
 * @return                      True if the passed in update sequence number is valid, false otherwise
 */
bool QUENTIER_EXPORT checkUpdateSequenceNumber(const int32_t updateSequenceNumber);

/**
 * @brief The DateTimePrint class simply wraps the enum containing datetime printing options
 */
class QUENTIER_EXPORT DateTimePrint
{
public:
    /**
     * Available printing options for datetime
     */
    enum Option {
        /**
         * Include the numeric representation of the timestamp into the printed string
         */
        IncludeNumericTimestamp = 1 << 1,
        /**
         * Include milliseconds into the printed string
         */
        IncludeMilliseconds = 1 << 2,
        /**
         * Include timezone into the printed string
         * WARNING: currently this option has no effect on Windows platform, the timezone is not included anyway.
         */
        IncludeTimezone = 1 << 3
    };
    Q_DECLARE_FLAGS(Options, Option)
};

Q_DECLARE_OPERATORS_FOR_FLAGS(DateTimePrint::Options)

/**
 * printableDateTimeFromTimestamp - converts the passed in timestamp into a human readable datetime string
 * @param timestamp             The timestamp to be translated to a human readable string
 * @param options               Datetime printing options
 * @param customFormat          The custom format string; internally, if not null, if would be passed to strftime function
 *                              declared in <ctime> header of the C++ standard library; but beware that the length
 *                              of the printed string is limited by 100 characters regardless of the format string
 *
 * @return      Human readable datetime string corresponding to the passed in timestamp
 */
const QString QUENTIER_EXPORT printableDateTimeFromTimestamp(const qint64 timestamp,
                                                             DateTimePrint::Options options =
                                                             DateTimePrint::Options(DateTimePrint::IncludeNumericTimestamp |
                                                                                    DateTimePrint::IncludeMilliseconds |
                                                                                    DateTimePrint::IncludeTimezone),
                                                             const char * customFormat = Q_NULLPTR);

/**
 * applicationStyle - provides the current style of the application
 * @return      The pointer to the current style of the application or null pointer
 *              if the application doesn't have the style
 */
QUENTIER_EXPORT QStyle * applicationStyle();

/**
 * humanReadableSize - provides the human readable string denoting the size
 * of some piece of data
 *
 * @param bytes     The number of bytes for which the human readable size string is required
 * @return          The human readable string corresponding to the passed in number of bytes
 */
const QString QUENTIER_EXPORT humanReadableSize(const quint64 bytes);

/**
 * getExistingFolderDialog - shows the file dialog with properly specified window modality
 */
const QString QUENTIER_EXPORT getExistingFolderDialog(QWidget * parent, const QString & title,
                                                      const QString & initialFolder,
                                                      QFileDialog::Options options = QFileDialog::ShowDirsOnly);

/**
 * relativePathFromAbsolutePath - converts the absolute path to a relative one
 * with respect to the given folder name
 *
 * @param absolutePath                  The absolute path for which the corresponding relative path is needed
 * @param relativePathRootFolderPath    The path to the root directory with respect to which the relative path is needed
 * @return                              The relative path corresponding to the input absolute path and root dir path
 */
const QString QUENTIER_EXPORT relativePathFromAbsolutePath(const QString & absolutePath,
                                                           const QString & relativePathRootFolderPath);

/**
 * getCurrentUserName
 * @return  The system user name of the currently logged in user
 */
const QString QUENTIER_EXPORT getCurrentUserName();

/**
 * getCurrentUserFullName
 * @return  The full name of the currently logged in user
 */
const QString QUENTIER_EXPORT getCurrentUserFullName();

/**
 * openUrl - sends the request to open a url
 */
void QUENTIER_EXPORT openUrl(const QUrl & url);

/**
 * removeFile - removes the file specified by path; in case of file removal error prints warning into the log;
 * works around some platform specific quirks
 *
 * @param filePath      The path to the file which needs to be removed
 * @return              True if the file was removed successfully, false otherwise
 */
bool QUENTIER_EXPORT removeFile(const QString & filePath);

/**
 * removeDir - removes the directory specified by path recursively, with all its contents; in case of dir removal error
 * prints warning into the log; workarounds the lack of QDir::removeRecursively in Qt 4.
 *
 * @param dirPath       The path to the directory which needs to be removed
 * @return              True if the directory was removed successfully, false otherwise
 */
bool QUENTIER_EXPORT removeDir(const QString & dirPath);

/**
 * readFileContents - reads the entire contents of a file into QByteArray which
 * is then returned from the function. This function workarounds some quirks of
 * QFile::readAll and hence can be used instead of it.
 *
 * @param filePath          The path to the file which contents are to be read
 * @param errorDescription  The textual description of the error in case of I/O
 *                          error, empty otherwise
 * @return                  QByteArray with file's contents read into memory or
 *                          empty QByteArray in case of I/O error
 */
QByteArray QUENTIER_EXPORT readFileContents(const QString & filePath, ErrorString & errorDescription);

/**
 * renameFile - renames file with absolute path "from" to file with absolute
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
bool QUENTIER_EXPORT renameFile(const QString & from, const QString & to, ErrorString & errorDescription);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_UTILITY_H
