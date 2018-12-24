/*
 * Copyright 2018 Dmitry Ivanov
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

#include "FileCopier_p.h"
#include <quentier/logging/QuentierLogger.h>
#include <QByteArray>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <algorithm>

namespace quentier {

FileCopierPrivate::FileCopierPrivate(QObject * parent) :
    QObject(parent),
    m_sourcePath(),
    m_destPath(),
    m_idle(true),
    m_cancelled(false),
    m_currentProgress(0.0)
{}

void FileCopierPrivate::copyFile(const QString & sourcePath, const QString & destPath)
{
    QNDEBUG(QStringLiteral("FileCopierPrivate::copyFile: source path = ") << sourcePath
            << QStringLiteral(", dest path = ") << destPath);

    if ((m_sourcePath == sourcePath) && (m_destPath == destPath)) {
        QNDEBUG(QStringLiteral("Paths haven't changed, nothing to do"));
        return;
    }

    m_sourcePath = sourcePath;
    m_destPath = destPath;
    m_idle = false;
    m_cancelled = false;
    m_currentProgress = 0.0;

    QFile fromFile(sourcePath);
    if (!fromFile.open(QIODevice::ReadOnly))
    {
        ErrorString error(QT_TR_NOOP("Can't copy file, failed to open source file for writing"));
        error.details() = QDir::toNativeSeparators(sourcePath);
        clear();

        Q_EMIT notifyError(error);
        return;
    }

    qint64 fromFileSize = fromFile.size();
    if (Q_UNLIKELY(fromFileSize <= 0))
    {
        ErrorString error(QT_TR_NOOP("Can't copy file, source file is empty"));
        error.details() = QDir::toNativeSeparators(sourcePath);
        clear();

        Q_EMIT notifyError(error);
        return;
    }

    QFile toFile(destPath);
    if (!toFile.open(QIODevice::WriteOnly))
    {
        ErrorString error(QT_TR_NOOP("Can't copy file, failed to open destination file for writing"));
        error.details() = QDir::toNativeSeparators(destPath);
        clear();

        Q_EMIT notifyError(error);
        return;
    }

    int bufLen = 4194304;   // 4 Mb in bytes
    QByteArray buf;
    buf.reserve(bufLen);

    qint64 totalBytesWritten = 0;

    while(true)
    {
        // Allow potential pending cancellation to get in
        QCoreApplication::processEvents();

        // Check if cancellation is pending first
        if (m_cancelled) {
            clear();
            Q_EMIT cancelled(sourcePath, destPath);
            return;
        }

        qint64 bytesRead = fromFile.read(buf.data(), bufLen);
        if (Q_UNLIKELY(bytesRead <= 0))
        {
            ErrorString error(QT_TR_NOOP("Can't copy file, failed to read data from source file"));
            error.details() = sourcePath;
            clear();

            Q_EMIT notifyError(error);
            return;
        }

        qint64 bytesWritten = toFile.write(buf.constData(), std::min(static_cast<int>(bytesRead), bufLen));
        if (Q_UNLIKELY(bytesWritten < 0))
        {
            ErrorString error(QT_TR_NOOP("Can't copy file, failed to write data to destination file"));
            error.details() = destPath;
            clear();

            Q_EMIT notifyError(error);
            return;
        }

        totalBytesWritten += bytesWritten;
        m_currentProgress = static_cast<double>(totalBytesWritten) / fromFileSize;

        QNTRACE(QStringLiteral("File copying progress update: progress = ") << m_currentProgress
                << QStringLiteral(", total bytes written = ") << totalBytesWritten
                << QStringLiteral(", source file size = ") << fromFileSize
                << QStringLiteral(", source path = ") << sourcePath
                << QStringLiteral(", dest path = ") << destPath);

        Q_EMIT progressUpdate(m_currentProgress);

        if (totalBytesWritten >= fromFileSize) {
            break;
        }
    }

    QNDEBUG(QStringLiteral("File copying is complete: source path = ") << sourcePath
            << QStringLiteral(", dest path = ") << destPath);

    clear();
    Q_EMIT finished(sourcePath, destPath);
}

void FileCopierPrivate::cancel()
{
    QNDEBUG(QStringLiteral("FileCopierPrivate::cancel"));

    if (m_idle) {
        QNDEBUG(QStringLiteral("Idle, nothing to cancel"));
        return;
    }

    m_cancelled = true;
}

void FileCopierPrivate::clear()
{
    QNDEBUG(QStringLiteral("FileCopierPrivate::clear"));

    m_sourcePath.clear();
    m_destPath.clear();
    m_idle = true;
    m_cancelled = false;
    m_currentProgress = 0.0;
}

} // namespace quentier
