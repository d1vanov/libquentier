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

#include "RenameResourceUndoCommand.h"

#include "../GenericResourceImageManager.h"
#include "../NoteEditor_p.h"

#include "../delegates/RenameResourceDelegate.h"

namespace quentier {

RenameResourceUndoCommand::RenameResourceUndoCommand(
    const Resource & resource, const QString & previousResourceName,
    NoteEditorPrivate & noteEditor,
    GenericResourceImageManager * pGenericResourceImageManager,
    QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_resource(resource), m_previousResourceName(previousResourceName),
    m_newResourceName(resource.displayName()),
    m_pGenericResourceImageManager(pGenericResourceImageManager),
    m_genericResourceImageFilePathsByResourceHash(
        genericResourceImageFilePathsByResourceHash)
{
    setText(tr("Rename attachment"));
}

RenameResourceUndoCommand::RenameResourceUndoCommand(
    const Resource & resource, const QString & previousResourceName,
    NoteEditorPrivate & noteEditor,
    GenericResourceImageManager * pGenericResourceImageManager,
    QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash,
    const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_resource(resource), m_previousResourceName(previousResourceName),
    m_newResourceName(resource.displayName()),
    m_pGenericResourceImageManager(pGenericResourceImageManager),
    m_genericResourceImageFilePathsByResourceHash(
        genericResourceImageFilePathsByResourceHash)
{}

RenameResourceUndoCommand::~RenameResourceUndoCommand() {}

void RenameResourceUndoCommand::undoImpl()
{
    auto * delegate = new RenameResourceDelegate(
        m_resource, m_noteEditorPrivate, m_pGenericResourceImageManager,
        m_genericResourceImageFilePathsByResourceHash,
        /* performing undo = */ true);

    m_noteEditorPrivate.setRenameResourceDelegateSubscriptions(*delegate);
    delegate->startWithPresetNames(m_newResourceName, m_previousResourceName);
}

void RenameResourceUndoCommand::redoImpl()
{
    auto * delegate = new RenameResourceDelegate(
        m_resource, m_noteEditorPrivate, m_pGenericResourceImageManager,
        m_genericResourceImageFilePathsByResourceHash,
        /* performing undo = */ true);

    m_noteEditorPrivate.setRenameResourceDelegateSubscriptions(*delegate);
    delegate->startWithPresetNames(m_previousResourceName, m_newResourceName);
}

} // namespace quentier
