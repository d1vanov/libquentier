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

#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_DECRYPT_ENCRYPTED_TEXT_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_DECRYPT_ENCRYPTED_TEXT_DELEGATE_H

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
 * @brief The DecryptEncryptedTextDelegate class encapsulates a chain of
 * callbacks required for proper implementation of decryption for encrypted text
 * considering the details of wrapping this action around the undo stack
 */
class Q_DECL_HIDDEN DecryptEncryptedTextDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit DecryptEncryptedTextDelegate(
        const QString & encryptedTextId, const QString & encryptedText,
        const QString & cipher, const QString & length, const QString & hint,
        NoteEditorPrivate * pNoteEditor,
        std::shared_ptr<EncryptionManager> encryptionManager,
        std::shared_ptr<DecryptedTextManager> decryptedTextManager);

    void start();

Q_SIGNALS:
    void finished(
        QString encryptedText, QString cipher, size_t length, QString hint,
        QString decryptedText, QString passphrase, bool rememberForSession,
        bool decryptPermanently);

    void cancelled();
    void notifyError(ErrorString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);

    void onEncryptedTextDecrypted(
        QString cipher, size_t keyLength, QString encryptedText,
        QString passphrase, QString decryptedText, bool rememberForSession,
        bool decryptPermanently);

    void onDecryptionScriptFinished(const QVariant & data);

private:
    void raiseDecryptionDialog();

private:
    typedef JsResultCallbackFunctor<DecryptEncryptedTextDelegate> JsCallback;

private:
    QString m_encryptedTextId;
    QString m_encryptedText;
    QString m_cipher;
    size_t m_length = 0;
    QString m_hint;
    QString m_decryptedText;
    QString m_passphrase;
    bool m_rememberForSession = false;
    bool m_decryptPermanently = false;

    QPointer<NoteEditorPrivate> m_pNoteEditor;
    std::shared_ptr<EncryptionManager> m_encryptionManager;
    std::shared_ptr<DecryptedTextManager> m_decryptedTextManager;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_DECRYPT_ENCRYPTED_TEXT_DELEGATE_H
