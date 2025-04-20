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

#include "EncryptSelectedTextDelegate.h"

#include "../NoteEditor_p.h"
#include "../dialogs/EncryptionDialog.h"

#include <quentier/enml/HtmlUtils.h>
#include <quentier/enml/IENMLTagsConverter.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define CHECK_NOTE_EDITOR()                                                    \
    if (Q_UNLIKELY(m_noteEditor.isNull())) {                                   \
        QNDEBUG("note_editor:delegate", "Note editor is null");                \
        return;                                                                \
    }

#define CHECK_ACCOUNT()                                                        \
    CHECK_NOTE_EDITOR()                                                        \
    if (Q_UNLIKELY(!m_noteEditor->accountPtr())) {                             \
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
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditor->page());        \
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
    NoteEditorPrivate * noteEditor, IEncryptorPtr encryptor,
    enml::IDecryptedTextCachePtr decryptedTextCache,
    enml::IENMLTagsConverterPtr enmlTagsConverter) :
    QObject(noteEditor), m_noteEditor(noteEditor),
    m_encryptor(std::move(encryptor)),
    m_decryptedTextCache(std::move(decryptedTextCache)),
    m_enmlTagsConverter(std::move(enmlTagsConverter))
{
    if (Q_UNLIKELY(!m_encryptor)) {
        throw InvalidArgument{
            ErrorString{"EncryptSelectedTextDelegate ctor: encryptor is null"}};
    }

    if (Q_UNLIKELY(!m_decryptedTextCache)) {
        throw InvalidArgument{ErrorString{
            "EncryptSelectedTextDelegate ctor: decrypted text cache is null"}};
    }

    if (Q_UNLIKELY(!m_enmlTagsConverter)) {
        throw InvalidArgument{ErrorString{
            "EncryptSelectedTextDelegate ctor: enml tags converter is null"}};
    }
}

void EncryptSelectedTextDelegate::start(const QString & selectionHtml)
{
    QNDEBUG(
        "note_editor::EncryptSelectedTextDelegate",
        "EncryptSelectedTextDelegate::start: selection html = "
            << selectionHtml);

    CHECK_NOTE_EDITOR()

    if (Q_UNLIKELY(selectionHtml.isEmpty())) {
        QNDEBUG(
            "note_editor::EncryptSelectedTextDelegate",
            "No selection html, nothing to encrypt");
        Q_EMIT cancelled();
        return;
    }

    m_selectionHtml = selectionHtml;

    raiseEncryptionDialog();
}

void EncryptSelectedTextDelegate::raiseEncryptionDialog()
{
    QNDEBUG(
        "note_editor::EncryptSelectedTextDelegate",
        "EncryptSelectedTextDelegate::raiseEncryptionDialog");

    CHECK_ACCOUNT()

    const auto encryptionDialog = std::make_unique<EncryptionDialog>(
        m_selectionHtml, *m_noteEditor->accountPtr(), m_encryptor,
        m_decryptedTextCache, m_noteEditor);

    encryptionDialog->setWindowModality(Qt::WindowModal);

    QObject::connect(
        encryptionDialog.get(), &EncryptionDialog::encryptionAccepted, this,
        &EncryptSelectedTextDelegate::onSelectedTextEncrypted);

    const int res = encryptionDialog->exec();

    QNTRACE(
        "note_editor::EncryptSelectedTextDelegate",
        "Executed encryption dialog: "
            << (res == QDialog::Accepted ? "accepted" : "rejected"));

    if (res == QDialog::Rejected) {
        Q_EMIT cancelled();
        return;
    }
}

void EncryptSelectedTextDelegate::onSelectedTextEncrypted(
    QString selectedText, QString encryptedText,   // NOLINT
    const IEncryptor::Cipher cipher, QString hint, // NOLINT
    bool rememberForSession)                       // NOLINT
{
    QNDEBUG(
        "note_editor::EncryptSelectedTextDelegate",
        "EncryptSelectedTextDelegate::onSelectedTextEncrypted: "
            << "encrypted text = " << encryptedText << ", hint = " << hint
            << ", remember for session = "
            << (rememberForSession ? "true" : "false"));

    CHECK_NOTE_EDITOR()

    Q_UNUSED(selectedText)

    m_rememberForSession = rememberForSession;

    if (m_rememberForSession) {
        m_encryptedText = enml::utils::htmlEscapeString(encryptedText);
        m_cipher = cipher;
        m_hint = enml::utils::htmlEscapeString(hint);
    }
    else {
        m_encryptedTextHtml = m_enmlTagsConverter->convertEncryptedText(
            encryptedText, hint, cipher, m_noteEditor->nextEncryptedTextId());

        m_encryptedTextHtml =
            enml::utils::htmlEscapeString(m_encryptedTextHtml);
    }

    if (m_noteEditor->isEditorPageModified()) {
        QObject::connect(
            m_noteEditor.data(), &NoteEditorPrivate::convertedToNote, this,
            &EncryptSelectedTextDelegate::onOriginalPageConvertedToNote);

        m_noteEditor->convertToNote();
    }
    else {
        encryptSelectedText();
    }
}

void EncryptSelectedTextDelegate::onOriginalPageConvertedToNote(
    qevercloud::Note note) // NOLINT
{
    QNDEBUG(
        "note_editor::EncryptSelectedTextDelegate",
        "EncryptSelectedTextDelegate::onOriginalPageConvertedToNote");

    CHECK_NOTE_EDITOR()

    Q_UNUSED(note)

    QObject::disconnect(
        m_noteEditor.data(), &NoteEditorPrivate::convertedToNote, this,
        &EncryptSelectedTextDelegate::onOriginalPageConvertedToNote);

    encryptSelectedText();
}

void EncryptSelectedTextDelegate::encryptSelectedText()
{
    QNDEBUG(
        "note_editor::EncryptSelectedTextDelegate",
        "EncryptSelectedTextDelegate::encryptSelectedText");

    GET_PAGE()

    QString javascript;
    if (m_rememberForSession) {
        const QString id = QString::number(m_noteEditor->nextDecryptedTextId());

        QString escapedDecryptedText =
            enml::utils::htmlEscapeString(m_selectionHtml);

        QString cipherStr;
        QTextStream strm{&cipherStr};
        strm << m_cipher;
        strm.flush();

        javascript = QStringLiteral(
                         "encryptDecryptManager."
                         "replaceSelectionWithDecryptedText('") +
            id + QStringLiteral("', '") + escapedDecryptedText +
            QStringLiteral("', '") + m_encryptedText + QStringLiteral("', '") +
            m_hint + QStringLiteral("', '") + cipherStr + QStringLiteral("');");
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
        "note_editor::EncryptSelectedTextDelegate",
        "EncryptSelectedTextDelegate::onEncryptionScriptDone: " << data);

    const auto resultMap = data.toMap();

    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of text encryption "
                       "script from JavaScript"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!statusIt.value().toBool()) {
        ErrorString error;

        const auto errorIt = resultMap.find(QStringLiteral("error"));
        if (Q_UNLIKELY(errorIt == resultMap.end())) {
            error.setBase(
                QT_TR_NOOP("Can't parse the error of text encryption "
                           "from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't encrypt the selected text"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor::EncryptSelectedTextDelegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished();
}

} // namespace quentier
