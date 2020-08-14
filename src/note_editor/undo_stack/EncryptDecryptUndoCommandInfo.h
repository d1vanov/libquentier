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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_ENCRYPT_DECRYPT_UNDO_COMMAND_INFO_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_ENCRYPT_DECRYPT_UNDO_COMMAND_INFO_H

#include <QString>

namespace quentier {

struct Q_DECL_HIDDEN EncryptDecryptUndoCommandInfo
{
    QString m_encryptedText;
    QString m_decryptedText;
    QString m_passphrase;
    QString m_cipher;
    QString m_hint;
    size_t m_keyLength = 0;
    bool m_rememberForSession = false;
    bool m_decryptPermanently = false;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_ENCRYPT_DECRYPT_UNDO_COMMAND_INFO_H
