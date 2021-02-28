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

#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ENCRYPT_SELECTED_TEXT_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ENCRYPT_SELECTED_TEXT_DELEGATE_H

#include "JsResultCallbackFunctor.hpp"

#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>

#include <QPointer>

#include <memory>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(DecryptedTextManager)
QT_FORWARD_DECLARE_CLASS(EncryptionManager)
QT_FORWARD_DECLARE_CLASS(NoteEditorPrivate)

/**
 * @brief The EncryptSelectedTextDelegate class encapsulates a chain of
 * callbacks required for proper implementation of currently selected text
 * encryption considering the details of wrapping this action around the undo
 * stack
 */
class Q_DECL_HIDDEN EncryptSelectedTextDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit EncryptSelectedTextDelegate(
        NoteEditorPrivate * pNoteEditor,
        std::shared_ptr<EncryptionManager> encryptionManager,
        std::shared_ptr<DecryptedTextManager> decryptedTextManager);

    void start(const QString & selectionHtml);

Q_SIGNALS:
    void finished();
    void cancelled();
    void notifyError(ErrorString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);

    void onSelectedTextEncrypted(
        QString selectedText, QString encryptedText, QString cipher,
        size_t keyLength, QString hint, bool rememberForSession);

    void onEncryptionScriptDone(const QVariant & data);

private:
    void raiseEncryptionDialog();
    void encryptSelectedText();

private:
    using JsCallback = JsResultCallbackFunctor<EncryptSelectedTextDelegate>;

private:
    QPointer<NoteEditorPrivate> m_pNoteEditor;
    std::shared_ptr<EncryptionManager> m_encryptionManager;
    std::shared_ptr<DecryptedTextManager> m_decryptedTextManager;

    QString m_encryptedTextHtml;

    QString m_selectionHtml;
    QString m_encryptedText;
    QString m_cipher;
    QString m_keyLength;
    QString m_hint;
    bool m_rememberForSession = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_ENCRYPT_SELECTED_TEXT_DELEGATE_H
