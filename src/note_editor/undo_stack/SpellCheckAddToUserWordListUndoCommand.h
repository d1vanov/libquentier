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

#include "INoteEditorUndoCommand.h"

#include <QPointer>

namespace quentier {

class SpellChecker;

class SpellCheckAddToUserWordListUndoCommand final :
    public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    SpellCheckAddToUserWordListUndoCommand(
        NoteEditorPrivate & noteEditor, QString word,
        SpellChecker * pSpellChecker, QUndoCommand * parent = nullptr);

    SpellCheckAddToUserWordListUndoCommand(
        NoteEditorPrivate & noteEditor, QString word,
        SpellChecker * pSpellChecker, const QString & text,
        QUndoCommand * parent = nullptr);

    ~SpellCheckAddToUserWordListUndoCommand() noexcept override;

    void redoImpl() override;
    void undoImpl() override;

private:
    QPointer<SpellChecker> m_pSpellChecker;
    QString m_word;
};

} // namespace quentier
