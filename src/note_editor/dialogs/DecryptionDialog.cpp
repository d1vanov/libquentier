/*
 * Copyright 2016-2025 Dmitry Ivanov
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

#include "DecryptionDialog.h"
#include "ui_DecryptionDialog.h"

#include "../NoteEditorSettingsNames.h"

#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>

namespace quentier {

DecryptionDialog::DecryptionDialog(
    QString encryptedText, IEncryptor::Cipher cipher, QString hint,
    Account account, IEncryptorPtr encryptor,
    enml::IDecryptedTextCachePtr decryptedTextCache, QWidget * parent,
    bool decryptPermanentlyFlag) :
    QDialog{parent}, m_encryptor{std::move(encryptor)},
    m_decryptedTextCache{std::move(decryptedTextCache)},
    m_ui{new Ui::DecryptionDialog}, m_encryptedText{std::move(encryptedText)},
    m_cipher{cipher}, m_hint{std::move(hint)}, m_account{std::move(account)}
{
    if (Q_UNLIKELY(!m_encryptor)) {
        throw InvalidArgument{
            ErrorString{"DecryptionDialog ctor: encryptor is null"}};
    }

    if (Q_UNLIKELY(!m_decryptedTextCache)) {
        throw InvalidArgument{
            ErrorString{"DecryptionDialog ctor: decrypted text cache is null"}};
    }

    m_ui->setupUi(this);
    m_ui->decryptPermanentlyCheckBox->setChecked(decryptPermanentlyFlag);

    setHint(m_hint);

    bool rememberPassphraseForSessionDefault = false;
    ApplicationSettings appSettings{m_account, NOTE_EDITOR_SETTINGS_NAME};

    const auto rememberPassphraseForSessionSetting =
        appSettings.value(NOTE_EDITOR_ENCRYPTION_REMEMBER_PASSWORD_FOR_SESSION);

    if (!rememberPassphraseForSessionSetting.isNull()) {
        rememberPassphraseForSessionDefault =
            rememberPassphraseForSessionSetting.toBool();
    }

    setRememberPassphraseDefaultState(rememberPassphraseForSessionDefault);
    m_ui->onErrorTextLabel->setVisible(false);

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    QObject::connect(
        m_ui->showPasswordCheckBox, &QCheckBox::checkStateChanged, this,
        &DecryptionDialog::onShowPasswordStateChanged);

    QObject::connect(
        m_ui->rememberPasswordCheckBox, &QCheckBox::checkStateChanged, this,
        &DecryptionDialog::onRememberPassphraseStateChanged);

    QObject::connect(
        m_ui->decryptPermanentlyCheckBox, &QCheckBox::checkStateChanged, this,
        &DecryptionDialog::onDecryptPermanentlyStateChanged);
#else
    QObject::connect(
        m_ui->showPasswordCheckBox, &QCheckBox::stateChanged, this,
        &DecryptionDialog::onShowPasswordStateChanged);

    QObject::connect(
        m_ui->rememberPasswordCheckBox, &QCheckBox::stateChanged, this,
        &DecryptionDialog::onRememberPassphraseStateChanged);

    QObject::connect(
        m_ui->decryptPermanentlyCheckBox, &QCheckBox::stateChanged, this,
        &DecryptionDialog::onDecryptPermanentlyStateChanged);
#endif
}

DecryptionDialog::~DecryptionDialog() noexcept
{
    delete m_ui;
}

QString DecryptionDialog::passphrase() const noexcept
{
    return m_ui->passwordLineEdit->text();
}

bool DecryptionDialog::rememberPassphrase() const noexcept
{
    return m_ui->rememberPasswordCheckBox->isChecked();
}

bool DecryptionDialog::decryptPermanently() const noexcept
{
    return m_ui->decryptPermanentlyCheckBox->isChecked();
}

QString DecryptionDialog::decryptedText() const noexcept
{
    return m_decryptedText;
}

void DecryptionDialog::setError(const ErrorString & error)
{
    m_ui->onErrorTextLabel->setText(error.localizedString());
    m_ui->onErrorTextLabel->setVisible(true);
}

void DecryptionDialog::setHint(const QString & hint)
{
    m_ui->hintLabel->setText(
        tr("Hint: ") + (hint.isEmpty() ? tr("No hint available") : hint));
}

void DecryptionDialog::setRememberPassphraseDefaultState(const bool checked)
{
    m_ui->rememberPasswordCheckBox->setChecked(checked);
}

void DecryptionDialog::onRememberPassphraseStateChanged(int checked)
{
    Q_UNUSED(checked)

    ApplicationSettings appSettings{m_account, NOTE_EDITOR_SETTINGS_NAME};
    if (!appSettings.isWritable()) {
        QNINFO(
            "note_editor::DecryptionDialog",
            "Can't persist remember passphrase for session setting: settings "
                << "are not writable");
    }
    else {
        appSettings.setValue(
            NOTE_EDITOR_ENCRYPTION_REMEMBER_PASSWORD_FOR_SESSION,
            QVariant(m_ui->rememberPasswordCheckBox->isChecked()));
    }
}

void DecryptionDialog::onShowPasswordStateChanged(int checked)
{
    m_ui->passwordLineEdit->setEchoMode(
        checked ? QLineEdit::Normal : QLineEdit::Password);

    m_ui->passwordLineEdit->setFocus();
}

void DecryptionDialog::onDecryptPermanentlyStateChanged(int checked)
{
    m_ui->rememberPasswordCheckBox->setEnabled(!static_cast<bool>(checked));
}

void DecryptionDialog::accept()
{
    const QString passphrase = m_ui->passwordLineEdit->text();

    auto res = m_encryptor->decrypt(m_encryptedText, passphrase, m_cipher);
    if (!res.isValid() && m_cipher == IEncryptor::Cipher::AES) {
        QNDEBUG(
            "note_editor::DecryptionDialog",
            "The initial attempt to decrypt the text using AES cipher has "
                << "failed; checking whether it is old encrypted text area "
                << "using RC2 encryption");

        res = m_encryptor->decrypt(
            m_encryptedText, passphrase, IEncryptor::Cipher::RC2);
    }

    if (!res.isValid()) {
        ErrorString error{QT_TR_NOOP("Failed to decrypt the text")};
        const auto & errorDescription = res.error();
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        setError(error);
        return;
    }

    m_decryptedText = res.get();

    const bool rememberForSession = m_ui->rememberPasswordCheckBox->isChecked();

    const bool decryptPermanently =
        m_ui->decryptPermanentlyCheckBox->isChecked();

    m_decryptedTextCache->addDecryptexTextInfo(
        m_encryptedText, m_decryptedText, passphrase, m_cipher,
        rememberForSession ? enml::IDecryptedTextCache::RememberForSession::Yes
                           : enml::IDecryptedTextCache::RememberForSession::No);

    QNTRACE(
        "note_editor::DecryptionDialog",
        "Cached decrypted text for encryptedText: "
            << m_encryptedText << "; remember for session = "
            << (rememberForSession ? "true" : "false")
            << "; decrypt permanently = "
            << (decryptPermanently ? "true" : "false"));

    Q_EMIT decryptionAccepted(
        m_encryptedText, m_cipher, passphrase, m_decryptedText,
        rememberForSession, decryptPermanently);

    QDialog::accept();
}

} // namespace quentier
