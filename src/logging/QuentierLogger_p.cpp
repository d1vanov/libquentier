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

#include "QuentierLogger_p.h"

#include <quentier/exception/LoggerInitializationException.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/StandardPaths.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextCodec>

#include <iostream>

#if defined Q_OS_WIN
#include <Windows.h>
#endif

namespace quentier {

QuentierFileLogWriter::QuentierFileLogWriter(
    const MaxSizeBytes & maxSizeBytes,
    const MaxOldLogFilesCount & maxOldLogFilesCount, QObject * parent) :
    IQuentierLogWriter(parent),
    m_maxSizeBytes(maxSizeBytes.size()),
    m_maxOldLogFilesCount(maxOldLogFilesCount.count())
{
    QString logFileDirPath = QuentierLogger::logFilesDirPath();

    QDir logFileDir(logFileDirPath);
    if (Q_UNLIKELY(!logFileDir.exists())) {
        if (Q_UNLIKELY(!logFileDir.mkpath(QStringLiteral(".")))) {
            ErrorString error(QT_TR_NOOP("Can't create the log file path"));
            error.details() = logFileDirPath;
            throw LoggerInitializationException(error);
        }
    }

    QString logFileName = logFileDirPath + QStringLiteral("/") +
        QCoreApplication::applicationName() + QStringLiteral("-log.txt");

    m_logFile.setFileName(logFileName);

    bool opened = m_logFile.open(
        QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered |
        QIODevice::Text);

    if (Q_UNLIKELY(!opened)) {
        ErrorString error(
            QT_TR_NOOP("Can't open the log file for writing/appending"));
        error.details() = m_logFile.errorString();
        error.details() += QStringLiteral(", error code = ");
        error.details() += QString::number(m_logFile.error());
        throw LoggerInitializationException(error);
    }

    m_currentLogFileSize = m_logFile.size();

    // Seek for old log files with indices from 1 to m_maxOldLogFilesCount,
    // count the existing ones
    for (int i = 1; i < m_maxOldLogFilesCount; ++i) {
        const QString previousLogFilePath = logFileDirPath +
            QStringLiteral("/") + QCoreApplication::applicationName() +
            QStringLiteral("-log.") + QString::number(i) +
            QStringLiteral(".txt");

        if (QFile::exists(previousLogFilePath)) {
            ++m_currentOldLogFilesCount;
        }
    }
}

QuentierFileLogWriter::~QuentierFileLogWriter()
{
    Q_UNUSED(m_logFile.flush())
    m_logFile.close();
}

void QuentierFileLogWriter::write(QString message)
{
    DateTimePrint::Options options(
        DateTimePrint::IncludeMilliseconds | DateTimePrint::IncludeTimezone);

    message.prepend(
        printableDateTimeFromTimestamp(
            QDateTime::currentMSecsSinceEpoch(), options) +
        QStringLiteral(" "));

    qint64 messageSize = message.toUtf8().size();
    m_currentLogFileSize += messageSize;

    if (Q_UNLIKELY(m_currentLogFileSize > m_maxSizeBytes)) {
        rotate();
    }

    if (Q_UNLIKELY(!m_pStream)) {
        m_pStream.reset(new QTextStream);
        m_pStream->setDevice(&m_logFile);
        m_pStream->setCodec(QTextCodec::codecForName("UTF-8"));
    }

    *m_pStream << message << QStringLiteral("\n");
    m_pStream->flush();
}

void QuentierFileLogWriter::restartLogging()
{
    if (m_pStream) {
        m_pStream->flush();
        m_pStream->setDevice(nullptr);
    }

    m_logFile.close();

    QFileInfo logFileInfo(m_logFile);
    QString logFilePath = logFileInfo.absoluteFilePath();
    bool res = QFile::remove(logFilePath);
    if (Q_UNLIKELY(!res)) {
        std::cerr << "Can't restart logging: failed to remove the existing "
                  << "log file: " << qPrintable(logFilePath) << "\n";
    }
    else {
        m_logFile.setFileName(logFilePath);
        bool opened = m_logFile.open(
            QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered |
            QIODevice::Text);
        if (Q_UNLIKELY(!opened)) {
            std::cerr << "Can't open the new libquentier log file, error: "
                      << qPrintable(m_logFile.errorString()) << " (error code "
                      << qPrintable(QString::number(m_logFile.error()))
                      << ")\n";
            return;
        }
    }

    m_currentLogFileSize = m_logFile.size();

    if (m_pStream) {
        m_pStream->setDevice(&m_logFile);
        m_pStream->setCodec(QTextCodec::codecForName("UTF-8"));
    }
}

void QuentierFileLogWriter::rotate()
{
    QString logFileDirPath = QFileInfo(m_logFile).absolutePath();

    // 1) Rename all existing old log files
    for (int i = m_currentOldLogFilesCount; i >= 1; --i) {
        const QString previousLogFilePath = logFileDirPath +
            QStringLiteral("/") + QCoreApplication::applicationName() +
            QStringLiteral("-log.") + QString::number(i) +
            QStringLiteral(".txt");

        QFile previousLogFile(previousLogFilePath);
        if (Q_UNLIKELY(!previousLogFile.exists())) {
            continue;
        }

        const QString newLogFilePath = logFileDirPath + QStringLiteral("/") +
            QCoreApplication::applicationName() + QStringLiteral("-log.") +
            QString::number(i + 1) + QStringLiteral(".txt");

        // Just-in-case check, shouldn't really do anything in normal
        // circumstances
        Q_UNUSED(QFile::remove(newLogFilePath))

        bool res = previousLogFile.rename(newLogFilePath);
        if (Q_UNLIKELY(!res)) {
            std::cerr << "Can't rename one of previous libquentier log files "
                      << "for log file rotation: attempted to rename from "
                      << qPrintable(previousLogFilePath) << " to "
                      << qPrintable(newLogFilePath) << ", error: "
                      << qPrintable(previousLogFile.errorString())
                      << " (error code "
                      << qPrintable(QString::number(previousLogFile.error()))
                      << ")\n";
        }
    }

    // 2) Rename the current log file
    if (m_pStream) {
        m_pStream->setDevice(nullptr);
    }
    m_logFile.close();

    bool res = m_logFile.rename(
        logFileDirPath + QStringLiteral("/") +
        QCoreApplication::applicationName() + QStringLiteral("-log.1.txt"));

    if (Q_UNLIKELY(!res)) {
        std::cerr
            << "Can't rename the current libquentier log file for log file "
            << "rotation, error: " << qPrintable(m_logFile.errorString())
            << " (error code " << qPrintable(QString::number(m_logFile.error()))
            << ")\n";
        return;
    }

    // 3) Open the new file
    m_logFile.setFileName(
        logFileDirPath + QStringLiteral("/") +
        QCoreApplication::applicationName() + QStringLiteral("-log.txt"));

    bool opened = m_logFile.open(
        QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered |
        QIODevice::Text);
    if (Q_UNLIKELY(!opened)) {
        std::cerr << "Can't open the renamed/rotated libquentier log file, "
                  << "error: " << qPrintable(m_logFile.errorString())
                  << " (error code "
                  << qPrintable(QString::number(m_logFile.error())) << ")\n";
        return;
    }

    m_currentLogFileSize = m_logFile.size();

    if (m_pStream) {
        m_pStream->setDevice(&m_logFile);
        m_pStream->setCodec(QTextCodec::codecForName("UTF-8"));
    }

    // 4) Increase the current count of old log files
    ++m_currentOldLogFilesCount;

    if (Q_LIKELY(m_currentOldLogFilesCount < m_maxOldLogFilesCount)) {
        return;
    }

    // 5) If got here, there are too many old log files, need to remove
    // the oldest one
    QString oldestLogFilePath = logFileDirPath + QStringLiteral("/") +
        QCoreApplication::applicationName() + QStringLiteral("-log.") +
        QString::number(m_currentOldLogFilesCount) + QStringLiteral(".txt");

    res = QFile::remove(oldestLogFilePath);
    if (Q_UNLIKELY(!res)) {
        std::cerr << "Can't remove the oldest previous libquentier log file: "
                  << qPrintable(oldestLogFilePath) << "\n";
        return;
    }

    // 6) Decrement the current count of old log files
    --m_currentOldLogFilesCount;
}

QuentierConsoleLogWriter::QuentierConsoleLogWriter(QObject * parent) :
    IQuentierLogWriter(parent)
{}

void QuentierConsoleLogWriter::write(QString message)
{
#if defined Q_OS_WIN
    OutputDebugStringW(reinterpret_cast<const WCHAR *>(message.utf16()));
    OutputDebugStringW(L"\n");
#else
    fprintf(stderr, "%s\n", qPrintable(message));
    fflush(stderr);
#endif
}

QuentierLogger & QuentierLogger::instance()
{
    // NOTE: since C++11 static construction is thread-safe
    static QuentierLogger instance;
    return instance;
}

QuentierLogger::QuentierLogger(QObject * parent) :
    QObject(parent), m_pImpl(nullptr)
{
    if (!m_pImpl) {
        m_pImpl = new QuentierLoggerImpl(this);
        addLogWriter(new QuentierFileLogWriter(
            MaxSizeBytes(104857600), MaxOldLogFilesCount(5)));
    }
}

QString QuentierLogger::logFilesDirPath()
{
    return applicationPersistentStoragePath() +
        QStringLiteral("/logs-quentier");
}

void QuentierLogger::addLogWriter(IQuentierLogWriter * pLogWriter)
{
    if (Q_UNLIKELY(!pLogWriter)) {
        return;
    }

    for (auto & pLogExistingWriter: m_pImpl->m_logWriterPtrs) {
        if (Q_UNLIKELY(pLogExistingWriter == pLogWriter)) {
            return;
        }
    }

    m_pImpl->m_logWriterPtrs << QPointer<IQuentierLogWriter>(pLogWriter);

    QObject::connect(
        this, &QuentierLogger::sendLogMessage, pLogWriter,
        &IQuentierLogWriter::write, Qt::QueuedConnection);

    auto * pFileLogWriter = qobject_cast<QuentierFileLogWriter *>(pLogWriter);
    if (pFileLogWriter) {
        QObject::connect(
            this, &QuentierLogger::sendRestartLoggingRequest, pFileLogWriter,
            &QuentierFileLogWriter::restartLogging, Qt::QueuedConnection);
    }

    pLogWriter->setParent(nullptr);
    pLogWriter->moveToThread(m_pImpl->m_pLogWriteThread);
}

void QuentierLogger::removeLogWriter(IQuentierLogWriter * pLogWriter)
{
    if (Q_UNLIKELY(!pLogWriter)) {
        return;
    }

    bool found = false;
    for (auto it = m_pImpl->m_logWriterPtrs.begin(),
              end = m_pImpl->m_logWriterPtrs.end();
         it != end; ++it)
    {
        if (Q_LIKELY(it->data() != pLogWriter)) {
            continue;
        }

        found = true;
        Q_UNUSED(m_pImpl->m_logWriterPtrs.erase(it));
        break;
    }

    if (Q_UNLIKELY(!found)) {
        return;
    }

    QObject::disconnect(
        this, &QuentierLogger::sendLogMessage, pLogWriter,
        &IQuentierLogWriter::write);

    pLogWriter->moveToThread(thread());
    pLogWriter->deleteLater();
}

void QuentierLogger::write(QString message)
{
    Q_EMIT sendLogMessage(message);
}

void QuentierLogger::setMinLogLevel(const LogLevel minLogLevel)
{
    Q_UNUSED(m_pImpl->m_minLogLevel.fetchAndStoreOrdered(
        static_cast<int>(minLogLevel)))
}

QRegularExpression QuentierLogger::componentFilterRegex()
{
    QReadLocker lock(&m_pImpl->m_componentFilterLock);
    return m_pImpl->m_componentFilterRegex;
}

void QuentierLogger::setComponentFilterRegex(const QRegularExpression & filter)
{
    QWriteLocker lock(&m_pImpl->m_componentFilterLock);
    m_pImpl->m_componentFilterRegex = filter;
}

void QuentierLogger::restartLogging()
{
    Q_EMIT sendRestartLoggingRequest();
}

LogLevel QuentierLogger::minLogLevel() const
{
    return static_cast<LogLevel>(m_pImpl->m_minLogLevel.loadAcquire());
}

QuentierLoggerImpl::QuentierLoggerImpl(QObject * parent) :
    QObject(parent), m_logWriterPtrs(),
    m_minLogLevel(static_cast<int>(LogLevel::Info)),
    m_pLogWriteThread(new QThread)
{
    QObject::connect(
        m_pLogWriteThread, &QThread::finished, m_pLogWriteThread,
        &QThread::deleteLater);

    QObject::connect(
        this, &QuentierLoggerImpl::destroyed, m_pLogWriteThread,
        &QThread::quit);

    m_pLogWriteThread->setObjectName(
        QStringLiteral("Libquentier-logger-thread"));

    m_pLogWriteThread->start(QThread::LowPriority);
}

} // namespace quentier
