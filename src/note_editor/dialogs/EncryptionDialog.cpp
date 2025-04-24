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

#include "EncryptionDialog.h"
#include "ui_EncryptionDialog.h"

#include "../NoteEditorSettingsNames.h"

#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/IEncryptor.h>

#include <QLineEdit>

namespace quentier {

EncryptionDialog::EncryptionDialog(
    QString textToEncrypt, Account account, IEncryptorPtr encryptor,
    enml::IDecryptedTextCachePtr decryptedTextCache, QWidget * parent) :
    QDialog{parent}, m_encryptor{std::move(encryptor)},
    m_decryptedTextCache{std::move(decryptedTextCache)},
    m_ui{new Ui::EncryptionDialog}, m_textToEncrypt{std::move(textToEncrypt)},
    m_account{std::move(account)}
{
    if (Q_UNLIKELY(!m_encryptor)) {
        throw InvalidArgument{
            ErrorString{"EncryptionDialog ctor: encryptor is null"}};
    }

    if (Q_UNLIKELY(!m_decryptedTextCache)) {
        throw InvalidArgument{
            ErrorString{"EncryptionDialog ctor: decrypted text cache is null"}};
    }

    m_ui->setupUi(this);

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
        m_ui->rememberPasswordForSessionCheckBox, &QCheckBox::checkStateChanged,
        this, &EncryptionDialog::onRememberPassphraseStateChanged);
#else
    QObject::connect(
        m_ui->rememberPasswordForSessionCheckBox, &QCheckBox::stateChanged,
        this, &EncryptionDialog::onRememberPassphraseStateChanged);
#endif
}

EncryptionDialog::~EncryptionDialog() noexcept
{
    delete m_ui;
}

QString EncryptionDialog::passphrase() const noexcept
{
    return m_ui->encryptionPasswordLineEdit->text();
}

bool EncryptionDialog::rememberPassphrase() const noexcept
{
    return m_ui->rememberPasswordForSessionCheckBox->isChecked();
}

QString EncryptionDialog::encryptedText() const noexcept
{
    return m_encryptedText;
}

QString EncryptionDialog::hint() const noexcept
{
    return m_ui->hintLineEdit->text();
}

void EncryptionDialog::setRememberPassphraseDefaultState(const bool checked)
{
    m_ui->rememberPasswordForSessionCheckBox->setChecked(checked);
}

void EncryptionDialog::onRememberPassphraseStateChanged(
    [[maybe_unused]] const int checked)
{
    ApplicationSettings appSettings{m_account, NOTE_EDITOR_SETTINGS_NAME};
    if (!appSettings.isWritable()) {
        QNINFO(
            "note_editor::EncryptionDialog",
            "Can't persist remember passphrase for session setting: settings "
                << "are not writable");
    }
    else {
        appSettings.setValue(
            NOTE_EDITOR_ENCRYPTION_REMEMBER_PASSWORD_FOR_SESSION,
            QVariant(m_ui->rememberPasswordForSessionCheckBox->isChecked()));
    }
}

void EncryptionDialog::accept()
{
    const QString passphrase = m_ui->encryptionPasswordLineEdit->text();

    const QString repeatedPassphrase =
        m_ui->repeatEncryptionPasswordLineEdit->text();

    if (passphrase.isEmpty()) {
        QNINFO(
            "note_editor::EncryptionDialog",
            "Attempted to press OK in EncryptionDialog without having a "
                << "password set");
        ErrorString error{QT_TR_NOOP("Please choose the encryption password")};
        setError(error);
        return;
    }

    if (passphrase != repeatedPassphrase) {
        ErrorString error{
            QT_TR_NOOP("Can't encrypt: password and repeated "
                       "password do not match")};
        QNINFO("note_editor::EncryptionDialog", error);
        setError(error);
        return;
    }

    const auto res = m_encryptor->encrypt(m_textToEncrypt, passphrase);

    if (!res.isValid()) {
        const auto & error = res.error();
        QNINFO("note_editor::EncryptionDialog", error);
        setError(error);
        return;
    }

    m_encryptedText = res.get();

    const bool rememberForSession =
        m_ui->rememberPasswordForSessionCheckBox->isChecked();

    m_decryptedTextCache->addDecryptexTextInfo(
        m_encryptedText, m_textToEncrypt, passphrase, IEncryptor::Cipher::AES,
        rememberForSession ? enml::IDecryptedTextCache::RememberForSession::Yes
                           : enml::IDecryptedTextCache::RememberForSession::No);

    Q_EMIT encryptionAccepted(
        m_textToEncrypt, m_encryptedText, IEncryptor::Cipher::AES,
        m_ui->hintLineEdit->text(), rememberForSession);

    QDialog::accept();
}

void EncryptionDialog::setError(const ErrorString & error)
{
    m_ui->onErrorTextLabel->setText(error.localizedString());
    m_ui->onErrorTextLabel->setVisible(true);
}

} // namespace quentier
