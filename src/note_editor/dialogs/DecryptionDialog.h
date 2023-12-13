/*
 * Copyright 2016-2023 Dmitry Ivanov
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

#include <cstddef>
#include <memory>

namespace Ui {

class DecryptionDialog;

} // namespace Ui

namespace quentier {

class Q_DECL_HIDDEN DecryptionDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit DecryptionDialog(
        QString encryptedText, QString cipher, QString hint, size_t keyLength,
        Account account, std::shared_ptr<EncryptionManager> encryptionManager,
        enml::IDecryptedTextCachePtr decryptedTextCache,
        QWidget * parent = nullptr, bool decryptPermanentlyFlag = false);

    ~DecryptionDialog() noexcept override;

    [[nodiscard]] QString passphrase() const noexcept;
    [[nodiscard]] bool rememberPassphrase() const noexcept;
    [[nodiscard]] bool decryptPermanently() const noexcept;

    [[nodiscard]] QString decryptedText() const noexcept;

Q_SIGNALS:
    void decryptionAccepted(
        QString cipher, size_t keyLength, QString encryptedText,
        QString passphrase, QString decryptedText, bool rememberPassphrase,
        bool decryptPermanently);

private Q_SLOTS:
    void setHint(const QString & hint);
    void setRememberPassphraseDefaultState(bool checked);
    void onRememberPassphraseStateChanged(int checked);
    void onShowPasswordStateChanged(int checked);

    void onDecryptPermanentlyStateChanged(int checked);

    void accept() override;

private:
    void setError(const ErrorString & error);

private:
    const std::shared_ptr<EncryptionManager> m_encryptionManager;
    const enml::IDecryptedTextCachePtr m_decryptedTextCache;

    Ui::DecryptionDialog * m_pUI;
    QString m_encryptedText;
    QString m_cipher;
    QString m_hint;
    QString m_cachedDecryptedText;
    Account m_account;
    std::size_t m_keyLength;
};

} // namespace quentier
