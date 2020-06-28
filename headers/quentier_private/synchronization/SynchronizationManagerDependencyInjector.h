/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include <quentier/synchronization/ISyncStateStorage.h>

#include <quentier_private/utility/IKeychainService.h>

namespace quentier {

class QUENTIER_EXPORT SynchronizationManagerDependencyInjector
{
public:
    INoteStore *        m_pNoteStore = nullptr;
    IUserStore *        m_pUserStore = nullptr;
    IKeychainService *  m_pKeychainService = nullptr;
    ISyncStateStorage * m_pSyncStateStorage = nullptr;
};

} // namespace quentier

#endif // LIB_QUENTIER_PRIVATE_SYNCHRONIZATION_SYNCHRONIZATION_MANAGER_DEPENDENCY_INJECTOR_H
