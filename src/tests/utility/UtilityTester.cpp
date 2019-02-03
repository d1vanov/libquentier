/*
 * Copyright 2016-2019 Dmitry Ivanov
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
#include "EncryptionManagerTests.h"
#include "TagSortByParentChildRelationsTest.h"
#include "LRUCacheTests.h"
#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Tag.h>
#include <quentier/types/Resource.h>
#include <quentier/types/Note.h>
#include <quentier/types/SharedNote.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/User.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/SysInfo.h>
#include <QApplication>
#include <QTextStream>
#include <QtTest/QTest>
#include <QTimer>

namespace quentier {
namespace test {

UtilityTester::UtilityTester(QObject * parent) :
    QObject(parent)
{}

UtilityTester::~UtilityTester()
{}

#if QT_VERSION >= 0x050000
inline void nullMessageHandler(QtMsgType type, const QMessageLogContext &,
                               const QString & message)
{
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#else
inline void nullMessageHandler(QtMsgType type, const char * message)
{
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#endif

void UtilityTester::init()
{
    registerMetatypes();

#if QT_VERSION >= 0x050000
    qInstallMessageHandler(nullMessageHandler);
#else
    qInstallMsgHandler(nullMessageHandler);
#endif
}

#define CATCH_EXCEPTION() \
    catch(const std::exception & exception) { \
        SysInfo sysInfo; \
        QFAIL(qPrintable(QStringLiteral("Caught exception: ") + \
                         QString::fromUtf8(exception.what()) + \
                         QStringLiteral(", backtrace: ") + \
                         sysInfo.stackTrace())); \
    }


void UtilityTester::encryptDecryptNoteTest()
{
    try
    {
        QString error;
        bool res = encryptDecryptTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void UtilityTester::decryptNoteAesTest()
{
    try
    {
        QString error;
        bool res = decryptAesTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void UtilityTester::decryptNoteRc2Test()
{
    try
    {
        QString error;
        bool res = decryptRc2Test(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void UtilityTester::tagSortByParentChildRelationsTest()
{
    try
    {
        QString error;
        bool res = ::quentier::test::tagSortByParentChildRelationsTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void UtilityTester::lruCacheTests()
{
    try
    {
        QString error;
        bool res = ::quentier::test::testEmptyLRUCacheConsistency(error);
        QVERIFY2(res == true, qPrintable(error));

        res = ::quentier::test::testNonEmptyLRUCacheConsistency(error);
        QVERIFY2(res == true, qPrintable(error));

        res = ::quentier::test::testRemovalFromLRUCache(error);
        QVERIFY2(res == true, qPrintable(error));

        res = ::quentier::test::testLRUCacheReverseIterators(error);
        QVERIFY2(res == true, qPrintable(error));

        res = ::quentier::test::testItemsAdditionToLRUCacheBeforeReachingMaxSize(error);
        QVERIFY2(res == true, qPrintable(error));

        res = ::quentier::test::testItemsAdditionToLRUCacheAfterReachingMaxSize(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

#undef CATCH_EXCEPTION

} // namespace test
} // namespace quentier
