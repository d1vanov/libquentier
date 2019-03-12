/*
 * Copyright 2016 Dmitry Ivanov
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
#include "SysInfo_p.h"
#include <sstream>

#if defined(__GNUC__) && !defined(__clang__)
#define BOOST_STACKTRACE_USE_BACKTRACE
#endif

#include <boost/stacktrace.hpp>

namespace quentier {

SysInfo::SysInfo() :
    d_ptr(new SysInfoPrivate)
{}

SysInfo::~SysInfo()
{}

QString SysInfo::stackTrace()
{
    std::stringstream sstrm;
    sstrm << boost::stacktrace::stacktrace();
    return QString::fromStdString(sstrm.str());
}

} // namespace quentier
