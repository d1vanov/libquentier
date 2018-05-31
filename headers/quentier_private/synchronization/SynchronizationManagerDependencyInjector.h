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

#ifndef LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_DEPENDENCY_INJECTOR_H
#define LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_DEPENDENCY_INJECTOR_H

#include "INoteStore.h"
#include "IUserStore.h"
#include "SyncStatePersistenceManager.h"
#include <quentier_private/utility/IKeychainService.h>

namespace quentier {

class QUENTIER_EXPORT SynchronizationManagerDependencyInjector
{
public:
    SynchronizationManagerDependencyInjector() :
        m_pNoteStore(Q_NULLPTR),
        m_pUserStore(Q_NULLPTR),
        m_pKeychainService(Q_NULLPTR),
        m_pSyncStatePersistenceManager(Q_NULLPTR)
    {}

    INoteStore *        m_pNoteStore;
    IUserStore *        m_pUserStore;
    IKeychainService *  m_pKeychainService;
    SyncStatePersistenceManager *   m_pSyncStatePersistenceManager;
};

} // namespace quentier

#endif // LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_DEPENDENCY_INJECTOR_H
