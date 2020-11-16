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

#include "enml/ENMLTester.h"
#include "local_storage/LocalStorageManagerTester.h"
#include "synchronization/FullSyncStaleDataItemsExpungerTester.h"
#include "synchronization/SynchronizationTester.h"
#include "types/TypesTester.h"
#include "utility/UtilityTester.h"
#include "utility/keychain/CompositeKeychainTester.h"
#include "utility/keychain/MigratingKeychainTester.h"
#include "utility/keychain/ObfuscatingKeychainTester.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/Initialize.h>
#include <quentier/utility/QuentierApplication.h>
#include <quentier/utility/StandardPaths.h>

#include <QDebug>
#include <QDir>
#include <QtTest/QtTest>

using namespace quentier::test;

int main(int argc, char * argv[])
{
    quentier::QuentierApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("d1vanov"));
    app.setApplicationName(QStringLiteral("LibquentierTests"));

    QUENTIER_INITIALIZE_LOGGING();
    QUENTIER_SET_MIN_LOG_LEVEL(Trace);
    // QUENTIER_ADD_STDOUT_LOG_DESTINATION();

    quentier::initializeLibquentier();

    // Remove any persistence left after the previous run of tests
    QString libquentierTestsPersistencePath =
        quentier::applicationPersistentStoragePath();

    QDir libquentierTestsPersistenceDir(libquentierTestsPersistencePath);
    if (libquentierTestsPersistenceDir.exists()) {
        QString evernoteAccountsPath = libquentierTestsPersistencePath +
            QStringLiteral("/EvernoteAccounts");

        QDir evernoteAccountsDir(evernoteAccountsPath);
        if (evernoteAccountsDir.exists() &&
            !quentier::removeDir(evernoteAccountsPath)) {
            qWarning() << "Failed to delete the directory with libquentier "
                       << "tests persistence for Evernote accounts: "
                       << QDir::toNativeSeparators(evernoteAccountsPath);

            return 1;
        }

        QString localAccountsPath =
            libquentierTestsPersistencePath + QStringLiteral("/LocalAccounts");

        QDir localAccountsDir(localAccountsPath);
        if (localAccountsDir.exists() &&
            !quentier::removeDir(localAccountsPath)) {
            qWarning() << "Failed to delete the directory with libquentier "
                       << "tests persistence for local accounts: "
                       << QDir::toNativeSeparators(evernoteAccountsPath);

            return 1;
        }
    }

    int res = 0;

#define RUN_TESTS(tester)                                                      \
    res = QTest::qExec(new tester);                                            \
    if (res != 0) {                                                            \
        return res;                                                            \
    }

    RUN_TESTS(TypesTester)
    RUN_TESTS(ENMLTester)
    RUN_TESTS(UtilityTester)
    RUN_TESTS(CompositeKeychainTester)
    RUN_TESTS(MigratingKeychainTester)
    RUN_TESTS(ObfuscatingKeychainTester)
    RUN_TESTS(LocalStorageManagerTester)
    RUN_TESTS(FullSyncStaleDataItemsExpungerTester)
    RUN_TESTS(SynchronizationTester)

    return 0;
}
