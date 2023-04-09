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

#include "EncryptedAreaPlugin.h"
#include "ui_EncryptedAreaPlugin.h"

#include "NoteEditorPluginFactory.h"
#include "NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

#include <QAction>

namespace quentier {

EncryptedAreaPlugin::EncryptedAreaPlugin(
    NoteEditorPrivate & noteEditor, QWidget * parent) :
    QWidget(parent),
    m_pUi(new Ui::EncryptedAreaPlugin), m_noteEditor(noteEditor)
{
    QNDEBUG("note_editor", "EncryptedAreaPlugin: constructor");

    m_pUi->setupUi(this);

    QAction * showEncryptedTextAction = new QAction(this);

    showEncryptedTextAction->setText(
        tr("Show encrypted text") + QStringLiteral("..."));

    showEncryptedTextAction->setEnabled(m_noteEditor.isPageEditable());

    QObject::connect(
        showEncryptedTextAction, &QAction::triggered, this,
        &EncryptedAreaPlugin::decrypt);

    m_pUi->toolButton->addAction(showEncryptedTextAction);

    if (m_noteEditor.isPageEditable()) {
        QObject::connect(
            m_pUi->iconPushButton, &QPushButton::released, this,
            &EncryptedAreaPlugin::decrypt);
    }
}

EncryptedAreaPlugin::~EncryptedAreaPlugin()
{
    QNDEBUG("note_editor", "EncryptedAreaPlugin: destructor");
    delete m_pUi;
}

bool EncryptedAreaPlugin::initialize(
    const QStringList & parameterNames, const QStringList & parameterValues,
    const NoteEditorPluginFactory & pluginFactory,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "note_editor",
        "EncryptedAreaPlugin::initialize: parameter names = "
            << parameterNames.join(QStringLiteral(", "))
            << ", parameter values = "
            << parameterValues.join(QStringLiteral(", ")));

    Q_UNUSED(pluginFactory)

    const int numParameterValues = parameterValues.size();

    int cipherIndex = parameterNames.indexOf(QStringLiteral("cipher"));
    if (numParameterValues <= cipherIndex) {
        errorDescription.setBase(
            QT_TR_NOOP("No value was found for cipher attribute"));
        return false;
    }

    int encryptedTextIndex =
        parameterNames.indexOf(QStringLiteral("encrypted_text"));
    if (encryptedTextIndex < 0) {
        errorDescription.setBase(
            QT_TR_NOOP("Encrypted text parameter was not found within "
                       "the object with encrypted text"));
        return false;
    }

    int keyLengthIndex = parameterNames.indexOf(QStringLiteral("length"));
    if (numParameterValues <= keyLengthIndex) {
        errorDescription.setBase(
            QT_TR_NOOP("No value was found for length attribute"));
        return false;
    }

    if (keyLengthIndex >= 0) {
        m_keyLength = parameterValues[keyLengthIndex];
    }
    else {
        m_keyLength = QStringLiteral("128");
        QNDEBUG(
            "note_editor",
            "Using the default value of key length = "
                << m_keyLength << " instead of missing HTML attribute");
    }

    if (cipherIndex >= 0) {
        m_cipher = parameterValues[cipherIndex];
    }
    else {
        m_cipher = QStringLiteral("AES");
        QNDEBUG(
            "note_editor",
            "Using the default value of cipher = "
                << m_cipher << " instead of missing HTML attribute");
    }

    m_encryptedText = parameterValues[encryptedTextIndex];

    int hintIndex = parameterNames.indexOf(QStringLiteral("hint"));
    if ((hintIndex < 0) || (numParameterValues <= hintIndex)) {
        m_hint.clear();
    }
    else {
        m_hint = parameterValues[hintIndex];
    }

    int enCryptIndexIndex =
        parameterNames.indexOf(QStringLiteral("en-crypt-id"));

    if ((enCryptIndexIndex < 0) || (numParameterValues <= enCryptIndexIndex)) {
        m_id.clear();
    }
    else {
        m_id = parameterValues[enCryptIndexIndex];
    }

    QNTRACE(
        "note_editor",
        "Initialized encrypted area plugin: cipher = "
            << m_cipher << ", length = " << m_keyLength << ", hint = " << m_hint
            << ", en-crypt-id = " << m_id
            << ", encrypted text = " << m_encryptedText);
    return true;
}

QString EncryptedAreaPlugin::name() const
{
    return QStringLiteral("EncryptedAreaPlugin");
}

QString EncryptedAreaPlugin::description() const
{
    return tr(
        "Encrypted area plugin - note editor plugin used for the display "
        "and convenient work with encrypted text within notes");
}

void EncryptedAreaPlugin::decrypt()
{
    m_noteEditor.decryptEncryptedText(
        m_encryptedText, m_cipher, m_keyLength, m_hint, m_id);
}

} // namespace quentier
