/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include <quentier/enml/DecryptedTextManager.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/QuentierCheckPtr.h>

namespace quentier {

DecryptionDialog::DecryptionDialog(
    const QString & encryptedText, const QString & cipher, const QString & hint,
    const size_t keyLength, const Account & account,
    std::shared_ptr<EncryptionManager> encryptionManager,
    std::shared_ptr<DecryptedTextManager> decryptedTextManager,
    QWidget * parent, bool decryptPermanentlyFlag) :
    QDialog(parent),
    m_pUI(new Ui::DecryptionDialog), m_encryptedText(encryptedText),
    m_cipher(cipher), m_hint(hint), m_account(account),
    m_encryptionManager(std::move(encryptionManager)),
    m_decryptedTextManager(std::move(decryptedTextManager)),
    m_keyLength(keyLength)
{
    m_pUI->setupUi(this);
    QUENTIER_CHECK_PTR("note_editor:dialog", m_encryptionManager.get())

    m_pUI->decryptPermanentlyCheckBox->setChecked(decryptPermanentlyFlag);

    setHint(m_hint);

    bool rememberPassphraseForSessionDefault = false;
    ApplicationSettings appSettings(m_account, NOTE_EDITOR_SETTINGS_NAME);

    QVariant rememberPassphraseForSessionSetting =
        appSettings.value(NOTE_EDITOR_ENCRYPTION_REMEMBER_PASSWORD_FOR_SESSION);

    if (!rememberPassphraseForSessionSetting.isNull()) {
        rememberPassphraseForSessionDefault =
            rememberPassphraseForSessionSetting.toBool();
    }

    setRememberPassphraseDefaultState(rememberPassphraseForSessionDefault);
    m_pUI->onErrorTextLabel->setVisible(false);

    QObject::connect(
        m_pUI->showPasswordCheckBox, &QCheckBox::stateChanged, this,
        &DecryptionDialog::onShowPasswordStateChanged);

    QObject::connect(
        m_pUI->rememberPasswordCheckBox, &QCheckBox::stateChanged, this,
        &DecryptionDialog::onRememberPassphraseStateChanged);

    QObject::connect(
        m_pUI->decryptPermanentlyCheckBox, &QCheckBox::stateChanged, this,
        &DecryptionDialog::onDecryptPermanentlyStateChanged);
}

DecryptionDialog::~DecryptionDialog()
{
    delete m_pUI;
}

QString DecryptionDialog::passphrase() const
{
    return m_pUI->passwordLineEdit->text();
}

bool DecryptionDialog::rememberPassphrase() const
{
    return m_pUI->rememberPasswordCheckBox->isChecked();
}

bool DecryptionDialog::decryptPermanently() const
{
    return m_pUI->decryptPermanentlyCheckBox->isChecked();
}

QString DecryptionDialog::decryptedText() const
{
    return m_cachedDecryptedText;
}

void DecryptionDialog::setError(const ErrorString & error)
{
    m_pUI->onErrorTextLabel->setText(error.localizedString());
    m_pUI->onErrorTextLabel->setVisible(true);
}

void DecryptionDialog::setHint(const QString & hint)
{
    m_pUI->hintLabel->setText(
        tr("Hint: ") + (hint.isEmpty() ? tr("No hint available") : hint));
}

void DecryptionDialog::setRememberPassphraseDefaultState(const bool checked)
{
    m_pUI->rememberPasswordCheckBox->setChecked(checked);
}

void DecryptionDialog::onRememberPassphraseStateChanged(int checked)
{
    Q_UNUSED(checked)

    ApplicationSettings appSettings(m_account, NOTE_EDITOR_SETTINGS_NAME);
    if (!appSettings.isWritable()) {
        QNINFO(
            "note_editor:dialog",
            "Can't persist remember passphrase for "
                << "session setting: settings are not writable");
    }
    else {
        appSettings.setValue(
            NOTE_EDITOR_ENCRYPTION_REMEMBER_PASSWORD_FOR_SESSION,
            QVariant(m_pUI->rememberPasswordCheckBox->isChecked()));
    }
}

void DecryptionDialog::onShowPasswordStateChanged(int checked)
{
    m_pUI->passwordLineEdit->setEchoMode(
        checked ? QLineEdit::Normal : QLineEdit::Password);

    m_pUI->passwordLineEdit->setFocus();
}

void DecryptionDialog::onDecryptPermanentlyStateChanged(int checked)
{
    m_pUI->rememberPasswordCheckBox->setEnabled(!static_cast<bool>(checked));
}

void DecryptionDialog::accept()
{
    QString passphrase = m_pUI->passwordLineEdit->text();

    ErrorString errorDescription;
    bool res = m_encryptionManager->decrypt(
        m_encryptedText, passphrase, m_cipher, m_keyLength,
        m_cachedDecryptedText, errorDescription);

    if (!res && (m_cipher == QStringLiteral("AES")) && (m_keyLength == 128)) {
        QNDEBUG(
            "note_editor:dialog",
            "The initial attempt to decrypt the text "
                << "using AES cipher and 128 bit key has failed; checking "
                << "whether it is old encrypted text area using RC2 "
                << "encryption and 64 bit key");

        res = m_encryptionManager->decrypt(
            m_encryptedText, passphrase, QStringLiteral("RC2"), 64,
            m_cachedDecryptedText, errorDescription);
    }

    if (!res) {
        ErrorString error(QT_TR_NOOP("Failed to decrypt the text"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        setError(error);
        return;
    }

    bool rememberForSession = m_pUI->rememberPasswordCheckBox->isChecked();
    bool decryptPermanently = m_pUI->decryptPermanentlyCheckBox->isChecked();

    m_decryptedTextManager->addEntry(
        m_encryptedText, m_cachedDecryptedText, rememberForSession, passphrase,
        m_cipher, m_keyLength);

    QNTRACE(
        "note_editor:dialog",
        "Cached decrypted text for encryptedText: "
            << m_encryptedText << "; remember for session = "
            << (rememberForSession ? "true" : "false")
            << "; decrypt permanently = "
            << (decryptPermanently ? "true" : "false"));

    Q_EMIT decryptionAccepted(
        m_cipher, m_keyLength, m_encryptedText, passphrase,
        m_cachedDecryptedText, rememberForSession, decryptPermanently);

    QDialog::accept();
}

} // namespace quentier
