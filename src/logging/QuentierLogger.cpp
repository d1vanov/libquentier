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

#include <quentier/logging/QuentierLogger.h>
#include "QuentierLogger_p.h"

#include <qt5qevercloud/Log.h>

#include <QCoreApplication>

#include <exception>

namespace quentier {

////////////////////////////////////////////////////////////////////////////////

LogLevel::type QEverCloudLogLevelToQuentierLogLevel(
    const qevercloud::LogLevel logLevel)
{
    switch(logLevel)
    {
    case qevercloud::LogLevel::Trace:
        return LogLevel::TraceLevel;
    case qevercloud::LogLevel::Debug:
        return LogLevel::DebugLevel;
    case qevercloud::LogLevel::Info:
        return LogLevel::InfoLevel;
    case qevercloud::LogLevel::Warn:
        return LogLevel::WarnLevel;
    case qevercloud::LogLevel::Error:
        return LogLevel::ErrorLevel;
    default:
        return LogLevel::InfoLevel;
    }
}

////////////////////////////////////////////////////////////////////////////////

class QEverCloudLogger final: public qevercloud::ILogger
{
public:
    virtual bool shouldLog(
        const qevercloud::LogLevel level, const char * component) const override
    {
        Q_UNUSED(component)

        auto logLevel = QEverCloudLogLevelToQuentierLogLevel(level);
        return QuentierIsLogLevelActive(logLevel);
    }

    virtual void log(
        const qevercloud::LogLevel level, const char * component,
        const char * fileName, const quint32 lineNumber, const qint64 timestamp,
        const QString & message) override
    {
        Q_UNUSED(component)
        Q_UNUSED(timestamp)

        QuentierAddLogEntry(
            QString::fromUtf8(fileName),
            static_cast<int>(lineNumber),
            message,
            QEverCloudLogLevelToQuentierLogLevel(level));
    }

    virtual void setLevel(const qevercloud::LogLevel level) override
    {
        Q_UNUSED(level)

        throw std::runtime_error(
            "Unimplemented method QEverCloudLogger::setLevel was called");
    }

    virtual qevercloud::LogLevel level() const override
    {
        auto logLevel = QuentierMinLogLevel();
        switch(logLevel)
        {
        case LogLevel::TraceLevel:
            return qevercloud::LogLevel::Trace;
        case LogLevel::DebugLevel:
            return qevercloud::LogLevel::Debug;
        case LogLevel::InfoLevel:
            return qevercloud::LogLevel::Info;
        case LogLevel::WarnLevel:
            return qevercloud::LogLevel::Warn;
        case LogLevel::ErrorLevel:
            return qevercloud::LogLevel::Error;
        default:
            return qevercloud::LogLevel::Info;
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

void QuentierInitializeLogging()
{
    QuentierLogger & logger = QuentierLogger::instance();
    Q_UNUSED(logger)

    qevercloud::setLogger(std::make_shared<QEverCloudLogger>());
}

void QuentierAddLogEntry(const QString & sourceFileName, const int sourceFileLineNumber,
                         const QString & message, const LogLevel::type logLevel)
{
    QString relativeSourceFileName = sourceFileName;

    int prefixIndex = relativeSourceFileName.indexOf(
        QStringLiteral("libquentier"),
        Qt::CaseInsensitive);

    if (prefixIndex < 0) {
        prefixIndex = relativeSourceFileName.indexOf(
            QStringLiteral("QEverCloud"),
            Qt::CaseInsensitive);
    }

    if (prefixIndex >= 0)
    {
        relativeSourceFileName.remove(0, prefixIndex);
    }
    else {
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
    case LogLevel::TraceLevel:
        logEntry += QStringLiteral("Trace]: ");
        break;
    case LogLevel::DebugLevel:
        logEntry += QStringLiteral("Debug]: ");
        break;
    case LogLevel::InfoLevel:
        logEntry += QStringLiteral("Info]: ");
        break;
    case LogLevel::WarnLevel:
        logEntry += QStringLiteral("Warn]: ");
        break;
    case LogLevel::ErrorLevel:
        logEntry += QStringLiteral("Error]: ");
        break;
    default:
        logEntry += QStringLiteral("Unknown log level: ") +
            QString::number(logLevel) + QStringLiteral("]: ");
        break;
    }

    logEntry += message;

    QuentierLogger & logger = QuentierLogger::instance();
    logger.write(logEntry);
}

LogLevel::type QuentierMinLogLevel()
{
    QuentierLogger & logger = QuentierLogger::instance();
    return logger.minLogLevel();
}

void QuentierSetMinLogLevel(const LogLevel::type logLevel)
{
    QuentierLogger & logger = QuentierLogger::instance();
    logger.setMinLogLevel(logLevel);
}

bool QuentierIsLogLevelActive(const LogLevel::type logLevel)
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

} // namespace quentier
