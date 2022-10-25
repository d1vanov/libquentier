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

#include "IVersionHandler.h"

#include <quentier/threading/Fwd.h>
#include <quentier/types/Account.h>

#include <QtGlobal>

#include <memory>

class QSqlDatabase;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class VersionHandler final :
    public IVersionHandler,
    public std::enable_shared_from_this<VersionHandler>
{
public:
    explicit VersionHandler(
        Account account, ConnectionPoolPtr connectionPool,
        threading::QThreadPoolPtr threadPool,
        threading::QThreadPtr writerThread);

    [[nodiscard]] QFuture<bool> isVersionTooHigh() const override;
    [[nodiscard]] QFuture<bool> requiresUpgrade() const override;

    [[nodiscard]] QFuture<QList<IPatchPtr>> requiredPatches() const override;

    [[nodiscard]] QFuture<qint32> version() const override;
    [[nodiscard]] QFuture<qint32> highestSupportedVersion() const override;

private:
    [[nodiscard]] qint32 versionImpl(
        QSqlDatabase & databaseConnection,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 highestSupportedVersionImpl() const noexcept;

private:
    Account m_account;
    ConnectionPoolPtr m_connectionPool;
    threading::QThreadPoolPtr m_threadPool;
    threading::QThreadPtr m_writerThread;
};

} // namespace quentier::local_storage::sql
