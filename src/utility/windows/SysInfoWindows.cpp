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

#ifdef __MINGW32__
#define _WIN32_WINNT 0x0501
#endif

#include <quentier/utility/SysInfo.h>
#include "../SysInfo_p.h"
#include <QMutexLocker>
#include <QString>
#include <QSysInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

namespace quentier {

qint64 SysInfo::pageSize()
{
    SYSTEM_INFO systemInfo;
    GetNativeSystemInfo (&systemInfo);
    return static_cast<qint64>(systemInfo.dwPageSize);
}

qint64 SysInfo::freeMemory()
{
    Q_D(SysInfo);
    QMutexLocker mutexLocker(&d->m_mutex);

    MEMORYSTATUSEX memory_status;
    ZeroMemory(&memory_status, sizeof(MEMORYSTATUSEX));
    memory_status.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memory_status)) {
        return static_cast<qint64>(memory_status.ullAvailPhys);
    }
    else {
        return -1;
    }
}

qint64 SysInfo::totalMemory()
{
    Q_D(SysInfo);
    QMutexLocker mutexLocker(&d->m_mutex);

    MEMORYSTATUSEX memory_status;
    ZeroMemory(&memory_status, sizeof(MEMORYSTATUSEX));
    memory_status.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memory_status)) {
        return static_cast<qint64>(memory_status.ullTotalPhys);
    }
    else {
        return -1;
    }
}

QString SysInfo::stackTrace()
{
    return QStringLiteral("Stack trace obtaining is not implemented on Windows, patches are welcome");
}

QString SysInfo::platformName()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 4, 0)
    return QSysInfo::prettyProductName();
#else
    switch(QSysInfo::WindowsVersion)
    {
    case QSysInfo::WV_32s:
        return QStringLiteral("Windows 3.1");
    case QSysInfo::WV_95:
        return QStringLiteral("Windows 95");
    case QSysInfo::WV_98:
        return QStringLiteral("Windows 98");
    case QSysInfo::WV_ME:
        return QStringLiteral("Windows ME");
    case QSysInfo::WV_NT:
        return QStringLiteral("Windows NT");
    case QSysInfo::WV_2000:
        return QStringLiteral("Windows 2000");
    case QSysInfo::WV_XP:
        return QStringLiteral("Windows XP");
    case QSysInfo::WV_VISTA:
        return QStringLiteral("Windows Vista");
    case QSysInfo::WV_WINDOWS7:
        return QStringLiteral("Windows 7");
    case QSysInfo::WV_WINDOWS8:
        return QStringLiteral("Windows 8");
#if QT_VERSION >= QT_VERSION_CHECK(4, 8, 6)
    case QSysInfo::WV_WINDOWS8_1:
        return QStringLiteral("Windows 8.1");
#endif
#if QT_VERSION_CHECK >= QT_VERSION_CHECK(4, 8, 7)
    case QSysInfo::WV_WINDOWS10:
        return QStringLiteral("Windows 10");
#endif
    }

    // If we got here, the version is unclear, trying to use WinAPI to figure it out
    OSVERSIONINFOW info;
    ZeroMemory(&info, sizeof(OSVERSIONINFOW));
    info.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
    GetVersionEx(&info);

    QString result = QStringLiteral("Windows/");
    result += QString::number(info.dwMajorVersion);
    result += QStringLiteral(".");
    result += QString::number(info.dwMinorVersion);
    return result;
#endif
}

} // namespace quentier
