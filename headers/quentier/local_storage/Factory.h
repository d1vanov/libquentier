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

#pragma once

#include <quentier/local_storage/Fwd.h>
#include <quentier/threading/Fwd.h>
#include <quentier/utility/Linkage.h>

#include <QtGlobal>

class QDir;

namespace quentier {

class Account;

} // namespace quentier

namespace quentier::local_storage {

[[nodiscard]] QUENTIER_EXPORT ILocalStoragePtr createSqliteLocalStorage(
    const Account & account, const QDir & localStorageDir,
    threading::QThreadPoolPtr threadPool);

} // namespace quentier::local_storage
