/*
 * Copyright 2021-2022 Dmitry Ivanov
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

#include <quentier/utility/Linkage.h>

#include <QFuture>

namespace quentier::local_storage {

/**
 * @brief The IPatch interface represents patches of the local storage. Each
 * such patch somehow changes the layout of local storage persistence so that
 * only compliant & corresponding versions of libquentier can be used to work
 * with it.
 */
class QUENTIER_EXPORT IPatch
{
public:
    virtual ~IPatch() noexcept;

    /**
     * @return      Version of local storage to which the patch needs to be
     *              applied
     */
    [[nodiscard]] virtual int fromVersion() const noexcept = 0;

    /**
     * @return      Version of local storage to which the patch would upgrade
     *              the local storage
     */
    [[nodiscard]] virtual int toVersion() const noexcept = 0;

    /**
     * @return      Short description of the patch
     */
    [[nodiscard]] virtual QString patchShortDescription() const = 0;

    /**
     * @return      Long i.e. detailed description of the patch
     */
    [[nodiscard]] virtual QString patchLongDescription() const = 0;

    /**
     * Backup either the entire local storage or its parts affected by the
     * particular patch, should be called before applying the patch (but can be
     * skipped if not desired).
     *
     * @return                      Future which can be awaited for the backup
     *                              completion. Contains exception if backup
     *                              fails.
     */
    [[nodiscard]] virtual QFuture<void> backupLocalStorage() = 0;

    /**
     * Restore local storage from previously made backup, presumably after the
     * failed attempt to apply a patch. Won't work if no backup was made before
     * applying a patch, obviously.
     *
     * @return                      Future which can be awaited for the backup
     *                              restoration completion. Contains exception
     *                              if backup restoration fails.
     */
    [[nodiscard]] virtual QFuture<void> restoreLocalStorageFromBackup() = 0;

    /**
     * Remove the previously made backup of local storage, presumably
     * after successful application of the patch so the backup is no longer
     * needed. It won't work if no backup was made before applying a patch,
     * obviously.
     *
     * @return                      Future which can be awaited for local
     *                              storage backup removal. Contains exception
     *                              if backup removal fails.
     */
    [[nodiscard]] virtual QFuture<void> removeLocalStorageBackup() = 0;

    /**
     * Apply the patch to local storage
     *
     * @return                      Future which can be awaited for patch
     *                              application. Contains exception if patch
     *                              application fails.
     */
    [[nodiscard]] virtual QFuture<void> apply() = 0;
};

} // namespace quentier::local_storage
