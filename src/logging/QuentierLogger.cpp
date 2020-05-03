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

#include <quentier/logging/QuentierLogger.h>

#include "QuentierLogger_p.h"

#include <QCoreApplication>

namespace quentier {

void QuentierInitializeLogging()
{
    QuentierLogger & logger = QuentierLogger::instance();
    Q_UNUSED(logger)
}

void QuentierAddLogEntry(
    const QString & sourceFileName, const int sourceFileLineNumber,
    const QString & message, const LogLevel logLevel)
{
    QString relativeSourceFileName = sourceFileName;
    int prefixIndex = relativeSourceFileName.indexOf(
        QStringLiteral("libquentier"),
        Qt::CaseInsensitive);
    if (prefixIndex >= 0)
    {
        relativeSourceFileName.remove(0, prefixIndex);
    }
    else
    {
        QString appName = QCoreApplication::applicationName().toLower();
        prefixIndex = relativeSourceFileName.indexOf(
            appName,
            Qt::CaseInsensitive);
        if (prefixIndex >= 0) {
            relativeSourceFileName.remove(0, prefixIndex + appName.size() + 1);
        }
    }

    QString logEntry = relativeSourceFileName;
    logEntry += QStringLiteral(QNLOG_FILE_LINENUMBER_DELIMITER);
    logEntry += QString::number(sourceFileLineNumber);
    logEntry += QStringLiteral(" [");

    switch(logLevel)
    {
    case LogLevel::Trace:
        logEntry += QStringLiteral("Trace]: ");
        break;
    case LogLevel::Debug:
        logEntry += QStringLiteral("Debug]: ");
        break;
    case LogLevel::Info:
        logEntry += QStringLiteral("Info]: ");
        break;
    case LogLevel::Warning:
        logEntry += QStringLiteral("Warn]: ");
        break;
    case LogLevel::Error:
        logEntry += QStringLiteral("Error]: ");
        break;
    default:
        logEntry += QStringLiteral("Unknown log level: ") +
            QString::number(static_cast<qint64>(logLevel)) +
            QStringLiteral("]: ");
        break;
    }

    logEntry += message;

    QuentierLogger & logger = QuentierLogger::instance();
    logger.write(logEntry);
}

LogLevel QuentierMinLogLevel()
{
    QuentierLogger & logger = QuentierLogger::instance();
    return logger.minLogLevel();
}

void QuentierSetMinLogLevel(const LogLevel logLevel)
{
    QuentierLogger & logger = QuentierLogger::instance();
    logger.setMinLogLevel(logLevel);
}

bool QuentierIsLogLevelActive(const LogLevel logLevel)
{
    return (QuentierLogger::instance().minLogLevel() <= logLevel);
}

void QuentierAddStdOutLogDestination()
{
    QuentierLogger & logger = QuentierLogger::instance();
    logger.addLogWriter(new QuentierConsoleLogWriter);
}

QString QuentierLogFilesDirPath()
{
    return QuentierLogger::logFilesDirPath();
}

void QuentierRestartLogging()
{
    QuentierLogger & logger = QuentierLogger::instance();
    logger.restartLogging();
}

////////////////////////////////////////////////////////////////////////////////

QDebug & operator<<(QDebug & dbg, const LogLevel logLevel)
{
    switch(logLevel)
    {
    case LogLevel::Trace:
        dbg << "Trace";
        break;
    case LogLevel::Debug:
        dbg << "Debug";
        break;
    case LogLevel::Info:
        dbg << "Info";
        break;
    case LogLevel::Warning:
        dbg << "Warning";
        break;
    case LogLevel::Error:
        dbg << "Error";
        break;
    default:
        dbg << "Unknown (" << static_cast<qint64>(logLevel) << ")";
        break;
    }

    return dbg;
}

} // namespace quentier
