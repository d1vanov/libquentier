/*
 * Copyright 2016-2024 Dmitry Ivanov
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

#include <quentier/enml/Fwd.h>
#include <quentier/types/Account.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/EncryptionManager.h>

#include <QDialog>

#include <memory>

namespace Ui {

class EncryptionDialog;

} // namespace Ui

namespace quentier {

class EncryptionDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit EncryptionDialog(
        QString textToEncrypt, Account account,
        std::shared_ptr<EncryptionManager> encryptionManager,
        enml::IDecryptedTextCachePtr decryptedTextCache,
        QWidget * parent = nullptr);

    ~EncryptionDialog() noexcept override;

    [[nodiscard]] QString passphrase() const noexcept;
    [[nodiscard]] bool rememberPassphrase() const noexcept;

    [[nodiscard]] QString encryptedText() const noexcept;
    [[nodiscard]] QString hint() const noexcept;

Q_SIGNALS:
    void encryptionAccepted(
        QString textToEncrypt, QString encryptedText, QString cipher,
        size_t keyLength, QString hint, bool rememberForSession);

private Q_SLOTS:
    void setRememberPassphraseDefaultState(bool checked);
    void onRememberPassphraseStateChanged(int checked);

    void accept() override;

private:
    void setError(const ErrorString & error);

private:
    const std::shared_ptr<EncryptionManager> m_encryptionManager;
    const enml::IDecryptedTextCachePtr m_decryptedTextCache;
    Ui::EncryptionDialog * m_pUI;

    QString m_textToEncrypt;
    QString m_cachedEncryptedText;
    Account m_account;
};

} // namespace quentier
