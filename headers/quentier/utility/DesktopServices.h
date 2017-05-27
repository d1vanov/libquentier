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

#ifndef LIB_QUENTIER_UTILITY_DESKTOP_SERVICES_H
#define LIB_QUENTIER_UTILITY_DESKTOP_SERVICES_H

#include <quentier/utility/Macros.h>
#include <quentier/utility/Linkage.h>
#include <quentier/types/Account.h>
#include <QString>
#include <QStyle>
#include <QFileDialog>
#include <QMessageBox>

/**
 * This macro defines the name of the environment variable which can be set to
 * override the default persistence storage path used by libquentier
 */
#define LIBQUENTIER_PERSISTENCE_STORAGE_PATH "LIBQUENTIER_PERSISTENCE_STORAGE_PATH"

QT_FORWARD_DECLARE_CLASS(QWidget)

namespace quentier {

// Convenience functions for some paths important for the application

/**
 * applicationPersistentStoragePath - returns the folder in which the application should store
 * its persistent data. By default chooses the appropriate system location but that can be overridden
 * by setting QUENTIER_PERSISTENCE_STORAGE_PATH environment variable. If the standard location
 * is overridden via the environment variable, the bool pointed to by pNonStandardLocation (if any) is set to false
 */
const QString QUENTIER_EXPORT applicationPersistentStoragePath(bool * pNonStandardLocation = Q_NULLPTR);

/**
 * accountPersistentStoragePath - returns the account-specific folder in which the application
 * should store the account-specific persistent data. The path returned by this function is a sub-path within
 * that returned by applicationPersistentStoragePath function.
 *
 * @param account   The account for which the path needs to be returned; if empty, the application persistent storage path is returned
 */
const QString QUENTIER_EXPORT accountPersistentStoragePath(const Account & account);

const QString QUENTIER_EXPORT applicationTemporaryStoragePath();

const QString QUENTIER_EXPORT homePath();

const QString QUENTIER_EXPORT documentsPath();

QUENTIER_EXPORT QStyle * applicationStyle();

const QString QUENTIER_EXPORT humanReadableSize(const quint64 bytes);

// Convenience functions for message boxes with proper modality on Mac OS X
int QUENTIER_EXPORT genericMessageBox(QWidget * parent, const QString & title,
                                      const QString & briefText, const QString & detailedText = QString(),
                                      const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);
int QUENTIER_EXPORT informationMessageBox(QWidget * parent, const QString & title,
                                          const QString & briefText, const QString & detailedText = QString(),
                                          const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);
int QUENTIER_EXPORT warningMessageBox(QWidget * parent, const QString & title,
                                      const QString & briefText, const QString & detailedText = QString(),
                                      const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);
int QUENTIER_EXPORT criticalMessageBox(QWidget * parent, const QString & title,
                                       const QString & briefText, const QString & detailedText = QString(),
                                       const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok);
int QUENTIER_EXPORT questionMessageBox(QWidget * parent, const QString & title,
                                       const QString & briefText, const QString & detailedText = QString(),
                                       const QMessageBox::StandardButtons standardButtons = QMessageBox::Ok | QMessageBox::Cancel);

// Convenience function for critical message box due to internal error, has built-in title
// ("Internal error") and brief text so the caller only needs to provide the detailed text
void QUENTIER_EXPORT internalErrorMessageBox(QWidget * parent, QString detailedText = QString());

// Convenience function for file dialogues with proper modality on Mac OS X
const QString QUENTIER_EXPORT getExistingFolderDialog(QWidget * parent, const QString & title,
                                                      const QString & initialFolder,
                                                      QFileDialog::Options options = QFileDialog::ShowDirsOnly);

// Convenience function to convert the absolute path to the relative one with respect to the given folder name
const QString QUENTIER_EXPORT relativePathFromAbsolutePath(const QString & absolutePath,
                                                           const QString & relativePathRootFolder);

// Get the system user name of the currently logged in user
const QString QUENTIER_EXPORT getCurrentUserName();

// Get the full name of the currently logged in user
const QString QUENTIER_EXPORT getCurrentUserFullName();

// Convenience function to send URL opening request
void QUENTIER_EXPORT openUrl(const QUrl url);

// Convenience function to remove a file automatically printing the possible error to warning log entry and
// working around some platform specific quirks
bool QUENTIER_EXPORT removeFile(const QString & filePath);

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_DESKTOP_SERVICES_H
