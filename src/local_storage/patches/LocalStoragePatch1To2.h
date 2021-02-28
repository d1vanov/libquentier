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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H
#define LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H

#include <quentier/local_storage/ILocalStoragePatch.h>
#include <quentier/types/Account.h>

QT_FORWARD_DECLARE_CLASS(QSqlDatabase)

namespace quentier {

QT_FORWARD_DECLARE_CLASS(LocalStorageManagerPrivate)

class Q_DECL_HIDDEN LocalStoragePatch1To2 final : public ILocalStoragePatch
{
    Q_OBJECT
public:
    explicit LocalStoragePatch1To2(
        const Account & account,
        LocalStorageManagerPrivate & localStorageManager,
        QSqlDatabase & database, QObject * parent = nullptr);

    virtual int fromVersion() const override
    {
        return 1;
    }
    virtual int toVersion() const override
    {
        return 2;
    }

    virtual QString patchShortDescription() const override;
    virtual QString patchLongDescription() const override;

    virtual bool backupLocalStorage(ErrorString & errorDescription) override;

    virtual bool restoreLocalStorageFromBackup(
        ErrorString & errorDescription) override;

    virtual bool removeLocalStorageBackup(
        ErrorString & errorDescription) override;

    virtual bool apply(ErrorString & errorDescription) override;

    // private
Q_SIGNALS:
    void copyDbFile(QString sourcePath, QString destPath);

private Q_SLOTS:
    void startLocalStorageBackup();
    void startLocalStorageRestorationFromBackup();

private:
    QStringList listResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(
        ErrorString & errorDescription);

    void filterResourceLocalUidsForDatabaseUpgradeFromVersion1ToVersion2(
        QStringList & resourceLocalUids);

    bool
    ensureExistenceOfResouceDataDirsForDatabaseUpgradeFromVersion1ToVersion2(
        ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(LocalStoragePatch1To2)

private:
    Account m_account;
    LocalStorageManagerPrivate & m_localStorageManager;
    QSqlDatabase & m_sqlDatabase;

    QString m_backupDirPath;
};

} // namespace quentier

#endif // LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H
