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

#include "Fwd.h"

#include <quentier/local_storage/Fwd.h>
#include <quentier/types/Account.h>

#include <QFuture>
#include <QtGlobal>

#include <memory>

class QSqlDatabase;
class QThreadPool;

namespace quentier {

class ErrorString;

} // namespace quentier

namespace quentier::local_storage::sql {

class VersionHandler final: public std::enable_shared_from_this<VersionHandler>
{
public:
    explicit VersionHandler(
        Account account, ConnectionPoolPtr pConnectionPool,
        QThreadPool * pThreadPool);

    [[nodiscard]] QFuture<bool> isVersionTooHigh() const;
    [[nodiscard]] QFuture<bool> requiresUpgrade() const;

    [[nodiscard]] QFuture<QList<ILocalStoragePatchPtr>> requiredPatches() const;

    [[nodiscard]] QFuture<qint32> version() const;
    [[nodiscard]] QFuture<qint32> highestSupportedVersion() const;

private:
    [[nodiscard]] qint32 versionImpl(
        QSqlDatabase & databaseConnection,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 highestSupportedVersionImpl() const noexcept;

private:
    Account m_account;
    ConnectionPoolPtr m_pConnectionPool;
    QThreadPool * m_pThreadPool;
};

} // namespace quentier::local_storage::sql
