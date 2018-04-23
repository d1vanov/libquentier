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
#include "enml/ENMLConverterTests.h"
#include "enml/EnexExportImportTests.h"
#include "types/ResourceRecognitionIndicesParsingTest.h"
#include "utility/EncryptionManagerTests.h"
#include "utility/TagSortByParentChildRelationsTest.h"
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

// 10 minutes should be enough
#define MAX_ALLOWED_MILLISECONDS 600000

namespace quentier {
namespace test {

CoreTester::CoreTester(QObject * parent) :
    QObject(parent)
{}

CoreTester::~CoreTester()
{}

#if QT_VERSION >= 0x050000
void nullMessageHandler(QtMsgType type, const QMessageLogContext &, const QString & message) {
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#else
void nullMessageHandler(QtMsgType type, const char * message) {
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#endif

void CoreTester::initTestCase()
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
        QFAIL(qPrintable(QStringLiteral("Caught exception: ") + QString::fromUtf8(exception.what()) + \
                         QStringLiteral(", backtrace: ") + sysInfo.stackTrace())); \
    }


void CoreTester::noteContainsToDoTest()
{
    try
    {
        QString noteContent = QStringLiteral("<en-note><h1>Hello, world!</h1><en-todo checked = \"true\"/>"
                                             "Completed item<en-todo/>Not yet completed item</en-note>");
        Note note;
        note.setContent(noteContent);

        QString error = QStringLiteral("Wrong result of Note's containsToDo method");
        QVERIFY2(note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent = QStringLiteral("<en-note><h1>Hello, world!</h1><en-todo checked = \"true\"/>"
                                     "Completed item</en-note>");
        note.setContent(noteContent);

        QVERIFY2(note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(!note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent = QStringLiteral("<en-note><h1>Hello, world!</h1><en-todo/>Not yet completed item</en-note>");
        note.setContent(noteContent);

        QVERIFY2(!note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent = QStringLiteral("<en-note><h1>Hello, world!</h1><en-todo checked = \"false\"/>Not yet completed item</en-note>");
        note.setContent(noteContent);

        QVERIFY2(!note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent = QStringLiteral("<en-note><h1>Hello, world!</h1></en-note>");
        note.setContent(noteContent);

        QVERIFY2(!note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(!note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(!note.containsTodo(), qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::noteContainsEncryptionTest()
{
    try
    {
        QString noteContent = QStringLiteral("<en-note><h1>Hello, world!</h1><en-crypt hint = \"the hint\" "
                                             "cipher = \"RC2\" length = \"64\">NKLHX5yK1MlpzemJQijAN6C4545s2EODxQ8Bg1r==</en-crypt></en-note>");

        Note note;
        note.setContent(noteContent);

        QString error = QStringLiteral("Wrong result of Note's containsEncryption method");
        QVERIFY2(note.containsEncryption(), qPrintable(error));

        QString noteContentWithoutEncryption = QStringLiteral("<en-note><h1>Hello, world!</h1></en-note>");
        note.setContent(noteContentWithoutEncryption);

        QVERIFY2(!note.containsEncryption(), qPrintable(error));

        note.clear();
        note.setContent(noteContentWithoutEncryption);

        QVERIFY2(!note.containsEncryption(), qPrintable(error));

        note.setContent(noteContent);
        QVERIFY2(note.containsEncryption(), qPrintable(error));

        note.clear();
        QVERIFY2(!note.containsEncryption(), qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::encryptDecryptNoteTest()
{
    try
    {
        QString error;
        bool res = encryptDecryptTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::decryptNoteAesTest()
{
    try
    {
        QString error;
        bool res = decryptAesTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::decryptNoteRc2Test()
{
    try
    {
        QString error;
        bool res = decryptRc2Test(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterSimpleTest()
{
    try
    {
        QString error;
        bool res = convertSimpleNoteToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterToDoTest()
{
    try
    {
        QString error;
        bool res = convertNoteWithToDoTagsToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterEnCryptTest()
{
    try
    {
        QString error;
        bool res = convertNoteWithEncryptionToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterEnCryptWithModifiedDecryptedTextTest()
{
    try
    {
        QString error;
        bool res = convertHtmlWithModifiedDecryptedTextToEnml(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterEnMediaTest()
{
    try
    {
        QString error;
        bool res = convertNoteWithResourcesToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterComplexTest()
{
    try
    {
        QString error;
        bool res = convertComplexNoteToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterComplexTest2()
{
    try
    {
        QString error;
        bool res = convertComplexNote2ToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterComplexTest3()
{
    try
    {
        QString error;
        bool res = convertComplexNote3ToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterComplexTest4()
{
    try
    {
        QString error;
        bool res = convertComplexNote4ToHtmlAndBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterHtmlWithTableHelperTags()
{
    try
    {
        QString error;
        bool res = convertHtmlWithTableHelperTagsToEnml(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enmlConverterHtmlWithTableAndHilitorHelperTags()
{
    try
    {
        QString error;
        bool res = convertHtmlWithTableAndHilitorHelperTagsToEnml(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enexExportImportSingleSimpleNoteTest()
{
    try
    {
        QString error;
        bool res = exportSingleNoteWithoutTagsAndResourcesToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enexExportImportSingleNoteWithTagsTest()
{
    try
    {
        QString error;
        bool res = exportSingleNoteWithTagsButNoResourcesToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enexExportImportSingleNoteWithResourcesTest()
{
    try
    {
        QString error;
        bool res = exportSingleNoteWithResourcesButNoTagsToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enexExportImportSingleNoteWithTagsAndResourcesTest()
{
    try
    {
        QString error;
        bool res = exportSingleNoteWithTagsAndResourcesToEnexAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enexExportImportSingleNoteWithTagsButSkipTagsTest()
{
    try
    {
        QString error;
        bool res = exportSingleNoteWithTagsToEnexButSkipTagsAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::enexExportImportMultipleNotesWithTagsAndResourcesTest()
{
    try
    {
        QString error;
        bool res = exportMultipleNotesWithTagsAndResourcesAndImportBack(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::importRealWorldEnexTest()
{
    try
    {
        QString error;
        bool res = importRealWorldEnex(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::tagSortByParentChildRelationsTest()
{
    try
    {
        QString error;
        bool res = ::quentier::test::tagSortByParentChildRelationsTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void CoreTester::resourceRecognitionIndicesParsingTest()
{
    try
    {
        QString error;
        bool res = parseResourceRecognitionIndicesAndItemsTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

#undef CATCH_EXCEPTION

} // namespace test
} // namespace quentier
