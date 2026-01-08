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

#include <quentier/enml/IDecryptedTextCache.h>

#include <QDebug>
#include <QTextStream>

namespace quentier::enml {

namespace {

template <class T>
void printRememberForSession(
    T & t, const IDecryptedTextCache::RememberForSession rememberForSession)
{
    switch (rememberForSession) {
    case IDecryptedTextCache::RememberForSession::Yes:
        t << "Yes";
        break;
    case IDecryptedTextCache::RememberForSession::No:
        t << "No";
        break;
    }
}

} // namespace

IDecryptedTextCache::~IDecryptedTextCache() = default;

QDebug & operator<<(
    QDebug & dbg,
    const IDecryptedTextCache::RememberForSession rememberForSession)
{
    printRememberForSession(dbg, rememberForSession);
    return dbg;
}

QTextStream & operator<<(
    QTextStream & strm,
    const IDecryptedTextCache::RememberForSession rememberForSession)
{
    printRememberForSession(strm, rememberForSession);
    return strm;
}

} // namespace quentier::enml
