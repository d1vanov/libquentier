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

#ifndef LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_PRIVATE_H
#define LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_PRIVATE_H

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
class Q_DECL_HIDDEN IQuentierLogWriter : public QObject
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
class Q_DECL_HIDDEN MaxSizeBytes
{
public:
    MaxSizeBytes(const qint64 size) : m_size(size) {}

    qint64 size() const
    {
        return m_size;
    }

private:
    qint64 m_size;
};

/**
 * Type-safe max number of old log files to keep around
 */
class Q_DECL_HIDDEN MaxOldLogFilesCount
{
public:
    MaxOldLogFilesCount(const int count) : m_count(count) {}

    int count() const
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
class Q_DECL_HIDDEN QuentierFileLogWriter final : public IQuentierLogWriter
{
    Q_OBJECT
public:
    explicit QuentierFileLogWriter(
        const MaxSizeBytes & maxSizeBytes,
        const MaxOldLogFilesCount & maxOldLogFilesCount,
        QObject * parent = nullptr);

    virtual ~QuentierFileLogWriter() override;

public Q_SLOTS:
    virtual void write(QString message) override;
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

class Q_DECL_HIDDEN QuentierConsoleLogWriter final : public IQuentierLogWriter
{
    Q_OBJECT
public:
    explicit QuentierConsoleLogWriter(QObject * parent = nullptr);

public Q_SLOTS:
    virtual void write(QString message) override;
};

QT_FORWARD_DECLARE_CLASS(QuentierLoggerImpl)

class Q_DECL_HIDDEN QuentierLogger final : public QObject
{
    Q_OBJECT
public:
    static QuentierLogger & instance();

    static QString logFilesDirPath();

    void addLogWriter(IQuentierLogWriter * pWriter);
    void removeLogWriter(IQuentierLogWriter * pWriter);

    void write(QString message);

    LogLevel minLogLevel() const;
    void setMinLogLevel(const LogLevel minLogLevel);

    QRegularExpression componentFilterRegex();
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

class Q_DECL_HIDDEN QuentierLoggerImpl final : public QObject
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

#endif // LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_PRIVATE_H
