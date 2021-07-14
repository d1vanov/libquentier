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

#include "PatchBase.h"

#include <quentier/types/Account.h>

#include <QPointer>

namespace quentier::local_storage::sql {

class Q_DECL_HIDDEN Patch1To2 final : public PatchBase
{
    Q_DECLARE_TR_FUNCTIONS(Patch1To2)
public:
    explicit Patch1To2(
        Account account, ConnectionPoolPtr connectionPool,
        QThreadPtr writerThread);

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

private:
    [[nodiscard]] bool backupLocalStorageSync(
        QPromise<void> & promise, // for progress updates and cancel tracking
        ErrorString & errorDescription) override;

    [[nodiscard]] bool restoreLocalStorageFromBackupSync(
        QPromise<void> & promise, // for progress updates
        ErrorString & errorDescription) override;

    [[nodiscard]] bool removeLocalStorageBackupSync(
        ErrorString & errorDescription) override;

    [[nodiscard]] bool applySync(
        QPromise<void> & promise, // for progress updates
        ErrorString & errorDescription) override;

    [[nodiscard]] QStringList listResourceLocalIds(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    void filterResourceLocalIds(QStringList & resourceLocalIds) const;

    [[nodiscard]] bool ensureExistenceOfResouceDataDirs(
        ErrorString & errorDescription);

    [[nodiscard]] bool compactDatabase(
        QSqlDatabase & database, ErrorString & errorDescription);

private:
    Account m_account;
};

} // namespace quentier::local_storage::sql
