/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_RENAME_RESOURCE_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_RENAME_RESOURCE_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"

#include <quentier/types/Resource.h>

#include <QHash>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(GenericResourceImageManager)

class Q_DECL_HIDDEN RenameResourceUndoCommand: public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    RenameResourceUndoCommand(
        const Resource & resource,
        const QString & previousResourceName,
        NoteEditorPrivate & noteEditor,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash,
        QUndoCommand * parent = Q_NULLPTR);

    RenameResourceUndoCommand(
        const Resource & resource,
        const QString & previousResourceName,
        NoteEditorPrivate & noteEditor,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> & genericResourceImageFilePathsByResourceHash,
        const QString & text,  QUndoCommand * parent = Q_NULLPTR);

    virtual ~RenameResourceUndoCommand();

    virtual void undoImpl() Q_DECL_OVERRIDE;
    virtual void redoImpl() Q_DECL_OVERRIDE;

private:
    Resource                        m_resource;
    QString                         m_previousResourceName;
    QString                         m_newResourceName;
    GenericResourceImageManager *   m_pGenericResourceImageManager;
    QHash<QByteArray, QString> &    m_genericResourceImageFilePathsByResourceHash;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_RENAME_RESOURCE_UNDO_COMMAND_H
