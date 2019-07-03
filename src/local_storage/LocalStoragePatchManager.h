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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_PATCH_MANAGER_H
#define LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_PATCH_MANAGER_H

#include <quentier/utility/Macros.h>
#include <quentier/types/Account.h>
#include <QObject>
#include <QSharedPointer>

QT_FORWARD_DECLARE_CLASS(QSqlDatabase)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ILocalStoragePatch)
QT_FORWARD_DECLARE_CLASS(LocalStorageManagerPrivate)

/**
 * @brief The LocalStoragePatchManager controls the set of patches which are
 * applied to the local storage when its schema/structure needs to be changed
 * (with upgrades of libquentier)
 */
class Q_DECL_HIDDEN LocalStoragePatchManager: public QObject
{
    Q_OBJECT
public:
    explicit LocalStoragePatchManager(const Account & account,
                                      LocalStorageManagerPrivate & localStorageManager,
                                      QSqlDatabase & database,
                                      QObject * parent = Q_NULLPTR);

    /**
     * @return          The list of patches required to be applied to the
     *                  current version of local storage
     */
    QVector<QSharedPointer<ILocalStoragePatch> > patchesForCurrentVersion();

private:
    Q_DISABLE_COPY(LocalStoragePatchManager)

private:
    Account                             m_account;
    LocalStorageManagerPrivate &        m_localStorageManager;
    QSqlDatabase &                      m_sqlDatabase;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_LOCAL_STORAGE_PATCH_MANAGER_H