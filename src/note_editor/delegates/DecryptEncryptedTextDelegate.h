/*
 * Copyright 2016-2023 Dmitry Ivanov
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

#pragma once

#include "JsResultCallbackFunctor.hpp"

#include <quentier/enml/Fwd.h>
#include <quentier/types/ErrorString.h>

#include <qevercloud/types/Note.h>

#include <QPointer>

#include <cstddef>
#include <memory>

namespace quentier {

class EncryptionManager;
class NoteEditorPrivate;

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
        QString encryptedTextId, QString encryptedText, QString cipher,
        const QString & length, QString hint, NoteEditorPrivate * pNoteEditor,
        std::shared_ptr<EncryptionManager> encryptionManager,
        enml::IDecryptedTextCachePtr decryptedTextCache,
        enml::IENMLTagsConverterPtr enmlTagsConverter);

    void start();

Q_SIGNALS:
    void finished(
        QString encryptedText, QString cipher, size_t length, QString hint,
        QString decryptedText, QString passphrase, bool rememberForSession,
        bool decryptPermanently);

    void cancelled();
    void notifyError(ErrorString error);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(qevercloud::Note note);

    void onEncryptedTextDecrypted(
        QString cipher, size_t keyLength, QString encryptedText,
        QString passphrase, QString decryptedText, bool rememberForSession,
        bool decryptPermanently);

    void onDecryptionScriptFinished(const QVariant & data);

private:
    void raiseDecryptionDialog();

private:
    using JsCallback = JsResultCallbackFunctor<DecryptEncryptedTextDelegate>;

private:
    const std::shared_ptr<EncryptionManager> m_encryptionManager;
    const enml::IDecryptedTextCachePtr m_decryptedTextCache;
    const enml::IENMLTagsConverterPtr m_enmlTagsConverter;

    QString m_encryptedTextId;
    QString m_encryptedText;
    QString m_cipher;
    std::size_t m_length = 0;
    QString m_hint;
    QString m_decryptedText;
    QString m_passphrase;
    bool m_rememberForSession = false;
    bool m_decryptPermanently = false;

    QPointer<NoteEditorPrivate> m_pNoteEditor;
};

} // namespace quentier
