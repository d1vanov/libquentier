#ifndef LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_PRIVATE_H
#define LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_PRIVATE_H

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Macros.h>
#include <QObject>
#include <QString>
#include <QFile>
#include <QVector>
#include <QPointer>
#include <QTextStream>
#include <QThread>
#include <QAtomicInt>

#if __cplusplus < 201103L
#include <QMutex>
#endif

namespace quentier {

/**
 * @brief The IQuentierLogWriter class is the interface for any class willing to implement a log writer.
 *
 * Typically a particular log writer writes the log messages to some particular logging destination,
 * like file or stderr or just something which can serve as a logging destination
 */
class Q_DECL_HIDDEN IQuentierLogWriter: public QObject
{
    Q_OBJECT
public:
    IQuentierLogWriter(QObject * parent = Q_NULLPTR) :
        QObject(parent)
    {}

public Q_SLOTS:
    virtual void write(QString message) = 0;
};

/**
 * Type-safe max allowed size of a log file in bytes
 */
class Q_DECL_HIDDEN MaxSizeBytes
{
public:
    MaxSizeBytes(const qint64 size) :
        m_size(size)
    {}

    qint64 size() const { return m_size; }

private:
    qint64      m_size;
};

/**
 * Type-safe max number of old log files to keep around
 */
class Q_DECL_HIDDEN MaxOldLogFilesCount
{
public:
    MaxOldLogFilesCount(const int count) :
        m_count(count)
    {}

    int count() const { return m_count; }

private:
    int     m_count;
};

/**
 * @brief The QuentierFileLogWriter class implements the log writer to a log file destination
 *
 * It features the automatic rotation of the log file by its max size and ensures not more than just a handful
 * of previous log files are stored around
 */
class Q_DECL_HIDDEN QuentierFileLogWriter: public IQuentierLogWriter
{
    Q_OBJECT
public:
    explicit QuentierFileLogWriter(const MaxSizeBytes & maxSizeBytes, const MaxOldLogFilesCount & maxOldLogFilesCount,
                                   QObject * parent = Q_NULLPTR);
    ~QuentierFileLogWriter();

public Q_SLOTS:
    virtual void write(QString message) Q_DECL_OVERRIDE;
    void restartLogging();

private:
    void rotate();

private:
    QFile       m_logFile;
    QTextStream m_stream;

    qint64      m_maxSizeBytes;
    int         m_maxOldLogFilesCount;

    qint64      m_currentLogFileSize;
    int         m_currentOldLogFilesCount;
};

class Q_DECL_HIDDEN QuentierConsoleLogWriter: public IQuentierLogWriter
{
    Q_OBJECT
public:
    explicit QuentierConsoleLogWriter(QObject * parent = Q_NULLPTR);

public Q_SLOTS:
    virtual void write(QString message) Q_DECL_OVERRIDE;
};

QT_FORWARD_DECLARE_CLASS(QuentierLoggerImpl)

class Q_DECL_HIDDEN QuentierLogger: public QObject
{
    Q_OBJECT
public:
    static QuentierLogger & instance();

    static QString logFilesDirPath();

    void addLogWriter(IQuentierLogWriter * pWriter);
    void removeLogWriter(IQuentierLogWriter * pWriter);

    void write(QString message);

    LogLevel::type minLogLevel() const;
    void setMinLogLevel(const LogLevel::type minLogLevel);

    void restartLogging();

Q_SIGNALS:
    void sendLogMessage(QString message);
    void sendRestartLoggingRequest();

private:
    QuentierLogger(QObject * parent = Q_NULLPTR);
    Q_DISABLE_COPY(QuentierLogger)

private:
    QuentierLoggerImpl * m_pImpl;

#if __cplusplus < 201103L
    QMutex  m_constructionMutex;
#endif
};

class Q_DECL_HIDDEN QuentierLoggerImpl: public QObject
{
    Q_OBJECT
public:
    QuentierLoggerImpl(QObject * parent = Q_NULLPTR);

    QVector<QPointer<IQuentierLogWriter> >  m_logWriterPtrs;
    QAtomicInt          m_minLogLevel;
    QThread *           m_pLogWriteThread;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOGGING_QUENTIER_LOGGER_PRIVATE_H
