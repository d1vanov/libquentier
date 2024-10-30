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

#include "EncryptionDialog.h"
#include "ui_EncryptionDialog.h"

#include "../NoteEditorSettingsNames.h"

#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>

#include <QLineEdit>

namespace quentier {

EncryptionDialog::EncryptionDialog(
    QString textToEncrypt, Account account,
    std::shared_ptr<EncryptionManager> encryptionManager,
    enml::IDecryptedTextCachePtr decryptedTextCache, QWidget * parent) :
    QDialog{parent}, m_encryptionManager{std::move(encryptionManager)},
    m_decryptedTextCache{std::move(decryptedTextCache)},
    m_pUI{new Ui::EncryptionDialog}, m_textToEncrypt{std::move(textToEncrypt)},
    m_account{std::move(account)}
{
    if (Q_UNLIKELY(!m_encryptionManager)) {
        throw InvalidArgument{
            ErrorString{"EncryptionDialog ctor: encryption manager is null"}};
    }

    if (Q_UNLIKELY(!m_decryptedTextCache)) {
        throw InvalidArgument{
            ErrorString{"EncryptionDialog ctor: decrypted text cache is null"}};
    }

    m_pUI->setupUi(this);

    bool rememberPassphraseForSessionDefault = false;
    ApplicationSettings appSettings{m_account, NOTE_EDITOR_SETTINGS_NAME};

    const auto rememberPassphraseForSessionSetting =
        appSettings.value(NOTE_EDITOR_ENCRYPTION_REMEMBER_PASSWORD_FOR_SESSION);

    if (!rememberPassphraseForSessionSetting.isNull()) {
        rememberPassphraseForSessionDefault =
            rememberPassphraseForSessionSetting.toBool();
    }

    setRememberPassphraseDefaultState(rememberPassphraseForSessionDefault);
    m_pUI->onErrorTextLabel->setVisible(false);

    QObject::connect(
        m_pUI->rememberPasswordForSessionCheckBox, &QCheckBox::stateChanged,
        this, &EncryptionDialog::onRememberPassphraseStateChanged);
}

EncryptionDialog::~EncryptionDialog() noexcept
{
    delete m_pUI;
}

QString EncryptionDialog::passphrase() const noexcept
{
    return m_pUI->encryptionPasswordLineEdit->text();
}

bool EncryptionDialog::rememberPassphrase() const noexcept
{
    return m_pUI->rememberPasswordForSessionCheckBox->isChecked();
}

QString EncryptionDialog::encryptedText() const noexcept
{
    return m_cachedEncryptedText;
}

QString EncryptionDialog::hint() const noexcept
{
    return m_pUI->hintLineEdit->text();
}

void EncryptionDialog::setRememberPassphraseDefaultState(const bool checked)
{
    m_pUI->rememberPasswordForSessionCheckBox->setChecked(checked);
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
            QVariant(m_pUI->rememberPasswordForSessionCheckBox->isChecked()));
    }
}

void EncryptionDialog::accept()
{
    const QString passphrase = m_pUI->encryptionPasswordLineEdit->text();

    const QString repeatedPassphrase =
        m_pUI->repeatEncryptionPasswordLineEdit->text();

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

    m_cachedEncryptedText.resize(0);
    ErrorString errorDescription;
    QString cipher = QStringLiteral("AES");
    size_t keyLength = 128;

    const bool res = m_encryptionManager->encrypt(
        m_textToEncrypt, passphrase, cipher, keyLength, m_cachedEncryptedText,
        errorDescription);

    if (!res) {
        QNINFO("note_editor::EncryptionDialog", errorDescription);
        setError(errorDescription);
        return;
    }

    const bool rememberForSession =
        m_pUI->rememberPasswordForSessionCheckBox->isChecked();

    m_decryptedTextCache->addDecryptexTextInfo(
        m_cachedEncryptedText, m_textToEncrypt, passphrase, cipher, keyLength,
        rememberForSession ? enml::IDecryptedTextCache::RememberForSession::Yes
                           : enml::IDecryptedTextCache::RememberForSession::No);

    Q_EMIT encryptionAccepted(
        m_textToEncrypt, m_cachedEncryptedText, cipher, keyLength,
        m_pUI->hintLineEdit->text(), rememberForSession);

    QDialog::accept();
}

void EncryptionDialog::setError(const ErrorString & error)
{
    m_pUI->onErrorTextLabel->setText(error.localizedString());
    m_pUI->onErrorTextLabel->setVisible(true);
}

} // namespace quentier
