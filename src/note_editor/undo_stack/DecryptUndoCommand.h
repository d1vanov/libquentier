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

#pragma once

#include "EncryptDecryptUndoCommandInfo.h"

#include "INoteEditorUndoCommand.h"

#include "../NoteEditorPage.h"

#include <quentier/enml/Fwd.h>

#include <memory>

namespace quentier {

class DecryptUndoCommand final : public INoteEditorUndoCommand
{
    Q_OBJECT
    using Callback = NoteEditorPage::Callback;
public:
    DecryptUndoCommand(
        EncryptDecryptUndoCommandInfo info,
        enml::IDecryptedTextCachePtr decryptedTextCache,
        NoteEditorPrivate & noteEditorPrivate, Callback callback,
        QUndoCommand * parent = nullptr);

    DecryptUndoCommand(
        EncryptDecryptUndoCommandInfo info,
        enml::IDecryptedTextCachePtr decryptedTextCache,
        NoteEditorPrivate & noteEditorPrivate, Callback callback,
        const QString & text, QUndoCommand * parent = nullptr);

    ~DecryptUndoCommand() noexcept override;

    void redoImpl() override;
    void undoImpl() override;

private:
    EncryptDecryptUndoCommandInfo m_info;
    const enml::IDecryptedTextCachePtr m_decryptedTextCache;
    Callback m_callback;
};

} // namespace quentier
