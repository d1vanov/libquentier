/*
 * Copyright 2023 Dmitry Ivanov
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

#include "SynchronizationMode.h"

#include <QDebug>
#include <QTextStream>

namespace quentier::synchronization {

namespace
{

template <class T>
void printSyncMode(T & t, const SynchronizationMode syncMode)
{
    switch (syncMode)
    {
    case SynchronizationMode::Full:
        t << "Full";
        break;
    case SynchronizationMode::Incremental:
        t << "Incremental";
        break;
    }
}

} // namespace

QDebug & operator<<(QDebug & dbg, const SynchronizationMode mode)
{
    printSyncMode(dbg, mode);
    return dbg;
}

QTextStream & operator<<(QTextStream & strm, const SynchronizationMode mode)
{
    printSyncMode(strm, mode);
    return strm;
}

} // namespace quentier::synchronization
