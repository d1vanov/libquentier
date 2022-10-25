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

#include "PatchBase.h"

#include <quentier/types/Account.h>

#include <QPointer>

namespace quentier::local_storage::sql {

class Q_DECL_HIDDEN Patch2To3 final : public PatchBase
{
    Q_DECLARE_TR_FUNCTIONS(Patch2To3)
public:
    explicit Patch2To3(
        Account account, ConnectionPoolPtr connectionPool,
        threading::QThreadPtr writerThread);

    ~Patch2To3() noexcept override = default;

    [[nodiscard]] int fromVersion() const noexcept override
    {
        return 2;
    }

    [[nodiscard]] int toVersion() const noexcept override
    {
        return 3;
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

    struct ResourceVersionIds
    {
        QString m_dataBodyVersionId;
        QString m_alternateDataBodyVersionId;
    };

    [[nodiscard]] QHash<QString, ResourceVersionIds> generateVersionIds() const;

    [[nodiscard]] std::optional<QHash<QString, ResourceVersionIds>>
        fetchVersionIdsFromDatabase(ErrorString & errorDescription) const;

    [[nodiscard]] bool putVersionIdsToDatabase(
        const QHash<QString, ResourceVersionIds> & resourceVersionIds,
        ErrorString & errorDescription);

private:
    Q_DISABLE_COPY(Patch2To3)

private:
    Account m_account;
};

} // namespace quentier::local_storage::sql
