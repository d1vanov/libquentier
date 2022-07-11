/*
 * Copyright 2020-2022 Dmitry Ivanov
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

#include <quentier/utility/EncryptionManager.h>
#include <quentier/utility/IKeychainService.h>

namespace quentier {

/**
 * @brief The ObfuscatingKeychainService class implements IKeychainService
 * interface; it stores the data passed to it in ApplicationSettings in
 * obfuscated form. It is not really a secure storage and should not be used for
 * data which *must* be stored securely.
 */
class ObfuscatingKeychainService final : public IKeychainService
{
    Q_OBJECT
public:
    explicit ObfuscatingKeychainService(QObject * parent = nullptr);

    ~ObfuscatingKeychainService() noexcept override;

    [[nodiscard]] QFuture<void> writePassword(
        QString service, QString key, QString password) override;

    [[nodiscard]] QFuture<QString> readPassword(
        QString service, QString key) const override;

    [[nodiscard]] QFuture<void> deletePassword(
        QString service, QString key) override;

    [[nodiscard]] QUuid startWritePasswordJob(
        const QString & service, const QString & key,
        const QString & password) override;

    [[nodiscard]] QUuid startReadPasswordJob(
        const QString & service, const QString & key) override;

    [[nodiscard]] QUuid startDeletePasswordJob(
        const QString & service, const QString & key) override;

private:
    mutable EncryptionManager m_encryptionManager;
};

} // namespace quentier
