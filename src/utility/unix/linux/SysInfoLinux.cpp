/*
 * Copyright 2016-2025 Dmitry Ivanov
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

#include <quentier/utility/SysInfo.h>

#include "../../SysInfo_p.h"
#include "StackTrace.h"

#include <sys/sysinfo.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QString>

namespace quentier::utility {

qint64 SysInfo::totalMemory()
{
    Q_D(SysInfo);
    const QMutexLocker mutexLocker(&d->m_mutex);

    struct sysinfo si;
    const int rc = sysinfo(&si);
    if (rc) {
        return -1;
    }

    return static_cast<qint64>(si.totalram);
}

qint64 SysInfo::freeMemory()
{
    Q_D(SysInfo);
    const QMutexLocker mutexLocker(&d->m_mutex);

    struct sysinfo si;
    const int rc = sysinfo(&si);
    if (rc) {
        return -1;
    }

    return static_cast<qint64>(si.freeram);
}

QString SysInfo::stackTrace()
{
    Q_D(SysInfo);
    const QMutexLocker mutexLocker(&d->m_mutex);

    fpos_t pos;

    QString tmpFile = QDir::tempPath();
    const QString appName = QApplication::applicationName();

    tmpFile += QStringLiteral("/Quentier_") + appName +
        QStringLiteral("_StackTrace.txt");

    // flush existing stderr and reopen it as file
    fflush(stderr);
    fgetpos(stderr, &pos);
    const int fd = dup(fileno(stderr));
    FILE * fileHandle = freopen(tmpFile.toLocal8Bit().data(), "w", stderr);
    if (!fileHandle) {
        perror("Can't reopen stderr");
        return {};
    }

    stacktrace::displayCurrentStackTrace();

    // revert stderr
    fflush(stderr);
    dup2(fd, fileno(stderr));
    close(fd);
    clearerr(stderr);
    fsetpos(stderr, &pos);
    fclose(fileHandle);

    QFile file{tmpFile};
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringLiteral("Cannot open temporary file with stacktrace");
    }

    return QString::fromLocal8Bit(file.readAll());
}

} // namespace quentier::utility
