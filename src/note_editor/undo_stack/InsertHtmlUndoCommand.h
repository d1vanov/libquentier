/*
 * Copyright 2017-2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_INSERT_HTML_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_INSERT_HTML_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"
#include "../NoteEditorPage.h"

#include <quentier/utility/Macros.h>
#include <quentier/types/Resource.h>

#include <QStringList>
#include <QHash>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(ResourceInfo)

class Q_DECL_HIDDEN InsertHtmlUndoCommand: public INoteEditorUndoCommand
{
    Q_OBJECT
    typedef NoteEditorPage::Callback Callback;
public:
    InsertHtmlUndoCommand(
        const Callback & callback, NoteEditorPrivate & noteEditor,
        QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
        ResourceInfo & resourceInfo,
        const QList<Resource> & addedResources = QList<Resource>(),
        const QStringList & resourceFileStoragePaths = QStringList(),
        QUndoCommand * parent = nullptr);

    InsertHtmlUndoCommand(
        const Callback & callback, NoteEditorPrivate & noteEditor,
        QHash<QString, QString> & resourceFileStoragePathsByResourceLocalUid,
        ResourceInfo & resourceInfo,
        const QString & text,
        const QList<Resource> & addedResources = QList<Resource>(),
        const QStringList & resourceFileStoragePaths = QStringList(),
        QUndoCommand * parent = nullptr);

    virtual ~InsertHtmlUndoCommand();

    virtual void undoImpl() override;
    virtual void redoImpl() override;

private:
    QList<Resource>     m_addedResources;
    QStringList         m_resourceFileStoragePaths;
    Callback            m_callback;

    QHash<QString, QString> &       m_resourceFileStoragePathsByResourceLocalUid;
    ResourceInfo &                  m_resourceInfo;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_INSERT_HTML_UNDO_COMMAND_H
