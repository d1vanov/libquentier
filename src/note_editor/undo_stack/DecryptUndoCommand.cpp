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

#include "DecryptUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditorPrivate.page());  \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "DecryptUndoCommand",                                              \
            "Can't undo/redo the encrypted text "                              \
            "decryption: no note editor page"));                               \
        QNWARNING("note_editor:undo", error);                                  \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

DecryptUndoCommand::DecryptUndoCommand(
    const EncryptDecryptUndoCommandInfo & info,
    std::shared_ptr<DecryptedTextManager> decryptedTextManager,
    NoteEditorPrivate & noteEditorPrivate, const Callback & callback,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, parent),
    m_info(info), m_decryptedTextManager(std::move(decryptedTextManager)),
    m_callback(callback)
{
    setText(tr("Decrypt text"));
}

DecryptUndoCommand::DecryptUndoCommand(
    const EncryptDecryptUndoCommandInfo & info,
    std::shared_ptr<DecryptedTextManager> decryptedTextManager,
    NoteEditorPrivate & noteEditorPrivate, const Callback & callback,
    const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditorPrivate, text, parent),
    m_info(info), m_decryptedTextManager(std::move(decryptedTextManager)),
    m_callback(callback)
{}

DecryptUndoCommand::~DecryptUndoCommand() {}

void DecryptUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "DecryptUndoCommand::redoImpl");

    GET_PAGE()

    if (!m_info.m_decryptPermanently) {
        m_decryptedTextManager->addEntry(
            m_info.m_encryptedText, m_info.m_decryptedText,
            m_info.m_rememberForSession, m_info.m_passphrase, m_info.m_cipher,
            m_info.m_keyLength);
    }

    page->executeJavaScript(
        QStringLiteral("encryptDecryptManager.redo();"), m_callback);
}

void DecryptUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "DecryptUndoCommand::undoImpl");

    GET_PAGE()

    if (!m_info.m_decryptPermanently) {
        m_decryptedTextManager->removeEntry(m_info.m_encryptedText);
    }

    page->executeJavaScript(
        QStringLiteral("encryptDecryptManager.undo();"), m_callback);
}

} // namespace quentier
