/*
 * Copyright 2018-2025 Dmitry Ivanov
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

#include "FileCopier.h"

#include <quentier/logging/QuentierLogger.h>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>

namespace quentier::utility {

namespace {

template <typename T>
void printState(T & t, const FileCopier::State state)
{
    switch (state) {
    case FileCopier::State::Idle:
        t << "Idle";
        break;
    case FileCopier::State::Copying:
        t << "Copying";
        break;
    case FileCopier::State::Cancelling:
        t << "Cancelling";
        break;
    default:
        t << "Unknown (" << static_cast<qint64>(state) << ")";
        break;
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

FileCopier::FileCopier(QObject * parent) : QObject{parent} {}

FileCopier::State FileCopier::state() const
{
    if (m_idle) {
        return State::Idle;
    }

    if (m_cancelled) {
        return State::Cancelling;
    }

    return State::Copying;
}

QString FileCopier::sourceFilePath() const
{
    return m_sourcePath;
}

QString FileCopier::destinationFilePath() const
{
    return m_destPath;
}

double FileCopier::currentProgress() const
{
    return m_currentProgress;
}

void FileCopier::copyFile(QString sourcePath, QString destPath) // NOLINT
{
    QNDEBUG(
        "utility::FileCopier",
        "FileCopierPrivate::copyFile: source path = "
            << sourcePath << ", dest path = " << destPath);

    if (m_sourcePath == sourcePath && m_destPath == destPath) {
        QNDEBUG("utility::FileCopier", "Paths haven't changed, nothing to do");
        return;
    }

    m_sourcePath = sourcePath;
    m_destPath = destPath;
    m_idle = false;
    m_cancelled = false;
    m_currentProgress = 0.0;

    QFile fromFile(sourcePath);
    if (!fromFile.open(QIODevice::ReadOnly)) {
        ErrorString error{
            QT_TR_NOOP("Can't copy file, failed to open the source "
                       "file for writing")};

        error.details() = QDir::toNativeSeparators(sourcePath);
        QNWARNING("utility::FileCopier", error);
        clear();

        Q_EMIT notifyError(error);
        return;
    }

    QFile toFile(destPath);
    if (!toFile.open(QIODevice::WriteOnly)) {
        ErrorString error{
            QT_TR_NOOP("Can't copy file, failed to open "
                       "the destination file for writing")};

        error.details() = QDir::toNativeSeparators(destPath);
        QNWARNING("utility::FileCopier", error);
        clear();

        Q_EMIT notifyError(error);
        return;
    }

    const qint64 fromFileSize = fromFile.size();
    if (fromFileSize > 0) {
        const int bufLen = 4194304; // 4 Mb in bytes
        QByteArray buf;
        buf.reserve(bufLen);

        qint64 totalBytesWritten = 0;

        while (true) {
            // Allow potential pending cancellation to get in
            QCoreApplication::processEvents();

            // Check if cancellation is pending first
            if (m_cancelled) {
                QNDEBUG(
                    "utility::FileCopier", "File copying has been canceled");
                clear();
                Q_EMIT cancelled(sourcePath, destPath);
                return;
            }

            const qint64 bytesRead = fromFile.read(buf.data(), bufLen);
            if (Q_UNLIKELY(bytesRead <= 0)) {
                ErrorString error{
                    QT_TR_NOOP("Can't copy file, failed to read data "
                               "from the source file")};

                error.details() = sourcePath;
                QNWARNING("utility::FileCopier", error);
                clear();

                Q_EMIT notifyError(error);
                return;
            }

            const qint64 bytesWritten = toFile.write(
                buf.constData(), std::min(static_cast<int>(bytesRead), bufLen));

            if (Q_UNLIKELY(bytesWritten < 0)) {
                ErrorString error{
                    QT_TR_NOOP("Can't copy file, failed to write data "
                               "to the destination file")};

                error.details() = destPath;
                QNWARNING("utility::FileCopier", error);
                clear();

                Q_EMIT notifyError(error);
                return;
            }

            totalBytesWritten += bytesWritten;

            m_currentProgress = static_cast<double>(totalBytesWritten) /
                static_cast<double>(fromFileSize);

            QNTRACE(
                "utility::FileCopier",
                "File copying progress update: progress = "
                    << m_currentProgress
                    << ", total bytes written = " << totalBytesWritten
                    << ", source file size = " << fromFileSize
                    << ", source path = " << sourcePath
                    << ", dest path = " << destPath);

            Q_EMIT progressUpdate(m_currentProgress);

            if (totalBytesWritten >= fromFileSize) {
                break;
            }
        }
    }
    else {
        // Write a single byte to the file to ensure it would be created,
        // afterwards the file would be resized to zero anyways
        toFile.write(QByteArray::fromRawData("0", 1));
        toFile.close();
        toFile.resize(0);

        m_currentProgress = 1.0;
        Q_EMIT progressUpdate(m_currentProgress);
    }

    QNDEBUG(
        "utility::FileCopier",
        "File copying is complete: source path = "
            << sourcePath << ", dest path = " << destPath);

    clear();
    Q_EMIT finished(sourcePath, destPath);
}

void FileCopier::cancel()
{
    QNDEBUG("utility::FileCopier", "FileCopierPrivate::cancel");

    if (m_idle) {
        QNDEBUG("utility::FileCopier", "Idle, nothing to cancel");
        return;
    }

    m_cancelled = true;
}

void FileCopier::clear()
{
    QNDEBUG("utility::FileCopier", "FileCopierPrivate::clear");

    m_sourcePath.clear();
    m_destPath.clear();
    m_idle = true;
    m_cancelled = false;
    m_currentProgress = 0.0;
}

////////////////////////////////////////////////////////////////////////////////

QDebug & operator<<(QDebug & dbg, const FileCopier::State state)
{
    printState(dbg, state);
    return dbg;
}

QTextStream & operator<<(QTextStream & strm, const FileCopier::State state)
{
    printState(strm, state);
    return strm;
}

} // namespace quentier::utility
