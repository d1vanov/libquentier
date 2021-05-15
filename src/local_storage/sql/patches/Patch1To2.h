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

#include <quentier/local_storage/IPatch.h>
#include <quentier/types/Account.h>

#include <QCoreApplication>
#include <QPointer>

#include <memory>

template <class T>
class QPromise;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class Q_DECL_HIDDEN Patch1To2 final :
    public IPatch,
    public std::enable_shared_from_this<Patch1To2>
{
    Q_DECLARE_TR_FUNCTIONS(Patch1To2)
public:
    explicit Patch1To2(
        Account account, ConnectionPoolPtr pConnectionPool,
        QThreadPtr pWriterThread);

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

    [[nodiscard]] QFuture<void> backupLocalStorage() override;

    [[nodiscard]] QFuture<void> restoreLocalStorageFromBackup() override;

    [[nodiscard]] QFuture<void> removeLocalStorageBackup() override;

    [[nodiscard]] QFuture<void> apply() override;

private:
    [[nodiscard]] bool backupLocalStorageImpl(
        QPromise<void> & promise, // for progress updates and cancel tracking
        ErrorString & errorDescription);

    [[nodiscard]] bool restoreLocalStorageFromBackupImpl(
        QPromise<void> & promise, // for progress updates
        ErrorString & errorDescription);

    [[nodiscard]] bool removeLocalStorageBackupImpl(
        ErrorString & errorDescription);

    [[nodiscard]] bool applyImpl(
        QPromise<void> & promise, // for progress updates
        ErrorString & errorDescription);

    [[nodiscard]] QStringList listResourceLocalIds(
        QSqlDatabase & database, ErrorString & errorDescription) const;

    void filterResourceLocalIds(QStringList & resourceLocalIds) const;

    [[nodiscard]] bool ensureExistenceOfResouceDataDirs(
        ErrorString & errorDescription);

    [[nodiscard]] bool compactDatabase(
        QSqlDatabase & database, ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(Patch1To2)

private:
    Account m_account;
    ConnectionPoolPtr m_pConnectionPool;
    QString m_backupDirPath;
    QThreadPtr m_pWriterThread;
};

} // namespace quentier::local_storage::sql
