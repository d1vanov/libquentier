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

#include "EncryptSelectedTextDelegate.h"

#include "../NoteEditor_p.h"
#include "../dialogs/EncryptionDialog.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define CHECK_NOTE_EDITOR()                                                    \
    if (Q_UNLIKELY(m_pNoteEditor.isNull())) {                                  \
        QNDEBUG("note_editor:delegate", "Note editor is null");                \
        return;                                                                \
    }

#define CHECK_ACCOUNT()                                                        \
    CHECK_NOTE_EDITOR()                                                        \
    if (Q_UNLIKELY(!m_pNoteEditor->accountPtr())) {                            \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "EncryptSelectedTextDelegate",                                     \
            "Can't encrypt the selected text: "                                \
            "no account is set to the note editor"));                          \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

#define GET_PAGE()                                                             \
    CHECK_NOTE_EDITOR()                                                        \
    auto * page = qobject_cast<NoteEditorPage *>(m_pNoteEditor->page());       \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "EncryptSelectedTextDelegate",                                     \
            "Can't encrypt the selected text: "                                \
            "no note editor page"));                                           \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

EncryptSelectedTextDelegate::EncryptSelectedTextDelegate(
    NoteEditorPrivate * pNoteEditor,
    std::shared_ptr<EncryptionManager> encryptionManager,
    std::shared_ptr<DecryptedTextManager> decryptedTextManager) :
    QObject(pNoteEditor),
    m_pNoteEditor(pNoteEditor),
    m_encryptionManager(std::move(encryptionManager)),
    m_decryptedTextManager(std::move(decryptedTextManager))
{}

void EncryptSelectedTextDelegate::start(const QString & selectionHtml)
{
    QNDEBUG(
        "note_editor:delegate",
        "EncryptSelectedTextDelegate::start: "
            << "selection html = " << selectionHtml);

    CHECK_NOTE_EDITOR()

    if (Q_UNLIKELY(selectionHtml.isEmpty())) {
        QNDEBUG(
            "note_editor:delegate", "No selection html, nothing to encrypt");
        Q_EMIT cancelled();
        return;
    }

    m_selectionHtml = selectionHtml;

    raiseEncryptionDialog();
}

void EncryptSelectedTextDelegate::raiseEncryptionDialog()
{
    QNDEBUG(
        "note_editor:delegate",
        "EncryptSelectedTextDelegate"
            << "::raiseEncryptionDialog");

    CHECK_ACCOUNT()

    auto pEncryptionDialog = std::make_unique<EncryptionDialog>(
        m_selectionHtml, *m_pNoteEditor->accountPtr(), m_encryptionManager,
        m_decryptedTextManager, m_pNoteEditor);

    pEncryptionDialog->setWindowModality(Qt::WindowModal);

    QObject::connect(
        pEncryptionDialog.get(), &EncryptionDialog::encryptionAccepted, this,
        &EncryptSelectedTextDelegate::onSelectedTextEncrypted);

    int res = pEncryptionDialog->exec();

    QNTRACE(
        "note_editor:delegate",
        "Executed encryption dialog: "
            << (res == QDialog::Accepted ? "accepted" : "rejected"));

    if (res == QDialog::Rejected) {
        Q_EMIT cancelled();
        return;
    }
}

void EncryptSelectedTextDelegate::onSelectedTextEncrypted(
    QString selectedText, QString encryptedText, QString cipher,
    size_t keyLength, QString hint, bool rememberForSession)
{
    QNDEBUG(
        "note_editor:delegate",
        "EncryptSelectedTextDelegate"
            << "::onSelectedTextEncrypted: encrypted text = " << encryptedText
            << ", hint = " << hint << ", remember for session = "
            << (rememberForSession ? "true" : "false"));

    CHECK_NOTE_EDITOR()

    Q_UNUSED(selectedText)

    m_rememberForSession = rememberForSession;

    if (m_rememberForSession) {
        m_encryptedText = encryptedText;
        ENMLConverter::escapeString(m_encryptedText);

        m_cipher = cipher;
        ENMLConverter::escapeString(m_cipher);

        m_keyLength = QString::number(keyLength);

        m_hint = hint;
        ENMLConverter::escapeString(m_hint);
    }
    else {
        m_encryptedTextHtml = ENMLConverter::encryptedTextHtml(
            encryptedText, hint, cipher, keyLength,
            m_pNoteEditor->GetFreeEncryptedTextId());

        ENMLConverter::escapeString(m_encryptedTextHtml);
    }

    if (m_pNoteEditor->isEditorPageModified()) {
        QObject::connect(
            m_pNoteEditor.data(), &NoteEditorPrivate::convertedToNote, this,
            &EncryptSelectedTextDelegate::onOriginalPageConvertedToNote);

        m_pNoteEditor->convertToNote();
    }
    else {
        encryptSelectedText();
    }
}

void EncryptSelectedTextDelegate::onOriginalPageConvertedToNote(Note note)
{
    QNDEBUG(
        "note_editor:delegate",
        "EncryptSelectedTextDelegate"
            << "::onOriginalPageConvertedToNote");

    CHECK_NOTE_EDITOR()

    Q_UNUSED(note)

    QObject::disconnect(
        m_pNoteEditor.data(), &NoteEditorPrivate::convertedToNote, this,
        &EncryptSelectedTextDelegate::onOriginalPageConvertedToNote);

    encryptSelectedText();
}

void EncryptSelectedTextDelegate::encryptSelectedText()
{
    QNDEBUG(
        "note_editor:delegate",
        "EncryptSelectedTextDelegate"
            << "::encryptSelectedText");

    GET_PAGE()

    QString javascript;
    if (m_rememberForSession) {
        QString id = QString::number(m_pNoteEditor->GetFreeDecryptedTextId());
        QString escapedDecryptedText = m_selectionHtml;
        ENMLConverter::escapeString(escapedDecryptedText);

        javascript = QStringLiteral(
                         "encryptDecryptManager."
                         "replaceSelectionWithDecryptedText('") +
            id + QStringLiteral("', '") + escapedDecryptedText +
            QStringLiteral("', '") + m_encryptedText + QStringLiteral("', '") +
            m_hint + QStringLiteral("', '") + m_cipher +
            QStringLiteral("', '") + m_keyLength + QStringLiteral("');");
    }
    else {
        javascript = QString::fromUtf8(
                         "encryptDecryptManager.encryptSelectedText('%1');")
                         .arg(m_encryptedTextHtml);
    }

    page->executeJavaScript(
        javascript,
        JsCallback(
            *this, &EncryptSelectedTextDelegate::onEncryptionScriptDone));
}

void EncryptSelectedTextDelegate::onEncryptionScriptDone(const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "EncryptSelectedTextDelegate"
            << "::onEncryptionScriptDone: " << data);

    auto resultMap = data.toMap();

    auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of text encryption "
                       "script from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    bool res = statusIt.value().toBool();
    if (!res) {
        ErrorString error;

        auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of text encryption "
                           "from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't encrypt the selected text"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished();
}

} // namespace quentier
