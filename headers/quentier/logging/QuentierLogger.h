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

#ifndef LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_H
#define LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_H

#include <quentier/utility/Linkage.h>
#include <quentier/utility/Macros.h>
#include <QString>
#include <QDebug>

namespace quentier {

class LogLevel
{
public:
    enum type {
        TraceLevel,
        DebugLevel,
        InfoLevel,
        WarnLevel,
        ErrorLevel
    };
};

void QUENTIER_EXPORT QuentierInitializeLogging();

void QUENTIER_EXPORT QuentierAddLogEntry(const QString & sourceFileName,
                                         const int sourceFileLineNumber,
                                         const QString & message,
                                         const LogLevel::type logLevel);

LogLevel::type QUENTIER_EXPORT QuentierMinLogLevel();

void QUENTIER_EXPORT QuentierSetMinLogLevel(const LogLevel::type logLevel);

void QUENTIER_EXPORT QuentierAddStdOutLogDestination();

bool QUENTIER_EXPORT QuentierIsLogLevelActive(const LogLevel::type logLevel);

QString QUENTIER_EXPORT QuentierLogFilesDirPath();

void QUENTIER_EXPORT QuentierRestartLogging();

} // namespace quentier

#if QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)
#define __QNLOG_QDEBUG_HELPER() \
    dbg.nospace(); \
    dbg.noquote()
#else
#define __QNLOG_QDEBUG_HELPER() \
    dbg.nospace()
#endif

#define __QNLOG_BASE(message, level) \
    if (quentier::QuentierIsLogLevelActive(quentier::LogLevel::level##Level)) { \
        QString msg; \
        QDebug dbg(&msg); \
        __QNLOG_QDEBUG_HELPER(); \
        dbg << message; \
        quentier::QuentierAddLogEntry(QStringLiteral(__FILE__), \
                                      __LINE__, msg, \
                                      quentier::LogLevel::level##Level); \
    }

#define QNTRACE(message) \
    __QNLOG_BASE(message, Trace)

#define QNDEBUG(message) \
    __QNLOG_BASE(message, Debug)

#define QNINFO(message) \
    __QNLOG_BASE(message, Info)

#define QNWARNING(message) \
    __QNLOG_BASE(message, Warn)

#define QNERROR(message) \
    __QNLOG_BASE(message, Error)

#define QUENTIER_SET_MIN_LOG_LEVEL(level) \
    quentier::QuentierSetMinLogLevel(quentier::LogLevel::level##Level)

#define QUENTIER_INITIALIZE_LOGGING() \
    quentier::QuentierInitializeLogging()

#define QUENTIER_ADD_STDOUT_LOG_DESTINATION() \
    quentier::QuentierAddStdOutLogDestination()

#define QNLOG_FILE_LINENUMBER_DELIMITER ":"

#endif // LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_H
