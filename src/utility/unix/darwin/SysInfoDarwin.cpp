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

#include <quentier/utility/SysInfo.h>
#include <quentier/utility/Macros.h>
#include "../../SysInfo_p.h"
#include <unistd.h>
#include <sys/sysctl.h>
#include <mach/vm_statistics.h>
#include <mach/mach_types.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include "../StackTrace.h"
#include <QString>
#include <QMutexLocker>

namespace quentier {

qint64 SysInfo::totalMemory()
{
    Q_D(SysInfo);
    QMutexLocker mutexLocker(&d->m_mutex);

    int mib[2];
    int64_t physical_memory;
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    size_t length = sizeof(int64_t);
    int rc = sysctl(mib, 2, &physical_memory, &length, NULL, 0);
    if (rc) {
        return -1;
    }

    return static_cast<qint64>(physical_memory);
}

qint64 SysInfo::freeMemory()
{
    Q_D(SysInfo);
    QMutexLocker mutexLocker(&d->m_mutex);

    vm_size_t page_size;
    mach_port_t mach_port;
    mach_msg_type_number_t count;
    vm_statistics_data_t vm_stats;

    mach_port = mach_host_self();
    count = sizeof(vm_stats) / sizeof(natural_t);
    if (KERN_SUCCESS == host_page_size(mach_port, &page_size) &&
        KERN_SUCCESS == host_statistics(mach_port, HOST_VM_INFO,
                                        (host_info_t)&vm_stats, &count))
    {
        return static_cast<qint64>(vm_stats.free_count * static_cast<qint64>(page_size));
    }
    else
    {
        return -1;
    }
}

QString SysInfo::platformName()
{
    char str[256];
    size_t size = sizeof(str);
    int ret = sysctlbyname("kern.osrelease", str, &size, NULL, 0);
    if (ret != 0) {
        return QStringLiteral("Unknown Darwin");
    }

    QString qstr = QString::fromLocal8Bit(&str[0], static_cast<int>(size));

    if (qstr.startsWith(QStringLiteral("5."))) {
        return QStringLiteral("Mac OS X 10.1 Puma");
    }
    else if (qstr.startsWith(QStringLiteral("6."))) {
        return QStringLiteral("Mac OS X 10.2 Jaguar");
    }
    else if (qstr.startsWith(QStringLiteral("7."))) {
        return QStringLiteral("Mac OS X 10.3 Panther");
    }
    else if (qstr.startsWith(QStringLiteral("8."))) {
        return QStringLiteral("Mac OS X 10.4 Tiger");
    }
    else if (qstr.startsWith(QStringLiteral("9."))) {
        return QStringLiteral("Mac OS X 10.5 Leopard");
    }
    else if (qstr.startsWith(QStringLiteral("10."))) {
        return QStringLiteral("Mac OS X 10.6 Snow Leopard");
    }
    else if (qstr.startsWith(QStringLiteral("11."))) {
        return QStringLiteral("Mac OS X 10.7 Lion");
    }
    else if (qstr.startsWith(QStringLiteral("12."))) {
        return QStringLiteral("Mac OS X 10.8 Mountain Lion");
    }
    else if (qstr.startsWith(QStringLiteral("13."))) {
        return QStringLiteral("Mac OS X 10.9 Mavericks");
    }
    else if (qstr.startsWith(QStringLiteral("14."))) {
        return QStringLiteral("Mac OS X 10.10 Yosemite");
    }
    else if (qstr.startsWith(QStringLiteral("15."))) {
        return QStringLiteral("Mac OS X 10.11 El Capitan");
    }
    else if (qstr.startsWith(QStringLiteral("16."))) {
        return QStringLiteral("macOS 10.12 Sierra");
    }
    else if (qstr.startsWith(QStringLiteral("17."))) {
        return QStringLiteral("macOS 10.13 High Sierra");
    }
    else if (qstr.startsWith(QStringLiteral("18."))) {
        return QStringLiteral("macOS 10.14 Mojave");
    }

    return QStringLiteral("Unknown Darwin");
}

} // namespace quentier
