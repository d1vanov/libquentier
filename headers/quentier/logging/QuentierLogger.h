/*
 * Copyright 2016-2020 Dmitry Ivanov
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

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error
};

void QUENTIER_EXPORT QuentierInitializeLogging();

void QUENTIER_EXPORT QuentierAddLogEntry(
    const QString & sourceFileName,
    const int sourceFileLineNumber,
    const QString & message,
    const LogLevel logLevel);

LogLevel QUENTIER_EXPORT QuentierMinLogLevel();

void QUENTIER_EXPORT QuentierSetMinLogLevel(const LogLevel logLevel);

void QUENTIER_EXPORT QuentierAddStdOutLogDestination();

bool QUENTIER_EXPORT QuentierIsLogLevelActive(const LogLevel logLevel);

QString QUENTIER_EXPORT QuentierLogFilesDirPath();

void QUENTIER_EXPORT QuentierRestartLogging();

} // namespace quentier

#define __QNLOG_QDEBUG_HELPER()                                                \
    dbg.nospace();                                                             \
    dbg.noquote()                                                              \
// __QNLOG_QDEBUG_HELPER

#define __QNLOG_BASE(message, level)                                           \
    if (quentier::QuentierIsLogLevelActive(quentier::LogLevel::level))         \
    {                                                                          \
        QString msg;                                                           \
        QDebug dbg(&msg);                                                      \
        __QNLOG_QDEBUG_HELPER();                                               \
        dbg << message;                                                        \
        quentier::QuentierAddLogEntry(                                         \
            QStringLiteral(__FILE__),                                          \
            __LINE__, msg,                                                     \
            quentier::LogLevel::level);                                        \
    }                                                                          \
// __QNLOG_BASE

#define QNTRACE(message)                                                       \
    __QNLOG_BASE(message, Trace)                                               \
// QNTRACE

#define QNDEBUG(message)                                                       \
    __QNLOG_BASE(message, Debug)                                               \
// QNDEBUG

#define QNINFO(message)                                                        \
    __QNLOG_BASE(message, Info)                                                \
// QNINFO

#define QNWARNING(message)                                                     \
    __QNLOG_BASE(message, Warning)                                             \
// QNWARNING

#define QNERROR(message)                                                       \
    __QNLOG_BASE(message, Error)                                               \
// QNERROR

#define QUENTIER_SET_MIN_LOG_LEVEL(level)                                      \
    quentier::QuentierSetMinLogLevel(quentier::LogLevel::level)                \
// QUENTIER_SET_MIN_LOG_LEVEL

#define QUENTIER_INITIALIZE_LOGGING()                                          \
    quentier::QuentierInitializeLogging()                                      \
// QUENTIER_INITIALIZE_LOGGING

#define QUENTIER_ADD_STDOUT_LOG_DESTINATION()                                  \
    quentier::QuentierAddStdOutLogDestination()                                \
// QUENTIER_ADD_STDOUT_LOG_DESTINATION

#define QNLOG_FILE_LINENUMBER_DELIMITER ":"

#endif // LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_H
