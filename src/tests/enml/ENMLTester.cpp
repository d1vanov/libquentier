/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include "ENMLTester.h"

#include "ENMLConverterTests.h"
#include "EnexExportImportTests.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/utility/SysInfo.h>

#include <QTextStream>
#include <QtTest/QTest>

#define CATCH_EXCEPTION()                                                      \
    catch (const std::exception & exception) {                                 \
        SysInfo sysInfo;                                                       \
        QFAIL(qPrintable(                                                      \
            QStringLiteral("Caught exception: ") +                             \
            QString::fromUtf8(exception.what()) +                              \
            QStringLiteral(", backtrace: ") + sysInfo.stackTrace()));          \
    }

inline void messageHandler(
    QtMsgType type, const QMessageLogContext &, const QString & message)
{
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << "\n";
    }
}

namespace quentier {
namespace test {

ENMLTester::ENMLTester(QObject * parent) : QObject(parent) {}

ENMLTester::~ENMLTester() {}

void ENMLTester::init()
{
    registerMetatypes();
    qInstallMessageHandler(messageHandler);
}

void ENMLTester::enmlConverterSimpleTest()
{
    try {
        QString error;
        bool res = convertSimpleNoteToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterToDoTest()
{
    try {
        QString error;
        bool res = convertNoteWithToDoTagsToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterEnCryptTest()
{
    try {
        QString error;
        bool res = convertNoteWithEncryptionToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterEnCryptWithModifiedDecryptedTextTest()
{
    try {
        QString error;
        bool res = convertHtmlWithModifiedDecryptedTextToEnml(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterEnMediaTest()
{
    try {
        QString error;
        bool res = convertNoteWithResourcesToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterComplexTest()
{
    try {
        QString error;
        bool res = convertComplexNoteToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterComplexTest2()
{
    try {
        QString error;
        bool res = convertComplexNote2ToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterComplexTest3()
{
    try {
        QString error;
        bool res = convertComplexNote3ToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterComplexTest4()
{
    try {
        QString error;
        bool res = convertComplexNote4ToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterHtmlWithTableHelperTags()
{
    try {
        QString error;
        bool res = convertHtmlWithTableHelperTagsToEnml(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enmlConverterHtmlWithTableAndHilitorHelperTags()
{
    try {
        QString error;
        bool res = convertHtmlWithTableAndHilitorHelperTagsToEnml(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enexExportImportSingleSimpleNoteTest()
{
    try {
        QString error;
        bool res =
            exportSingleNoteWithoutTagsAndResourcesToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enexExportImportSingleNoteWithTagsTest()
{
    try {
        QString error;
        bool res =
            exportSingleNoteWithTagsButNoResourcesToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enexExportImportSingleNoteWithResourcesTest()
{
    try {
        QString error;
        bool res =
            exportSingleNoteWithResourcesButNoTagsToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enexExportImportSingleNoteWithTagsAndResourcesTest()
{
    try {
        QString error;
        bool res =
            exportSingleNoteWithTagsAndResourcesToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enexExportImportSingleNoteWithTagsButSkipTagsTest()
{
    try {
        QString error;
        bool res =
            exportSingleNoteWithTagsToEnexButSkipTagsAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::enexExportImportMultipleNotesWithTagsAndResourcesTest()
{
    try {
        QString error;
        bool res = exportMultipleNotesWithTagsAndResourcesAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void ENMLTester::importRealWorldEnexTest()
{
    try {
        QString error;
        bool res = importRealWorldEnex(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

} // namespace test
} // namespace quentier
