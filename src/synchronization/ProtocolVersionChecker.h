/*
 * Copyright 2022 Dmitry Ivanov
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

#include <synchronization/IProtocolVersionChecker.h>

#include <qevercloud/services/Fwd.h>

#include <QMutex>

#include <optional>

namespace quentier::synchronization {

class ProtocolVersionChecker final : public IProtocolVersionChecker
{
public:
    explicit ProtocolVersionChecker(qevercloud::IUserStorePtr userStore);

    [[nodiscard]] QFuture<void> checkProtocolVersion(
        const IAuthenticationInfo & authenticationInfo) override;

private:
    const qevercloud::IUserStorePtr m_userStore;

    QMutex m_mutex;
    std::optional<QFuture<void>> m_future;
};

} // namespace quentier::synchronization
