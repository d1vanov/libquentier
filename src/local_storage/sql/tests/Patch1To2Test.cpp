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

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"
#include "../Fwd.h"
#include "../TablesInitializer.h"
#include "../patches/Patch1To2.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtGlobal>
#include <QThread>
#include <QTemporaryDir>

namespace quentier::local_storage::sql::tests {

namespace {

const QString gTestDbConnectionName =
    QStringLiteral("libquentier_local_storage_sql_patch1to2_test_db");

const QString gTestDatabaseFileName = QStringLiteral("qn.storage.sqlite");

const QString gTestAccountName = QStringLiteral("testAccountName");

// Adds columns dataBody and alternateDataBody to Resources table within the
// passed in database so that the db schema corresponds to that of version 1
// of the local storage
void addResourceTableColumnsFromVersion1(QSqlDatabase & database)
{
    QSqlQuery query{database};

    bool res = query.exec(QStringLiteral(
        "ALTER TABLE Resources ADD COLUMN dataBody TEXT DEFAULT NULL"));

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::tests::Patch1To2Test",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tests::Patch1To2Test",
            "Failed to insert dataBody column into Resources table"));

    // clang-format off
    res = query.exec(QStringLiteral("ALTER TABLE Resources ADD COLUMN "
                                    "alternateDataBody TEXT DEFAULT NULL"));
    // clang-format on

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::tests::Patch1To2Test",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tests::Patch1To2Test",
            "Failed to insert alternateDataBody column into Resources table"));
}

void ensureFile(const QDir & dir, const QString & fileName)
{
    QFile databaseFile{dir.filePath(fileName)};

    EXPECT_TRUE(databaseFile.open(QIODevice::WriteOnly));
    databaseFile.write(QByteArray::fromRawData("0", 1));
    databaseFile.flush();
    databaseFile.close();

    databaseFile.resize(0);
}

// Prepares local storage database corresponding to version 1 in a temporary dir
// so that it can be upgraded from version 1 to version 2
void prepareLocalStorageForUpgrade(
    const QString & localStorageDirPath,
    ConnectionPool & connectionPool)
{
    QDir dir{localStorageDirPath};
    if (!dir.exists()) {
        const bool res = dir.mkpath(localStorageDirPath);
        Q_ASSERT(res);
    }

    ensureFile(dir, gTestDatabaseFileName);

    QFileInfo testDatabaseFileInfo{dir.absoluteFilePath(gTestDatabaseFileName)};
    EXPECT_TRUE(testDatabaseFileInfo.exists());
    EXPECT_TRUE(testDatabaseFileInfo.isFile());
    EXPECT_TRUE(testDatabaseFileInfo.isReadable());
    EXPECT_TRUE(testDatabaseFileInfo.isWritable());

    auto database = connectionPool.database();
    database.setHostName(QStringLiteral("localhost"));
    database.setUserName(gTestAccountName);
    database.setPassword(gTestAccountName);
    database.setDatabaseName(testDatabaseFileInfo.absoluteFilePath());

    TablesInitializer::initializeTables(database);
    addResourceTableColumnsFromVersion1(database);

    // TODO: fill the database with test content
}

} // namespace

TEST(Patch1To2Test, Ctor)
{
    Account account{gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_NO_THROW(const auto patch = std::make_shared<Patch1To2>(
        std::move(account), std::move(connectionPool),
        std::move(pWriterThread)));
}

TEST(Patch1To2Test, CtorEmptyAccount)
{
    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch1To2>(
            Account{}, std::move(connectionPool), std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch1To2Test, CtorNullConnectionPool)
{
    Account account{gTestAccountName, Account::Type::Local};

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch1To2>(
            std::move(account), nullptr, std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch1To2Test, CtorNullWriterThread)
{
    Account account{gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch1To2>(
            std::move(account), std::move(connectionPool), nullptr),
        IQuentierException);
}

TEST(Patch1To2Test, BackupLocalStorageTest)
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

    prepareLocalStorageForUpgrade(localStorageDirPath, *connectionPool);

    const auto writerThread = std::make_shared<QThread>();
    writerThread->start();

    qputenv(
        LIBQUENTIER_PERSISTENCE_STORAGE_PATH,
        testLocalStorageDir.path().toLocal8Bit());

    const auto patch = std::make_shared<Patch1To2>(
        std::move(account), std::move(connectionPool), writerThread);

    auto backupLocalStorageFuture = patch->backupLocalStorage();
    backupLocalStorageFuture.waitForFinished();

    bool foundLocalStorageBackup = false;
    QDir dir{localStorageDirPath};
    auto dirInfos = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    EXPECT_FALSE(dirInfos.isEmpty());
    for (const auto & dirInfo: qAsConst(dirInfos)) {
        if (dirInfo.fileName().startsWith(
                QStringLiteral("backup_upgrade_1_to_2_")))
        {
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

    auto restoreLocalStorageFromBackupFuture =
        patch->restoreLocalStorageFromBackup();

    restoreLocalStorageFromBackupFuture.waitForFinished();

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
        if (dirInfo.fileName().startsWith(
                QStringLiteral("backup_upgrade_1_to_2_")))
        {
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

    auto removeLocalStorageBackupFuture =
        patch->removeLocalStorageBackup();

    removeLocalStorageBackupFuture.waitForFinished();

    const auto entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    foundLocalStorageBackup = false;
    foundRestoredFromBackupLocalStorage = false;

    for (const auto & entryInfo: qAsConst(entries)) {
        if (entryInfo.fileName().startsWith(
                QStringLiteral("backup_upgrade_1_to_2_")))
        {
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
