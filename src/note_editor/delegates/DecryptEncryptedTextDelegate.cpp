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

#include "DecryptEncryptedTextDelegate.h"

#include "../NoteEditorPage.h"
#include "../NoteEditor_p.h"
#include "../dialogs/DecryptionDialog.h"

#include <quentier/enml/HtmlUtils.h>
#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/enml/IENMLTagsConverter.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/EncryptionManager.h>

#include <memory>

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
            "DecryptEncryptedTextDelegate",                                    \
            "Can't decrypt the encrypted text: "                               \
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
            "DecryptEncryptedTextDelegate",                                    \
            "Can't decrypt the encrypted text: "                               \
            "no note editor page"));                                           \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

DecryptEncryptedTextDelegate::DecryptEncryptedTextDelegate(
    QString encryptedTextId, QString encryptedText, QString cipher,
    const QString & length, QString hint, NoteEditorPrivate * pNoteEditor,
    std::shared_ptr<EncryptionManager> encryptionManager,
    enml::IDecryptedTextCachePtr decryptedTextCache,
    enml::IENMLTagsConverterPtr enmlTagsConverter) :
    QObject(pNoteEditor), m_encryptionManager(std::move(encryptionManager)),
    m_decryptedTextCache(std::move(decryptedTextCache)),
    m_enmlTagsConverter(std::move(enmlTagsConverter)),
    m_encryptedTextId(std::move(encryptedTextId)),
    m_encryptedText(std::move(encryptedText)), m_cipher(std::move(cipher)),
    m_hint(std::move(hint)), m_pNoteEditor(pNoteEditor)
{
    if (Q_UNLIKELY(!m_encryptionManager)) {
        throw InvalidArgument{ErrorString{
            "DecryptEncryptedTextDelegate ctor: encryption manager is null"}};
    }

    if (Q_UNLIKELY(!m_decryptedTextCache)) {
        throw InvalidArgument{ErrorString{
            "DecryptEncryptedTextDelegate ctor: decrypted text cache is null"}};
    }

    if (Q_UNLIKELY(!m_enmlTagsConverter)) {
        throw InvalidArgument{ErrorString{
            "DecryptEncryptedTextDelegate ctor: enml tags converter is null"}};
    }

    if (length.isEmpty()) {
        m_length = 128;
    }
    else {
        bool conversionResult = false;
        m_length = static_cast<size_t>(length.toInt(&conversionResult));
        if (Q_UNLIKELY(!conversionResult)) {
            // NOTE: postponing the error report until the attempt to start
            // the delegate
            m_length = 0;
        }
    }
}

