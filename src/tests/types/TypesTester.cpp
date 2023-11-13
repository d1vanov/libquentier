/*
 * Copyright 2018-2021 Dmitry Ivanov
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
#include <quentier/types/NoteUtils.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/utility/SysInfo.h>

#include <QApplication>
#include <QTextStream>
#include <QTest>

#define CATCH_EXCEPTION()                                                      \
    catch (const std::exception & exception) {                                 \
        SysInfo sysInfo;                                                       \
        QFAIL(qPrintable(                                                      \
            QStringLiteral("Caught exception: ") +                             \
            QString::fromUtf8(exception.what()) +                              \
            QStringLiteral(", backtrace: ") + sysInfo.stackTrace()));          \
    }

inline void messageHandler(
    QtMsgType type, const QMessageLogContext & /* context */,
    const QString & message)
{
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << "\n";
    }
}

namespace quentier::test {

TypesTester::TypesTester(QObject * parent) : QObject(parent) {}

TypesTester::~TypesTester() = default;

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

        const QString error =
            QStringLiteral("Wrong result of Note's containsToDo method");

        QVERIFY2(noteContentContainsCheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(
            noteContentContainsUncheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(noteContentContainsToDo(noteContent), qPrintable(error));

        noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1>"
            "<en-todo checked = \"true\"/>"
            "Completed item</en-note>");

        QVERIFY2(
            noteContentContainsCheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(
            !noteContentContainsUncheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(noteContentContainsToDo(noteContent), qPrintable(error));

        noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1><en-todo/>"
            "Not yet completed item</en-note>");

        QVERIFY2(
            !noteContentContainsCheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(
            noteContentContainsUncheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(noteContentContainsToDo(noteContent), qPrintable(error));

        noteContent = QStringLiteral(
            "<en-note><h1>Hello, world!</h1>"
            "<en-todo checked = \"false\"/>"
            "Not yet completed item</en-note>");

        QVERIFY2(
            !noteContentContainsCheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(
            noteContentContainsUncheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(noteContentContainsToDo(noteContent), qPrintable(error));

        noteContent =
            QStringLiteral("<en-note><h1>Hello, world!</h1></en-note>");

        QVERIFY2(
            !noteContentContainsCheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(
            !noteContentContainsUncheckedToDo(noteContent), qPrintable(error));

        QVERIFY2(!noteContentContainsToDo(noteContent), qPrintable(error));
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

        const QString error =
            QStringLiteral("Wrong result of Note's containsEncryption method");

        QVERIFY2(
            noteContentContainsEncryptedFragments(noteContent),
            qPrintable(error));

        const QString noteContentWithoutEncryption =
            QStringLiteral("<en-note><h1>Hello, world!</h1></en-note>");

        QVERIFY2(
            !noteContentContainsEncryptedFragments(noteContentWithoutEncryption),
            qPrintable(error));

        QVERIFY2(
            !noteContentContainsEncryptedFragments(QString{}),
            qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void TypesTester::resourceRecognitionIndicesParsingTest()
{
    try {
        QString error;
        const bool res = parseResourceRecognitionIndicesAndItemsTest(error);
        QVERIFY2(res, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

} // namespace quentier::test
