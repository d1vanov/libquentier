/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_PATCH_H
#define LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_PATCH_H

#include <quentier/utility/Linkage.h>

#include <QObject>

namespace quentier {

class ErrorString;

/**
 * @brief The ILocalStoragePatch class represents the interface for patches of
 * local storage. Each such patch somehow changes the layout of local storage
 * persistence so that only compliant & corresponding versions of libquentier
 * can be used to work with it
 */
class QUENTIER_EXPORT ILocalStoragePatch : public QObject
{
    Q_OBJECT
protected:
    explicit ILocalStoragePatch(QObject * parent = nullptr);

public:
    ~ILocalStoragePatch() noexcept override;

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
     * @param errorDescription      The textual description of the error in case
     *                              of backup preparation failure
     * @return                      True if the local storage was backed up for
     *                              the patch successfully, false otherwise
     */
    [[nodiscard]] virtual bool backupLocalStorage(
        ErrorString & errorDescription) = 0;

    /**
     * Restore local storage from previously made backup, presumably after the
     * failed attempt to apply a patch. Won't work if no backup was made before
     * applying a patch, obviously.
     *
     * @param errorDescription      The textual description of the error in case
     *                              of failure to restore the local storage from
     *                              backup
     * @return                      True if local storage was successfully
     *                              restored from backup, false otherwise
     */
    [[nodiscard]] virtual bool restoreLocalStorageFromBackup(
        ErrorString & errorDescription) = 0;

    /**
     * Remove the previously made backup of local storage, presumably
     * after successful application of the patch so the backup is no longer
     * needed. It won't work if no backup was made before applying a patch,
     * obviously.
     *
     * @param errorDescription      The textual description of the error in case
     *                              of failure to remove the local storage
     *                              backup
     * @return                      True if local storage backup was
     * successfully removed, false otherwise
     */
    [[nodiscard]] virtual bool removeLocalStorageBackup(
        ErrorString & errorDescription) = 0;

    /**
     * Apply the patch to local storage
     *
     * @param errorDescription      The textual description of the error in case
     *                              of patch application failure
     * @return                      True in case of successful patch
     * application, false otherwise
     */
    [[nodiscard]] virtual bool apply(ErrorString & errorDescription) = 0;

Q_SIGNALS:
    /**
     * Patch application progress signal
     *
     * @param progress          Patch application progress value, from 0 to 1
     */
    void progress(double progress);

    /**
     * Local storage backup preparation progress
     *
     * @param progress          Backup preparation progress value, from 0 to 1
     */
    void backupProgress(double progress);

    /**
     * Local storage restoration from backup progress
     *
     * @param progress          Local storage restoration from backup progress
     *                          value, from 0 to 1
     */
    void restoreBackupProgress(double progress);
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_PATCH_H
