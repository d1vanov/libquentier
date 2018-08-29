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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_PATCH_H
#define LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_PATCH_H

#include <quentier/utility/Macros.h>
#include <quentier/utility/Linkage.h>
#include <QObject>
#include <QStringList>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ErrorString)
QT_FORWARD_DECLARE_CLASS(LocalStorageDatabaseUpgrader)

/**
 * @brief The ILocalStoragePatch class represents the interface for patches of
 * local storage. Each such patch somehow changes the layout of local storage
 * persistence so that only compliant & corresponding versions of libquentier
 * can be used to work with it
 */
class QUENTIER_EXPORT ILocalStoragePatch: public QObject
{
    Q_OBJECT
protected:
    explicit ILocalStoragePatch(QObject * parent = Q_NULLPTR);

public:
    virtual ~ILocalStoragePatch();

    /**
     * @return      Version of local storage to which the patch needs to be applied
     */
    virtual int fromVersion() const = 0;

    /**
     * @return      Version of local storage to which the patch would upgrade the local storage
     */
    virtual int toVersion() const = 0;

    /**
     * @return      Short description of the patch
     */
    virtual QString patchShortDescription() const = 0;

    /**
     * @return      Long i.e. detailed description of the patch
     */
    virtual QStringList patchLongDescription() const = 0;

    /**
     * Apply the patch to local storage
     *
     * @param errorDescription      The textual description of the error in case of patch application failure
     * @return                      True in case of successful patch application, false otherwise
     */
    virtual bool apply(ErrorString & errorDescription) = 0;

    friend class LocalStorageDatabaseUpgrader;

Q_SIGNALS:
    /**
     * Patch application progress signal
     *
     * @param progress          Patch application progress value, from 0 to 1
     */
    void progress(double progress);
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_I_LOCAL_STORAGE_PATCH_H
