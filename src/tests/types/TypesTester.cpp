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

#include "TypesTester.h"

#include "ResourceRecognitionIndicesParsingTest.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/Note.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/types/Resource.h>
#include <quentier/utility/SysInfo.h>

#include <QApplication>
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

TypesTester::TypesTester(QObject * parent) : QObject(parent) {}

TypesTester::~TypesTester() {}

void TypesTester::init()
{
    registerMetatypes();
    qInstallMessageHandler(messageHandler);
}

void TypesTester::noteContainsToDoTest()
{
    try {
        QString noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1>"
            "<en-todo checked = \"true\"/>"
            "Completed item<en-todo/>Not yet "
            "completed item</en-note>");

        Note note;
        note.setContent(noteContent);

        QString error =
            QStringLiteral("Wrong result of Note's containsToDo method");

        QVERIFY2(note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1>"
            "<en-todo checked = \"true\"/>"
            "Completed item</en-note>");

        note.setContent(noteContent);

        QVERIFY2(note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(!note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1><en-todo/>"
            "Not yet completed item</en-note>");

        note.setContent(noteContent);

        QVERIFY2(!note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1>"
            "<en-todo checked = \"false\"/>"
            "Not yet completed item</en-note>");

        note.setContent(noteContent);

        QVERIFY2(!note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(note.containsTodo(), qPrintable(error));

        noteContent =
            QStringLiteral("<en-note><h1>Hello, world!</h1></en-note>");

        note.setContent(noteContent);

        QVERIFY2(!note.containsCheckedTodo(), qPrintable(error));
        QVERIFY2(!note.containsUncheckedTodo(), qPrintable(error));
        QVERIFY2(!note.containsTodo(), qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void TypesTester::noteContainsEncryptionTest()
{
    try {
        QString noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1><en-crypt hint = \"the hint\" "
            "cipher = \"RC2\" length = \"64\">"
            "NKLHX5yK1MlpzemJQijAN6C4545s2EODx"
            "Q8Bg1r==</en-crypt></en-note>");

        Note note;
        note.setContent(noteContent);

        QString error =
            QStringLiteral("Wrong result of Note's containsEncryption method");

        QVERIFY2(note.containsEncryption(), qPrintable(error));

        QString noteContentWithoutEncryption =
            QStringLiteral("<en-note><h1>Hello, world!</h1></en-note>");

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

void TypesTester::resourceRecognitionIndicesParsingTest()
{
    try {
        QString error;
        bool res = parseResourceRecognitionIndicesAndItemsTest(error);
        QVERIFY2(res, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

} // namespace test
} // namespace quentier
