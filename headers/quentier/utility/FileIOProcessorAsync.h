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

#ifndef LIB_QUENTIER_UTILITY_FILE_IO_PROCESSOR_ASYNC_H
#define LIB_QUENTIER_UTILITY_FILE_IO_PROCESSOR_ASYNC_H

#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>

#include <QByteArray>
#include <QIODevice>
#include <QObject>
#include <QString>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(FileIOProcessorAsyncPrivate)

/**
 * @brief The FileIOProcessorAsync class is a wrapper under simple file IO
 * operations, it is meant to be used for simple asynchronous IO
 */
class QUENTIER_EXPORT FileIOProcessorAsync : public QObject
{
    Q_OBJECT
public:
    explicit FileIOProcessorAsync(QObject * parent = nullptr);

    /**
     * @brief setIdleTimePeriod sets time period defining the idle state of
     * FileIOProcessorAsync: once the time measured since the last IO operation
     * is over the specified number of seconds, the class emits readyForIO
     * signal to any interested listeners of this event.  If this method is not
     * called ever, the default idle time period would be 30 seconds.
     *
     * @param seconds               Number of seconds for idle time period
     */
    void setIdleTimePeriod(qint32 seconds);

Q_SIGNALS:
    /**
     * @brief readyForIO signal is emitted when the queue for file IO
     * is empty for some time (30 seconds by default, can also be configured
     * via setIdleTimePeriod method) after the last IO event to signal
     * listeners that they can perform some IO via the FileIOProcessorAsync
     */
    void readyForIO();

    /**
     * @brief writeFileRequestProcessed signal is emitted when the file write
     * request with given id is finished
     *
     * @param success                   True if write operation was successful,
     *                                  false otherwise
     * @param errorDescription          Textual description of the error
     * @param requestId                 Unique identifier of the file write
     *                                  request
     */
    void writeFileRequestProcessed(
        bool success, ErrorString errorDescription, QUuid requestId);

    /**
     * @brief readFileRequestProcessed signal is emitted when the file read
     * request with given id is finished
     *
     * @param success                   True if read operation was successful,
     *                                  false otherwise
     * @param errorDescription          Textual description of the error
     * @param data                      Data read from file
     * @param requestId                 Unique identifier of the file read
     *                                  request
     */
    void readFileRequestProcessed(
        bool success, ErrorString errorDescription, QByteArray data,
        QUuid requestId);

public Q_SLOTS:
    /**
     * @brief onWriteFileRequest slot processes file write requests
     * with given request ids
     *
     * @param absoluteFilePath      Absolute file path to be written
     * @param data                  Data to be written to the file
     * @param requestId             Unique identifier of the file write request
     * @param append                If true, the data would be appended to file,
     *                              otherwise the entire file would be erased
     *                              before with the data is written
     */
    void onWriteFileRequest(
        QString absoluteFilePath, QByteArray data, QUuid requestId,
        bool append);

    /**
     * @brief onReadFileRequest slot processes file read requests
     * with given request ids
     *
     * @param absoluteFilePath      Absolute file path to be read
     * @param requestId             Unique identifier of the file read request
     */
    void onReadFileRequest(QString absoluteFilePath, QUuid requestId);

private:
    FileIOProcessorAsyncPrivate * const d_ptr;
    Q_DECLARE_PRIVATE(FileIOProcessorAsync)
};

} // namespace quentier

#endif // LIB_QUENTIER_UTILITY_FILE_IO_PROCESSOR_ASYNC_H
