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

#include "EncryptionDialog.h"
#include "ui_EncryptionDialog.h"

#include "../NoteEditorSettingsNames.h"

#include <quentier/enml/DecryptedTextManager.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/QuentierCheckPtr.h>

#include <QLineEdit>

namespace quentier {

EncryptionDialog::EncryptionDialog(
    const QString & textToEncrypt, const Account & account,
    std::shared_ptr<EncryptionManager> encryptionManager,
    std::shared_ptr<DecryptedTextManager> decryptedTextManager,
    QWidget * parent) :
    QDialog(parent),
    m_pUI(new Ui::EncryptionDialog), m_textToEncrypt(textToEncrypt),
    m_account(account), m_encryptionManager(std::move(encryptionManager)),
    m_decryptedTextManager(std::move(decryptedTextManager))
{
    m_pUI->setupUi(this);
    QUENTIER_CHECK_PTR("note_editor:dialog", m_encryptionManager.get())

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
        m_pUI->rememberPasswordForSessionCheckBox, &QCheckBox::stateChanged,
        this, &EncryptionDialog::onRememberPassphraseStateChanged);
}

EncryptionDialog::~EncryptionDialog()
{
    delete m_pUI;
}

QString EncryptionDialog::passphrase() const
{
    return m_pUI->encryptionPasswordLineEdit->text();
}

bool EncryptionDialog::rememberPassphrase() const
{
    return m_pUI->rememberPasswordForSessionCheckBox->isChecked();
}

QString EncryptionDialog::encryptedText() const
{
    return m_cachedEncryptedText;
}

QString EncryptionDialog::hint() const
{
    return m_pUI->hintLineEdit->text();
}

void EncryptionDialog::setRememberPassphraseDefaultState(const bool checked)
{
    m_pUI->rememberPasswordForSessionCheckBox->setChecked(checked);
}

void EncryptionDialog::onRememberPassphraseStateChanged(int checked)
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
            QVariant(m_pUI->rememberPasswordForSessionCheckBox->isChecked()));
    }
}

void EncryptionDialog::accept()
{
    QString passphrase = m_pUI->encryptionPasswordLineEdit->text();
    QString repeatedPassphrase =
        m_pUI->repeatEncryptionPasswordLineEdit->text();

    if (passphrase.isEmpty()) {
        QNINFO(
            "note_editor:dialog",
            "Attempted to press OK in "
                << "EncryptionDialog without having a password set");
        ErrorString error(QT_TR_NOOP("Please choose the encryption password"));
        setError(error);
        return;
    }

    if (passphrase != repeatedPassphrase) {
        ErrorString error(
            QT_TR_NOOP("Can't encrypt: password and repeated "
                       "password do not match"));
        QNINFO("note_editor:dialog", error);
        setError(error);
        return;
    }

    m_cachedEncryptedText.resize(0);
    ErrorString errorDescription;
    QString cipher = QStringLiteral("AES");
    size_t keyLength = 128;

    bool res = m_encryptionManager->encrypt(
        m_textToEncrypt, passphrase, cipher, keyLength, m_cachedEncryptedText,
        errorDescription);

    if (!res) {
        QNINFO("note_editor:dialog", errorDescription);
        setError(errorDescription);
        return;
    }

    bool rememberForSession =
        m_pUI->rememberPasswordForSessionCheckBox->isChecked();

    m_decryptedTextManager->addEntry(
        m_cachedEncryptedText, m_textToEncrypt, rememberForSession, passphrase,
        cipher, keyLength);

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
