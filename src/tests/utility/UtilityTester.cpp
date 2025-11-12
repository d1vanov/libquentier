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

#include "UtilityTester.h"

#include "LRUCacheTests.h"
#include "TagSortByParentChildRelationsTest.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/RegisterMetatypes.h>

#include <quentier/utility/SysInfo.h>

#include <QApplication>
#include <QTest>
#include <QTextStream>
#include <QTimer>

namespace quentier::test {

UtilityTester::UtilityTester(QObject * parent) : QObject(parent) {}

UtilityTester::~UtilityTester() noexcept = default;

inline void messageHandler(
    QtMsgType type, const QMessageLogContext & /* context */,
    const QString & message)
{
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}

void UtilityTester::init()
{
    registerMetatypes();
    qInstallMessageHandler(messageHandler);
}

#define CATCH_EXCEPTION()                                                      \
    catch (const std::exception & exception) {                                 \
        utility::SysInfo sysInfo;                                              \
        QFAIL(qPrintable(                                                      \
            QStringLiteral("Caught exception: ") +                             \
            QString::fromUtf8(exception.what()) +                              \
            QStringLiteral(", backtrace: ") + sysInfo.stackTrace()));          \
    }

void UtilityTester::tagSortByParentChildRelationsTest()
{
    try {
        QString error;
        const bool res =
            ::quentier::test::tagSortByParentChildRelationsTest(error);
        QVERIFY2(res, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void UtilityTester::lruCacheTests()
{
    try {
        QString error;
        bool res =
            ::quentier::utility::test::testEmptyLRUCacheConsistency(error);
        QVERIFY2(res, qPrintable(error));

        res = ::quentier::utility::test::testNonEmptyLRUCacheConsistency(error);
        QVERIFY2(res, qPrintable(error));

        res = ::quentier::utility::test::testRemovalFromLRUCache(error);
        QVERIFY2(res, qPrintable(error));

        res = ::quentier::utility::test::testLRUCacheReverseIterators(error);
        QVERIFY2(res, qPrintable(error));

        res = ::quentier::utility::test::
            testItemsAdditionToLRUCacheBeforeReachingMaxSize(error);

        QVERIFY2(res, qPrintable(error));

        res = ::quentier::utility::test::
            testItemsAdditionToLRUCacheAfterReachingMaxSize(error);

        QVERIFY2(res, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

#undef CATCH_EXCEPTION

} // namespace quentier::test
