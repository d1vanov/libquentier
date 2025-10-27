/*
 * Copyright 2016-2025 Dmitry Ivanov
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

#include "types/TypesTester.h"
#include "utility/UtilityTester.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/Initialize.h>
#include <quentier/utility/QuentierApplication.h>
#include <quentier/utility/StandardPaths.h>

#include <QDebug>
#include <QDir>
#include <QTest>

using namespace quentier::test;

int main(int argc, char * argv[])
{
    quentier::utility::QuentierApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("d1vanov"));
    QCoreApplication::setApplicationName(QStringLiteral("LibquentierTests"));

    QUENTIER_INITIALIZE_LOGGING();
    QUENTIER_SET_MIN_LOG_LEVEL(Trace);
    // QUENTIER_ADD_STDOUT_LOG_DESTINATION();

    quentier::utility::initializeLibquentier();

    // Remove any persistence left after the previous run of tests
    QString libquentierTestsPersistencePath =
        quentier::applicationPersistentStoragePath();

    QDir libquentierTestsPersistenceDir(libquentierTestsPersistencePath);
    if (libquentierTestsPersistenceDir.exists()) {
        QString evernoteAccountsPath = libquentierTestsPersistencePath +
            QStringLiteral("/EvernoteAccounts");

        QDir evernoteAccountsDir(evernoteAccountsPath);
        if (evernoteAccountsDir.exists() &&
            !quentier::utility::removeDir(evernoteAccountsPath))
        {
            qWarning() << "Failed to delete the directory with libquentier "
                       << "tests persistence for Evernote accounts: "
                       << QDir::toNativeSeparators(evernoteAccountsPath);

            return 1;
        }

        QString localAccountsPath =
            libquentierTestsPersistencePath + QStringLiteral("/LocalAccounts");

        QDir localAccountsDir(localAccountsPath);
        if (localAccountsDir.exists() &&
            !quentier::utility::removeDir(localAccountsPath))
        {
            qWarning() << "Failed to delete the directory with libquentier "
                       << "tests persistence for local accounts: "
                       << QDir::toNativeSeparators(evernoteAccountsPath);

            return 1;
        }
    }

    int res = 0;

#define RUN_TESTS(tester)                                                      \
    res = QTest::qExec(new tester, argc, argv);                                \
    if (res != 0) {                                                            \
        return res;                                                            \
    }

    RUN_TESTS(TypesTester)
    RUN_TESTS(UtilityTester)

    return 0;
}
