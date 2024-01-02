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

#include <quentier/logging/QuentierLogger.h>

#include <QAtomicInt>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>
#include <QThread>
#include <QVector>

#include <memory>

namespace quentier {

/**
 * @brief The IQuentierLogWriter class is the interface for any class willing to
 * implement a log writer.
 *
 * Typically a particular log writer writes the log messages to some particular
 * logging destination, like file or stderr or just something which can serve as
 * a logging destination
 */
class IQuentierLogWriter : public QObject
{
    Q_OBJECT
public:
    IQuentierLogWriter(QObject * parent = nullptr) : QObject(parent) {}

public Q_SLOTS:
    virtual void write(QString message) = 0;
};

/**
 * Type-safe max allowed size of a log file in bytes
 */
class MaxSizeBytes
{
public:
    MaxSizeBytes(const qint64 size) : m_size(size) {}

    [[nodiscard]] qint64 size() const noexcept
    {
        return m_size;
    }

private:
    qint64 m_size;
};

/**
 * Type-safe max number of old log files to keep around
 */
class MaxOldLogFilesCount
{
public:
    MaxOldLogFilesCount(const int count) : m_count(count) {}

    [[nodiscard]] int count() const noexcept
    {
        return m_count;
    }

private:
    int m_count;
};

/**
 * @brief The QuentierFileLogWriter class implements the log writer to a log
 * file destination
 *
 * It features the automatic rotation of the log file by its max size and
 * ensures not more than just a handful of previous log files are stored around
 */
class QuentierFileLogWriter final : public IQuentierLogWriter
{
    Q_OBJECT
public:
    explicit QuentierFileLogWriter(
        const MaxSizeBytes & maxSizeBytes,
        const MaxOldLogFilesCount & maxOldLogFilesCount,
        QObject * parent = nullptr);

    ~QuentierFileLogWriter() noexcept override;

public Q_SLOTS:
    void write(QString message) override;
    void restartLogging();

private:
    void rotate();

private:
    QFile m_logFile;
    std::unique_ptr<QTextStream> m_pStream;

    qint64 m_maxSizeBytes;
    int m_maxOldLogFilesCount;

    qint64 m_currentLogFileSize = 0;
    int m_currentOldLogFilesCount = 0;
};

class QuentierConsoleLogWriter final : public IQuentierLogWriter
{
    Q_OBJECT
public:
    explicit QuentierConsoleLogWriter(QObject * parent = nullptr);

public Q_SLOTS:
    void write(QString message) override;
};

class QuentierLoggerImpl;

class QuentierLogger final : public QObject
{
    Q_OBJECT
public:
    static QuentierLogger & instance();

    [[nodiscard]] static QString logFilesDirPath();

    void addLogWriter(IQuentierLogWriter * pLogWriter);
    void removeLogWriter(IQuentierLogWriter * pLogWriter);

    void write(QString message);

    [[nodiscard]] LogLevel minLogLevel() const;
    void setMinLogLevel(LogLevel minLogLevel);

    [[nodiscard]] QRegularExpression componentFilterRegex();
    void setComponentFilterRegex(const QRegularExpression & filter);

    void restartLogging();

Q_SIGNALS:
    void sendLogMessage(QString message);
    void sendRestartLoggingRequest();

private:
    QuentierLogger(QObject * parent = nullptr);
    Q_DISABLE_COPY(QuentierLogger)

private:
    QuentierLoggerImpl * m_pImpl;
};

class QuentierLoggerImpl final : public QObject
{
    Q_OBJECT
public:
    QuentierLoggerImpl(QObject * parent = nullptr);

    QVector<QPointer<IQuentierLogWriter>> m_logWriterPtrs;
    QAtomicInt m_minLogLevel;
    QThread * m_pLogWriteThread;

    QReadWriteLock m_componentFilterLock;
    QRegularExpression m_componentFilterRegex;
};

} // namespace quentier
