/*
 * Copyright 2021-2025 Dmitry Ivanov
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
#include "../SqlDatabaseWrapper.h"
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

#include <utility>

namespace quentier::local_storage::sql::tests {

namespace {

const char * gTestDbConnectionName =
    "libquentier_local_storage_sql_patch_utils_test_db";

const char * gTestDatabaseFileName = "qn.storage.sqlite";

const char * gTestAccountName = "testAccountName";

} // namespace

TEST(PatchUtilsTest, BackupLocalStorageTest)
{
    Account account{QString::fromUtf8(gTestAccountName), Account::Type::Local};

    QTemporaryDir testLocalStorageDir{
        QDir::tempPath() + QString::fromUtf8("/%1").arg(gTestDbConnectionName)};

    Q_ASSERT(testLocalStorageDir.isValid());

    auto connectionPool = std::make_shared<ConnectionPool>(
        std::make_shared<SqlDatabaseWrapper>(), QStringLiteral("localhost"),
        QString::fromUtf8(gTestAccountName),
        QString::fromUtf8(gTestAccountName),
        testLocalStorageDir.filePath(QString::fromUtf8(gTestDatabaseFileName)),
        QStringLiteral("QSQLITE"));

    const QString localStorageDirPath = testLocalStorageDir.path() +
        QString::fromUtf8("/LocalAccounts/%1").arg(gTestAccountName);

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
    for (const auto & dirInfo: std::as_const(dirInfos)) {
        if (dirInfo.fileName().startsWith(backupDirPrefix)) {
            QDir backupDir{dir.absoluteFilePath(dirInfo.fileName())};

            const auto files =
                backupDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            EXPECT_FALSE(files.isEmpty());

            for (const auto & file: std::as_const(files)) {
                EXPECT_TRUE(file.fileName().startsWith(gTestDatabaseFileName));
            }

            foundLocalStorageBackup = true;
            break;
        }
    }

    EXPECT_TRUE(foundLocalStorageBackup);

    // Now ensure the ability to restore the backup

    EXPECT_TRUE(utility::removeFile(
        localStorageDirPath +
        QString::fromUtf8("/%1").arg(gTestDatabaseFileName)));

    res = sql::utils::restoreLocalStorageDatabaseFilesFromBackup(
        localStorageDirPath, backupDir.absolutePath(), promise,
        errorDescription);

    EXPECT_TRUE(res);

    auto fileInfos = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    EXPECT_FALSE(fileInfos.isEmpty());
    bool foundRestoredFromBackupLocalStorage = false;

    for (const auto & fileInfo: std::as_const(fileInfos)) {
        if (fileInfo.fileName() == QString::fromUtf8(gTestDatabaseFileName)) {
            foundRestoredFromBackupLocalStorage = true;
            break;
        }
    }

    dirInfos = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto & dirInfo: std::as_const(dirInfos)) {
        if (dirInfo.fileName().startsWith(backupDirPrefix)) {
            QDir backupDir{dir.absoluteFilePath(dirInfo.fileName())};

            const auto files =
                backupDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
            EXPECT_FALSE(files.isEmpty());

            for (const auto & file: std::as_const(files)) {
                EXPECT_TRUE(file.fileName().startsWith(
                    QString::fromUtf8(gTestDatabaseFileName)));
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

    const auto entries =
        dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    foundLocalStorageBackup = false;
    foundRestoredFromBackupLocalStorage = false;

    for (const auto & entryInfo: std::as_const(entries)) {
        if (entryInfo.fileName().startsWith(backupDirPrefix)) {
            foundLocalStorageBackup = true;
            break;
        }

        if (entryInfo.fileName() == QString::fromUtf8(gTestDatabaseFileName)) {
            foundRestoredFromBackupLocalStorage = true;
        }
    }

    EXPECT_TRUE(foundRestoredFromBackupLocalStorage);
    EXPECT_FALSE(foundLocalStorageBackup);

    writerThread->quit();
    writerThread->wait();
}

} // namespace quentier::local_storage::sql::tests