void DecryptEncryptedTextDelegate::start()
{
    QNDEBUG(
        "note_editor::DecryptEncryptedTextDelegate",
        "DecryptEncryptedTextDelegate::start");

    CHECK_NOTE_EDITOR()

    if (Q_UNLIKELY(!m_length)) {
        ErrorString errorDescription(
            QT_TR_NOOP("Can't decrypt the encrypted text: "
                       "can't convert the encryption key "
                       "length from string to number"));
        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    if (m_pNoteEditor->isEditorPageModified()) {
        QObject::connect(
            m_pNoteEditor.data(), &NoteEditorPrivate::convertedToNote, this,
            &DecryptEncryptedTextDelegate::onOriginalPageConvertedToNote);

        m_pNoteEditor->convertToNote();
    }
    else {
        raiseDecryptionDialog();
    }
}

void DecryptEncryptedTextDelegate::onOriginalPageConvertedToNote(
    qevercloud::Note note) // NOLINT
{
    QNDEBUG(
        "note_editor::DecryptEncryptedTextDelegate",
        "DecryptEncryptedTextDelegate::onOriginalPageConvertedToNote");

    CHECK_NOTE_EDITOR()

    Q_UNUSED(note)

    QObject::disconnect(
        m_pNoteEditor.data(), &NoteEditorPrivate::convertedToNote, this,
        &DecryptEncryptedTextDelegate::onOriginalPageConvertedToNote);

    raiseDecryptionDialog();
}

void DecryptEncryptedTextDelegate::raiseDecryptionDialog()
{
    QNDEBUG(
        "note_editor::DecryptEncryptedTextDelegate",
        "DecryptEncryptedTextDelegate::raiseDecryptionDialog");

    CHECK_ACCOUNT()

    if (m_cipher.isEmpty()) {
        m_cipher = QStringLiteral("AES");
    }

    const auto pDecryptionDialog = std::make_unique<DecryptionDialog>(
        m_encryptedText, m_cipher, m_hint, m_length,
        *m_pNoteEditor->accountPtr(), m_encryptionManager, m_decryptedTextCache,
        m_pNoteEditor);

    pDecryptionDialog->setWindowModality(Qt::WindowModal);

    QObject::connect(
        pDecryptionDialog.get(), &DecryptionDialog::decryptionAccepted, this,
        &DecryptEncryptedTextDelegate::onEncryptedTextDecrypted);

    if (pDecryptionDialog->exec() == QDialog::Rejected) {
        Q_EMIT cancelled();
        return;
    }
}

void DecryptEncryptedTextDelegate::onEncryptedTextDecrypted(
    QString cipher, size_t keyLength, QString encryptedText, // NOLINT
    QString passphrase, QString decryptedText, bool rememberForSession,
    bool decryptPermanently)
{
    QNDEBUG(
        "note_editor::DecryptEncryptedTextDelegate",
        "DecryptEncryptedTextDelegate"
            << "::onEncryptedTextDecrypted: encrypted text = " << encryptedText
            << ", remember for session = "
            << (rememberForSession ? "true" : "false")
            << ", decrypt permanently = "
            << (decryptPermanently ? "true" : "false"));

    CHECK_NOTE_EDITOR()

    m_decryptedText = std::move(decryptedText);
    m_passphrase = std::move(passphrase);
    m_rememberForSession = rememberForSession;
    m_decryptPermanently = decryptPermanently;

    Q_UNUSED(cipher)
    Q_UNUSED(keyLength)

    QString decryptedTextHtml;
    if (!m_decryptPermanently) {
        decryptedTextHtml = m_enmlTagsConverter->convertDecryptedText(
            m_decryptedText, m_encryptedText, m_hint, m_cipher, m_length,
            m_pNoteEditor->nextDecryptedTextId());
    }
    else {
        decryptedTextHtml = m_decryptedText;
    }

    decryptedTextHtml = enml::utils::htmlEscapeString(decryptedTextHtml);

    GET_PAGE()
    page->executeJavaScript(
        QStringLiteral("encryptDecryptManager.decryptEncryptedText('") +
            m_encryptedTextId + QStringLiteral("', '") + decryptedTextHtml +
            QStringLiteral("');"),
        JsCallback(
            *this, &DecryptEncryptedTextDelegate::onDecryptionScriptFinished));
}

void DecryptEncryptedTextDelegate::onDecryptionScriptFinished(
    const QVariant & data)
{
    QNDEBUG(
        "note_editor::DecryptEncryptedTextDelegate",
        "DecryptEncryptedTextDelegate::onDecryptionScriptFinished: " << data);

    const auto resultMap = data.toMap();

    const auto statusIt = resultMap.find(QStringLiteral("status"));
    if (Q_UNLIKELY(statusIt == resultMap.end())) {
        ErrorString error(
            QT_TR_NOOP("Can't parse the result of text decryption "
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
                QT_TR_NOOP("Can't parse the error of text decryption "
                           "from JavaScript"));
        }
        else {
            error.setBase(QT_TR_NOOP("Can't decrypt the encrypted text"));
            error.details() = errorIt.value().toString();
        }

        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    Q_EMIT finished(
        m_encryptedText, m_cipher, m_length, m_hint, m_decryptedText,
        m_passphrase, m_rememberForSession, m_decryptPermanently);
}

} // namespace quentier
