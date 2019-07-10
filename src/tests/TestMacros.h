/*
 * Copyright 2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_TEST_MACROS_H
#define LIB_QUENTIER_TESTS_TEST_MACROS_H

#include <QtTest/QtTest>
#include <QDebug>

#if QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)
#define VERIFY_QDEBUG_HELPER()                                                 \
    dbg.nospace();                                                             \
    dbg.noquote()                                                              \
// VERIFY_QDEBUG_HELPER
#else
#define VERIFY_QDEBUG_HELPER()                                                 \
    dbg.nospace()                                                              \
// VERIFY_QDEBUG_HELPER
#endif

#define VERIFY2(condition, message)                                            \
    if (!(condition))                                                          \
    {                                                                          \
        QString msg;                                                           \
        {                                                                      \
            QDebug dbg(&msg);                                                  \
            VERIFY_QDEBUG_HELPER();                                            \
            dbg << message;                                                    \
        }                                                                      \
        QFAIL(qPrintable(msg));                                                \
    }                                                                          \
// VERIFY2

// 10 minutes should be enough
#define MAX_ALLOWED_TEST_DURATION_MSEC 600000

#endif // LIB_QUENTIER_TESTS_TEST_MACROS_H
