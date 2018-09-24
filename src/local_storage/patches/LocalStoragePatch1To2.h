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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H
#define LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H

#include <quentier/local_storage/ILocalStoragePatch.h>
#include <quentier/types/Account.h>

QT_FORWARD_DECLARE_CLASS(QSqlDatabase)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerPrivate)

class Q_DECL_HIDDEN LocalStoragePatch1To2: public ILocalStoragePatch
{
    Q_OBJECT
public:
    explicit LocalStoragePatch1To2(const Account & account,
                                   LocalStorageManagerPrivate & localStorageManager,
                                   QSqlDatabase & database, QObject * parent = Q_NULLPTR);

    virtual int fromVersion() const Q_DECL_OVERRIDE { return 1; }
    virtual int toVersion() const Q_DECL_OVERRIDE { return 2; }

    virtual QString patchShortDescription() const Q_DECL_OVERRIDE;
    virtual QStringList patchLongDescription() const Q_DECL_OVERRIDE;

    virtual bool backupLocalStorage(ErrorString & errorDescription) Q_DECL_OVERRIDE;
    virtual bool restoreLocalStorageFromBackup(ErrorString & errorDescription) Q_DECL_OVERRIDE;

    virtual bool apply(ErrorString & errorDescription) Q_DECL_OVERRIDE;

// private
Q_SIGNALS:
    void copyDbFile(QString sourcePath, QString destPath);

private Q_SLOTS:
    void startLocalStorageBackup();
    void startLocalStorageRestorationFromBackup();

private:
    QStringList listResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription);
    void filterResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(QStringList & resourceLocalUids);
    bool ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(LocalStoragePatch1To2)

private:
    Account                         m_account;
    LocalStorageManagerPrivate &    m_localStorageManager;
    QSqlDatabase &                  m_sqlDatabase;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H
