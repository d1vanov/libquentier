/*
 * Copyright 2021 Dmitry Ivanov
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

#include "../Fwd.h"

#include <quentier/local_storage/ILocalStoragePatch.h>
#include <quentier/types/Account.h>

namespace quentier::local_storage::sql {

class Q_DECL_HIDDEN Patch1To2 final : public ILocalStoragePatch
{
public:
    explicit Patch1To2(
        Account account, ConnectionPoolPtr pConnectionPool,
        QObject * parent = nullptr);

    ~Patch1To2() noexcept override = default;

    [[nodiscard]] int fromVersion() const noexcept override
    {
        return 1;
    }

    [[nodiscard]] int toVersion() const noexcept override
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

    [[nodiscard]] int resourceCount(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    [[nodiscard]] bool compactDatabase(
        QSqlDatabase & database, ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(Patch1To2)

private:
    Account m_account;
    ConnectionPoolPtr m_pConnectionPool;
    QString m_backupDirPath;
};

} // namespace quentier::local_storage::sql
