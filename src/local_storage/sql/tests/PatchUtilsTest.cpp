/*
 * Copyright 2021 Dmitry Ivanov
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

#include "Utils.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"
#include "../Fwd.h"
#include "../TablesInitializer.h"
#include "../patches/PatchUtils.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>

#include <gtest/gtest.h>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <QTemporaryDir>

namespace quentier::local_storage::sql::tests {

namespace {

const QString gTestDbConnectionName =
    QStringLiteral("libquentier_local_storage_sql_patch_utils_test_db");

const QString gTestDatabaseFileName = QStringLiteral("qn.storage.sqlite");

const QString gTestAccountName = QStringLiteral("testAccountName");

} // namespace

TEST(PatchUtilsTest, BackupLocalStorageTest)
{
    Account account{gTestAccountName, Account::Type::Local};

    QTemporaryDir testLocalStorageDir{
        QDir::tempPath() + QStringLiteral("/") + gTestDbConnectionName};

    Q_ASSERT(testLocalStorageDir.isValid());

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), gTestAccountName,
        gTestAccountName, testLocalStorageDir.filePath(gTestDatabaseFileName),
        QStringLiteral("QSQLITE"));

    const QString localStorageDirPath = testLocalStorageDir.path() +
        QStringLiteral("/LocalAccounts/") + gTestAccountName;

    utils::prepareLocalStorage(localStorageDirPath, *connectionPool);

    const auto writerThread = std::make_shared<QThread>();
    writerThread->start();

    qputenv(
        LIBQUENTIER_PERSISTENCE_STORAGE_PATH,
        testLocalStorageDir.path().toLocal8Bit());

    const QString backupDirPrefix = QStringLiteral("backup_dir");
    QDir backupDir{localStorageDirPath + QStringLiteral("/") + backupDirPrefix};
    QPromise<void> promise;

    ErrorString errorDescription;

    bool res = sql::utils::backupLocalStorageDatabaseFiles(
        localStorageDirPath, backupDir.absolutePath(), promise,
        errorDescription);

    EXPECT_TRUE(res);

    bool foundLocalStorageBackup = false;
    QDir dir{localStorageDirPath};
    auto dirInfos = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    EXPECT_FALSE(dirInfos.isEmpty());
    for (const auto & dirInfo: qAsConst(dirInfos)) {
        if (dirInfo.fileName().startsWith(backupDirPrefix)) {
            QDir backupDir{dir.absoluteFilePath(dirInfo.fileName())};

            const auto files =
                backupDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            EXPECT_FALSE(files.isEmpty());

            for (const auto & file: qAsConst(files)) {
                EXPECT_TRUE(file.fileName().startsWith(gTestDatabaseFileName));
            }

            foundLocalStorageBackup = true;
            break;
        }
    }

    EXPECT_TRUE(foundLocalStorageBackup);

    // Now ensure the ability to restore the backup

    EXPECT_TRUE(removeFile(
        localStorageDirPath + QStringLiteral("/") + gTestDatabaseFileName));

    res = sql::utils::restoreLocalStorageDatabaseFilesFromBackup(
        localStorageDirPath, backupDir.absolutePath(), promise,
        errorDescription);

    EXPECT_TRUE(res);

    auto fileInfos = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    EXPECT_FALSE(fileInfos.isEmpty());
    bool foundRestoredFromBackupLocalStorage = false;

    for (const auto & fileInfo: qAsConst(fileInfos)) {
        if (fileInfo.fileName() == gTestDatabaseFileName) {
            foundRestoredFromBackupLocalStorage = true;
            break;
        }
    }

    dirInfos = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto & dirInfo: qAsConst(dirInfos)) {
        if (dirInfo.fileName().startsWith(backupDirPrefix)) {
            QDir backupDir{dir.absoluteFilePath(dirInfo.fileName())};

            const auto files =
                backupDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            EXPECT_FALSE(files.isEmpty());

            for (const auto & file: qAsConst(files)) {
                EXPECT_TRUE(file.fileName().startsWith(gTestDatabaseFileName));
            }

            foundLocalStorageBackup = true;
            break;
        }
    }

    EXPECT_TRUE(foundRestoredFromBackupLocalStorage);
    EXPECT_TRUE(foundLocalStorageBackup);

    // Now ensure the backup is deleted properly

    res = sql::utils::removeLocalStorageDatabaseFilesBackup(
        backupDir.absolutePath(), errorDescription);

    EXPECT_TRUE(res);

    const auto entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    foundLocalStorageBackup = false;
    foundRestoredFromBackupLocalStorage = false;

    for (const auto & entryInfo: qAsConst(entries)) {
        if (entryInfo.fileName().startsWith(backupDirPrefix)) {
            foundLocalStorageBackup = true;
            break;
        }

        if (entryInfo.fileName() == gTestDatabaseFileName) {
            foundRestoredFromBackupLocalStorage = true;
        }
    }

    EXPECT_TRUE(foundRestoredFromBackupLocalStorage);
    EXPECT_FALSE(foundLocalStorageBackup);

    writerThread->quit();
    writerThread->wait();
}

} // namespace quentier::local_storage::sql::tests
