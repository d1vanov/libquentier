/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "tst_qmimetype.h"

#include <qmimetype_p.h>

#include <qmimetype.h>
#include <qmimedatabase.h>

#include <QtTest/QtTest>

// ------------------------------------------------------------------------------------------------

void tst_qmimetype::initTestCase()
{
    qputenv("XDG_DATA_DIRS", "doesnotexist");
}

// ------------------------------------------------------------------------------------------------

static QString qMimeTypeName()
{
    static const QString result ("No name of the MIME type");
    return result;
}

static QString qMimeTypeGenericIconName()
{
    static const QString result ("No file name of an icon image that represents the MIME type");
    return result;
}

static QString qMimeTypeIconName()
{
    static const QString result ("No file name of an icon image that represents the MIME type");
    return result;
}

static QStringList buildQMimeTypeFilenameExtensions()
{
    QStringList result;
    result << QString::fromLatin1("*.png");
    return result;
}

static QStringList qMimeTypeGlobPatterns()
{
    static const QStringList result (buildQMimeTypeFilenameExtensions());
    return result;
}

// ------------------------------------------------------------------------------------------------

#ifndef Q_COMPILER_RVALUE_REFS
QMIMETYPE_BUILDER
#else
QMIMETYPE_BUILDER_FROM_RVALUE_REFS
#endif

// ------------------------------------------------------------------------------------------------

void tst_qmimetype::isValid()
{
    QMimeType instantiatedQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeIconName(),
                      qMimeTypeGlobPatterns()
                  )
              );

    QVERIFY(instantiatedQMimeType.isValid());

    QMimeType otherQMimeType (instantiatedQMimeType);

    QVERIFY(otherQMimeType.isValid());
    QCOMPARE(instantiatedQMimeType, otherQMimeType);

    QMimeType defaultQMimeType;

    QVERIFY(!defaultQMimeType.isValid());
}

// ------------------------------------------------------------------------------------------------

void tst_qmimetype::name()
{
    QMimeType instantiatedQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeIconName(),
                      qMimeTypeGlobPatterns()
                  )
              );

    QMimeType otherQMimeType (
                  buildQMimeType (
                      QString(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeIconName(),
                      qMimeTypeGlobPatterns()
                  )
              );

    // Verify that the Name is part of the equality test:
    QCOMPARE(instantiatedQMimeType.name(), qMimeTypeName());

    QVERIFY(instantiatedQMimeType != otherQMimeType);
    QVERIFY(!(instantiatedQMimeType == otherQMimeType));
}

// ------------------------------------------------------------------------------------------------

void tst_qmimetype::genericIconName()
{
    QMimeType instantiatedQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeIconName(),
                      qMimeTypeGlobPatterns()
                  )
              );

    QMimeType otherQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      QString(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeGlobPatterns()
                  )
              );

    // Verify that the GenericIconName is part of the equality test:
    QCOMPARE(instantiatedQMimeType.genericIconName(), qMimeTypeGenericIconName());

    QVERIFY(instantiatedQMimeType != otherQMimeType);
    QVERIFY(!(instantiatedQMimeType == otherQMimeType));
}

// ------------------------------------------------------------------------------------------------

void tst_qmimetype::iconName()
{
    QMimeType instantiatedQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeIconName(),
                      qMimeTypeGlobPatterns()
                  )
              );

    QMimeType otherQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      qMimeTypeGenericIconName(),
                      QString(),
                      qMimeTypeGlobPatterns()
                  )
              );

    // Verify that the IconName is part of the equality test:
    QCOMPARE(instantiatedQMimeType.iconName(), qMimeTypeIconName());

    QVERIFY(instantiatedQMimeType != otherQMimeType);
    QVERIFY(!(instantiatedQMimeType == otherQMimeType));
}

// ------------------------------------------------------------------------------------------------

void tst_qmimetype::suffixes()
{
    QMimeType instantiatedQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeIconName(),
                      qMimeTypeGlobPatterns()
                  )
              );

    QMimeType otherQMimeType (
                  buildQMimeType (
                      qMimeTypeName(),
                      qMimeTypeGenericIconName(),
                      qMimeTypeIconName(),
                      QStringList()
                  )
              );

    // Verify that the Suffixes are part of the equality test:
    QCOMPARE(instantiatedQMimeType.globPatterns(), qMimeTypeGlobPatterns());
    QCOMPARE(instantiatedQMimeType.suffixes(), QStringList() << QString::fromLatin1("png"));

    QVERIFY(instantiatedQMimeType != otherQMimeType);
    QVERIFY(!(instantiatedQMimeType == otherQMimeType));
}

// ------------------------------------------------------------------------------------------------

#if (QT_VERSION < QT_VERSION_CHECK(5, 0, 0))
#define QTEST_GUILESS_MAIN(TestObject) \
int main(int argc, char *argv[]) \
{ \
    QCoreApplication app(argc, argv); \
    TestObject tc; \
    return QTest::qExec(&tc, argc, argv); \
}
#endif

QTEST_GUILESS_MAIN(tst_qmimetype)
