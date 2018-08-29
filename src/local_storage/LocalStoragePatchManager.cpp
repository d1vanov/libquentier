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

#include "LocalStoragePatchManager.h"
#include "LocalStorageManager_p.h"
#include "patches/LocalStoragePatch1To2.h"
#include <quentier/types/ErrorString.h>
#include <quentier/logging/QuentierLogger.h>
#include <iterator>

namespace quentier {

LocalStoragePatchManager::LocalStoragePatchManager(const Account & account,
                                                   LocalStorageManagerPrivate & localStorageManager,
                                                   QSqlDatabase & database, QObject * parent) :
    QObject(parent),
    m_account(account),
    m_localStorageManager(localStorageManager),
    m_sqlDatabase(database),
    m_patches()
{
    registerPatch(new LocalStoragePatch1To2(account, localStorageManager, database));
}

QVector<ILocalStoragePatch*> LocalStoragePatchManager::patchesForCurrentVersion()
{
    QVector<ILocalStoragePatch*> result;

    ErrorString errorDescription;
    int version = m_localStorageManager.localStorageVersion(errorDescription);
    if (version <= 0) {
        QNWARNING(QStringLiteral("LocalStoragePatchManager::patchInfoForCurrentLocalStorageVersion: unable to determine the current local storage version"));
        return result;
    }

    auto it = m_patches.find(version);
    if (it == m_patches.end()) {
        return result;
    }

    result.reserve(static_cast<int>(std::distance(it, m_patches.end())));
    for(auto cit = it; cit != m_patches.end(); ++cit) {
        result << cit->second;
    }

    return result;
}

void LocalStoragePatchManager::registerPatch(ILocalStoragePatch * pPatch)
{
    QNDEBUG(QStringLiteral("LocalStoragePatchManager::registerPatch: from version ")
            << (pPatch ? QString::number(pPatch->fromVersion()) : QStringLiteral("<null>"))
            << QStringLiteral(" to version ") << (pPatch ? QString::number(pPatch->toVersion()) : QStringLiteral("<null>")));

    if (Q_UNLIKELY(!pPatch)) {
        QNWARNING(QStringLiteral("Detected attempt to register null patch for local storage"));
        return;
    }

    pPatch->setParent(this);
    m_patches[pPatch->fromVersion()] = pPatch;
}

} // namespace quentier
