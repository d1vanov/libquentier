/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#pragma once

#include <quentier/utility/Linkage.h>

#include <QDebug>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>

namespace quentier {

/**
 * The LogLevel enumeration defines different levels for log entries which are
 * meant to separate log entries with different importance and meaning
 */
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error
};

QUENTIER_EXPORT QDebug & operator<<(QDebug & dbg, LogLevel logLevel);

QUENTIER_EXPORT QTextStream & operator<<(QTextStream & strm, LogLevel logLevel);

/**
 * This function needs to be called once during a process lifetime before
 * libquentier is used by the process. It initializes some internal data
 * structures used by libquentier's logging subsystem and prepares to write logs
 * to rotated files in directory path to which is returned by
 * QuentierLogFilesDirPath function.
 */
void QUENTIER_EXPORT QuentierInitializeLogging();

/**
 * This function is used to add new log entry to logs written by libquentier
 */
void QUENTIER_EXPORT QuentierAddLogEntry(
    const QString & sourceFileName, int sourceFileLineNumber,
    const QString & component, const QString & message, LogLevel logLevel);

/**
 * Current minimal log level used by libquentier. By default minimal log level
 * is LogLevel::Info which means that Info, Warning and Error logs are being
 * output but Debug and Trace ones are not
 */
LogLevel QUENTIER_EXPORT QuentierMinLogLevel();

/**
 * Change the current minimal log level used by libquentier
 */
void QUENTIER_EXPORT QuentierSetMinLogLevel(LogLevel logLevel);

/**
 * Call this function to write logs not only to rotating files but also to
 * stdout
 */
void QUENTIER_EXPORT QuentierAddStdOutLogDestination();

/**
 * Check whether log level is active i.e. whether log level is larger than or
 * equal to the minimal log level
 */
[[nodiscard]] bool QUENTIER_EXPORT QuentierIsLogLevelActive(LogLevel logLevel);

/**
 * Directory containing rotating log files written by libquentier
 */
[[nodiscard]] QString QUENTIER_EXPORT QuentierLogFilesDirPath();

/**
 * Clear logs accumulated within the existing log file
 */
void QUENTIER_EXPORT QuentierRestartLogging();

/**
 * Current filter specified for log components
 */
[[nodiscard]] QRegularExpression QUENTIER_EXPORT QuentierLogComponentFilter();

/**
 * Change the current filter for log components
 */
void QUENTIER_EXPORT
    QuentierSetLogComponentFilter(const QRegularExpression & filter);

} // namespace quentier

#define QNLOG_PRIVATE_BASE(component, message, level)                          \
    if (quentier::QuentierIsLogLevelActive(quentier::LogLevel::level)) {       \
        QString msg;                                                           \
        QDebug dbg(&msg);                                                      \
        dbg.nospace();                                                         \
        dbg.noquote();                                                         \
        dbg << message;                                                        \
        quentier::QuentierAddLogEntry(                                         \
            QStringLiteral(__FILE__), __LINE__, QString::fromUtf8(component),  \
            msg, quentier::LogLevel::level);                                   \
    }                                                                          \
    // QNLOG_PRIVATE_BASE

#define QNTRACE(component, message)                                            \
    QNLOG_PRIVATE_BASE(component, message, Trace)                              \
    // QNTRACE

#define QNDEBUG(component, message)                                            \
    QNLOG_PRIVATE_BASE(component, message, Debug)                              \
    // QNDEBUG

#define QNINFO(component, message)                                             \
    QNLOG_PRIVATE_BASE(component, message, Info)                               \
    // QNINFO

#define QNWARNING(component, message)                                          \
    QNLOG_PRIVATE_BASE(component, message, Warning)                            \
    // QNWARNING

#define QNERROR(component, message)                                            \
    QNLOG_PRIVATE_BASE(component, message, Error)                              \
    // QNERROR

#define QUENTIER_SET_MIN_LOG_LEVEL(level)                                      \
    quentier::QuentierSetMinLogLevel(quentier::LogLevel::level)
  // QUENTIER_SET_MIN_LOG_LEVEL

#define QUENTIER_INITIALIZE_LOGGING() quentier::QuentierInitializeLogging()
  // QUENTIER_INITIALIZE_LOGGING

// clang-format off
#define QUENTIER_ADD_STDOUT_LOG_DESTINATION()                                  \
    quentier::QuentierAddStdOutLogDestination()                                \
    // QUENTIER_ADD_STDOUT_LOG_DESTINATION
// clang-format on

#define QNLOG_FILE_LINENUMBER_DELIMITER ":"
