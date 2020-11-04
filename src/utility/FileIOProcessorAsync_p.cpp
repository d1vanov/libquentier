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

#include "FileIOProcessorAsync_p.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/DateTime.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimerEvent>

namespace quentier {

FileIOProcessorAsyncPrivate::FileIOProcessorAsyncPrivate(QObject * parent) :
    QObject(parent)
{}

void FileIOProcessorAsyncPrivate::setIdleTimePeriod(const qint32 seconds)
{
    QNDEBUG(
        "utility:file_async",
        "FileIOProcessorAsyncPrivate::setIdleTimePeriod: seconds = "
            << seconds);

    m_idleTimePeriodSeconds = seconds;
}

#define RESTART_TIMER()                                                        \
    if (m_postOperationTimerId != 0) {                                         \
        killTimer(m_postOperationTimerId);                                     \
    }                                                                          \
    m_postOperationTimerId =                                                   \
        startTimer(secondsToMilliseconds(m_idleTimePeriodSeconds));            \
    QNTRACE(                                                                   \
        "utility:file_async",                                                  \
        "FileIOProcessorAsyncPrivate: started post "                           \
            << "operation timer with id " << m_postOperationTimerId)

void FileIOProcessorAsyncPrivate::onWriteFileRequest(
    QString absoluteFilePath, QByteArray data, QUuid requestId, bool append)
{
    QNDEBUG(
        "utility:file_async",
        "FileIOProcessorAsyncPrivate::onWriteFileRequest: file path = "
            << absoluteFilePath << ", request id = " << requestId
            << ", append = " << (append ? "true" : "false"));

    QFileInfo fileInfo(absoluteFilePath);
    QDir folder = fileInfo.absoluteDir();
    if (!folder.exists()) {
        bool madeFolder = folder.mkpath(folder.absolutePath());
        if (!madeFolder) {
            ErrorString error(
                QT_TR_NOOP("can't create folder to write file into"));

            error.details() = absoluteFilePath;
            QNWARNING("utility:file_async", error);
            Q_EMIT writeFileRequestProcessed(false, error, requestId);
            RESTART_TIMER();
            return;
        }
    }

    QFile file(absoluteFilePath);

    QIODevice::OpenMode mode;
    if (append) {
        mode = QIODevice::Append;
    }
    else {
        mode = QIODevice::WriteOnly;
    }

    bool open = file.open(mode);
    if (Q_UNLIKELY(!open)) {
        ErrorString error(QT_TR_NOOP("can't open file for writing/appending"));
        error.details() = absoluteFilePath;
        QNWARNING("utility:file_async", error);
        Q_EMIT writeFileRequestProcessed(false, error, requestId);
        RESTART_TIMER();
        return;
    }

    qint64 writtenBytes = file.write(data);
    if (Q_UNLIKELY(writtenBytes < data.size())) {
        ErrorString error(QT_TR_NOOP("can't write the whole data to file"));
        error.details() = absoluteFilePath;
        QNWARNING("utility:file_async", error);
        Q_EMIT writeFileRequestProcessed(false, error, requestId);
        RESTART_TIMER();
        return;
    }

    file.close();

    QNDEBUG(
        "utility:file_async", "Successfully wrote file " << absoluteFilePath);

    Q_EMIT writeFileRequestProcessed(true, ErrorString(), requestId);
    RESTART_TIMER();
}

void FileIOProcessorAsyncPrivate::onReadFileRequest(
    QString absoluteFilePath, QUuid requestId)
{
    QNDEBUG(
        "utility:file_async",
        "FileIOProcessorAsyncPrivate::onReadFileRequest: file path = "
            << absoluteFilePath << ", request id = " << requestId);

    QFile file(absoluteFilePath);
    if (!file.exists()) {
        QNTRACE(
            "utility:file_async",
            "The file to read does not exist, "
                << "sending empty data in return");

        Q_EMIT readFileRequestProcessed(
            true, ErrorString(), QByteArray(), requestId);

        RESTART_TIMER();
        return;
    }

    bool open = file.open(QIODevice::ReadOnly);
    if (!open) {
        ErrorString error(QT_TR_NOOP("can't open file for reading"));
        error.details() = absoluteFilePath;
        QNDEBUG("utility:file_async", error);
        Q_EMIT readFileRequestProcessed(false, error, QByteArray(), requestId);
        RESTART_TIMER();
        return;
    }

    QByteArray data = file.readAll();
    Q_EMIT readFileRequestProcessed(true, ErrorString(), data, requestId);
    RESTART_TIMER();
}

void FileIOProcessorAsyncPrivate::timerEvent(QTimerEvent * pEvent)
{
    if (!pEvent) {
        QNWARNING(
            "utility:file_async",
            "Detected null pointer to QTimerEvent "
                << "in FileIOProcessorAsyncPrivate");
        return;
    }

    qint32 timerId = pEvent->timerId();

    if (timerId != m_postOperationTimerId) {
        QNTRACE(
            "utility:file_async",
            "Received unidentified timer event for "
                << "FileIOProcessorAsyncPrivate");
        return;
    }

    killTimer(timerId);
    m_postOperationTimerId = 0;

    Q_EMIT readyForIO();
}

} // namespace quentier
