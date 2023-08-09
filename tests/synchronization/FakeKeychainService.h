/*
 * Copyright 2023 Dmitry Ivanov
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

#include <quentier/threading/Fwd.h>
#include <quentier/utility/IKeychainService.h>

#include <QHash>
#include <QMutex>

namespace quentier::synchronization::tests {

class FakeKeychainService final : public IKeychainService
{
public:
    using PasswordByKey = QHash<QString, QString>;
    using PasswordByKeyByService = QHash<QString, PasswordByKey>;

    FakeKeychainService(
        threading::QThreadPoolPtr threadPool = nullptr,
        PasswordByKeyByService passwords = {});

    [[nodiscard]] PasswordByKeyByService passwords() const;
    void setPasswords(PasswordByKeyByService passwords);
    void clear();

public: // IKeychainService
    [[nodiscard]] QFuture<void> writePassword(
        QString service, QString key, QString password) override;

    [[nodiscard]] QFuture<QString> readPassword(
        QString service, QString key) const override;

    [[nodiscard]] QFuture<void> deletePassword(
        QString service, QString key) override;

private:
    const threading::QThreadPoolPtr m_threadPool;

    QHash<QString, PasswordByKey> m_servicesKeysAndPasswords;
    mutable QMutex m_mutex;
};

} // namespace quentier::synchronization::tests
