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

#ifndef LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H
#define LIB_QUENTIER_LOCAL_STORAGE_PATCHES_LOCAL_STORAGE_PATCH_1_TO_2_H

#include <quentier/local_storage/ILocalStoragePatch.h>
#include <quentier/types/Account.h>

class QSqlDatabase;

namespace quentier {

class LocalStorageManagerPrivate;

class Q_DECL_HIDDEN LocalStoragePatch1To2 final : public ILocalStoragePatch
{
    Q_OBJECT
public:
    explicit LocalStoragePatch1To2(
        Account account,
        LocalStorageManagerPrivate & localStorageManager,
        QSqlDatabase & database, QObject * parent = nullptr);

    ~LocalStoragePatch1To2() noexcept override = default;

    [[nodiscard]] int fromVersion() const override
    {
        return 1;
    }

    [[nodiscard]] int toVersion() const override
    {
        return 2;
    }

    [[nodiscard]] QString patchShortDescription() const override;
    [[nodiscard]] QString patchLongDescription() const override;

    [[nodiscard]] bool backupLocalStorage(
        ErrorString & errorDescription) override;

    [[nodiscard]] bool restoreLocalStorageFromBackup(
        ErrorString & errorDescription) override;

    [[nodiscard]] bool removeLocalStorageBackup(
        ErrorString & errorDescription) override;

    [[nodiscard]] bool apply(ErrorString & errorDescription) override;

    // private
Q_SIGNALS:
    void copyDbFile(QString sourcePath, QString destPath);

private Q_SLOTS:
    void startLocalStorageBackup();
    void startLocalStorageRestorationFromBackup();

private:
    [[nodiscard]] QStringList
    listResourceLocalIdsForDatabaseUpgradeFromVersion1ToVersion2(
        ErrorString & errorDescription);

    void filterResourceLocalIdsForDatabaseUpgradeFromVersion1ToVersion2(
        QStringList & resourceLocalIds);

    [[nodiscard]] bool
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
