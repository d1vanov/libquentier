/*
 * Copyright 2018-2019 Dmitry Ivanov
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

#include "LocalStoragePatchManager.h"
#include "LocalStorageManager_p.h"
#include "patches/LocalStoragePatch1To2.h"

#include <quentier/types/ErrorString.h>
#include <quentier/logging/QuentierLogger.h>

#include <iterator>

namespace quentier {

LocalStoragePatchManager::LocalStoragePatchManager(
        const Account & account,
        LocalStorageManagerPrivate & localStorageManager,
        QSqlDatabase & database, QObject * parent) :
    QObject(parent),
    m_account(account),
    m_localStorageManager(localStorageManager),
    m_sqlDatabase(database)
{}

QVector<QSharedPointer<ILocalStoragePatch> >
LocalStoragePatchManager::patchesForCurrentVersion()
{
    QVector<QSharedPointer<ILocalStoragePatch> > result;

    ErrorString errorDescription;
    int version = m_localStorageManager.localStorageVersion(errorDescription);
    if (version <= 0) {
        QNWARNING("LocalStoragePatchManager::"
                  "patchInfoForCurrentLocalStorageVersion: "
                  "unable to determine the current local "
                  "storage version");
        return result;
    }

    if (version == 1) {
        result.append(QSharedPointer<ILocalStoragePatch>(
                new LocalStoragePatch1To2(m_account,
                                          m_localStorageManager,
                                          m_sqlDatabase)));
    }

    return result;
}

} // namespace quentier
