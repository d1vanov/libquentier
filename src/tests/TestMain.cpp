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

#include "CoreTester.h"
#include "enml/ENMLTester.h"
#include "local_storage/LocalStorageManagerTester.h"
#include "synchronization/FullSyncStaleDataItemsExpungerTester.h"
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/QuentierApplication.h>
#include <quentier/utility/Utility.h>
#include <QtTest/QtTest>
#include <QDebug>
#include <QSqlDatabase>

using namespace quentier::test;

int main(int argc, char *argv[])
{
    quentier::QuentierApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("d1vanov"));
    app.setApplicationName(QStringLiteral("QuentierCoreTests"));

    QUENTIER_INITIALIZE_LOGGING();
    QUENTIER_SET_MIN_LOG_LEVEL(Warn);
    QUENTIER_ADD_STDOUT_LOG_DESTINATION();

    quentier::initializeLibquentier();

    int res = QTest::qExec(new CoreTester);
    if (res != 0) {
        return res;
    }

    res = QTest::qExec(new ENMLTester);
    if (res != 0) {
        return res;
    }

    res = QTest::qExec(new LocalStorageManagerTester);
    if (res != 0) {
        return res;
    }

    res = QTest::qExec(new FullSyncStaleDataItemsExpungerTester);
    if (res != 0) {
        return res;
    }

    return 0;
}
