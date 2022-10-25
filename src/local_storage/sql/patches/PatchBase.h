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

#include "../Fwd.h"

#include <quentier/local_storage/IPatch.h>
#include <quentier/threading/Fwd.h>

#include <QCoreApplication>
#include <QDir>

#include <memory>

template <class T>
class QPromise;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class Q_DECL_HIDDEN PatchBase :
    public IPatch,
    public std::enable_shared_from_this<PatchBase>
{
    Q_DECLARE_TR_FUNCTIONS(PatchBase)
protected:
    explicit PatchBase(
        ConnectionPoolPtr connectionPool, threading::QThreadPtr writerThread,
        const QString & localStorageDirPath, const QString & backupDirPath);

public:
    ~PatchBase() noexcept override = default;

    [[nodiscard]] QFuture<void> backupLocalStorage() final;
    [[nodiscard]] QFuture<void> restoreLocalStorageFromBackup() final;
    [[nodiscard]] QFuture<void> removeLocalStorageBackup() final;
    [[nodiscard]] QFuture<void> apply() final;

protected:
    [[nodiscard]] virtual bool backupLocalStorageSync(
        QPromise<void> & promise, // for progress updates and cancel tracking
        ErrorString & errorDescription) = 0;

    [[nodiscard]] virtual bool restoreLocalStorageFromBackupSync(
        QPromise<void> & promise, // for progress updates
        ErrorString & errorDescription) = 0;

    [[nodiscard]] virtual bool removeLocalStorageBackupSync(
        ErrorString & errorDescription) = 0;

    [[nodiscard]] virtual bool applySync(
        QPromise<void> & promise, // for progress updates
        ErrorString & errorDescription) = 0;

private:
    Q_DISABLE_COPY(PatchBase)

protected:
    ConnectionPoolPtr m_connectionPool;
    QDir m_localStorageDir;
    QDir m_backupDir;
    threading::QThreadPtr m_writerThread;
};

} // namespace quentier::local_storage::sql
