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

#include "DecryptUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/enml/IDecryptedTextCache.h>
#include <quentier/exception/InvalidArgument.h>
#include <quentier/logging/QuentierLogger.h>

namespace quentier {

DecryptUndoCommand::DecryptUndoCommand(
    EncryptDecryptUndoCommandInfo info,
    enml::IDecryptedTextCachePtr decryptedTextCache,
    NoteEditorPrivate & noteEditorPrivate, Callback callback,
    QUndoCommand * parent) :
    DecryptUndoCommand(
        std::move(info), std::move(decryptedTextCache), noteEditorPrivate,
        std::move(callback), tr("Decrypt text"), parent)
{}

DecryptUndoCommand::DecryptUndoCommand(
    EncryptDecryptUndoCommandInfo info,
    enml::IDecryptedTextCachePtr decryptedTextCache,
    NoteEditorPrivate & noteEditorPrivate, Callback callback,
    const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_info(std::move(info)),
    m_decryptedTextCache(std::move(decryptedTextCache)),
    m_callback(std::move(callback))
{
    if (Q_UNLIKELY(!m_decryptedTextCache)) {
        throw InvalidArgument{ErrorString{
            "DecryptUndoCommand ctor: decrypted text cache is null"}};
    }
}

DecryptUndoCommand::~DecryptUndoCommand() noexcept = default;

void DecryptUndoCommand::redoImpl()
{
    QNDEBUG("note_editor::DecryptUndoCommand", "DecryptUndoCommand::redoImpl");

    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());
    if (Q_UNLIKELY(!page)) {
        ErrorString error{QT_TR_NOOP(
            "Can'redo encrypted text decryption: no note editor page")};
        QNWARNING("note_editor:::DecryptUndoCommand", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!m_info.m_decryptPermanently) {
        m_decryptedTextCache->addDecryptexTextInfo(
            m_info.m_encryptedText, m_info.m_decryptedText, m_info.m_passphrase,
            m_info.m_cipher, m_info.m_keyLength,
            m_info.m_rememberForSession
                ? enml::IDecryptedTextCache::RememberForSession::Yes
                : enml::IDecryptedTextCache::RememberForSession::No);
    }

    page->executeJavaScript(
        QStringLiteral("encryptDecryptManager.redo();"), m_callback);
}

void DecryptUndoCommand::undoImpl()
{
    QNDEBUG("note_editor::DecryptUndoCommand", "DecryptUndoCommand::undoImpl");

    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());
    if (Q_UNLIKELY(!page)) {
        ErrorString error{QT_TR_NOOP(
            "Can'undo encrypted text decryption: no note editor page")};
        QNWARNING("note_editor:::DecryptUndoCommand", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (!m_info.m_decryptPermanently) {
        m_decryptedTextCache->removeDecryptedTextInfo(m_info.m_encryptedText);
    }

    page->executeJavaScript(
        QStringLiteral("encryptDecryptManager.undo();"), m_callback);
}

} // namespace quentier
