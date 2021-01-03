/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#include <QHash>
#include <QStringList>

namespace qevercloud {

class Resource;

} // namespace qevercloud

namespace quentier {

class ResourceInfo;

class Q_DECL_HIDDEN InsertHtmlUndoCommand final : public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    using Callback = NoteEditorPage::Callback;

public:
    InsertHtmlUndoCommand(
        const Callback & callback, NoteEditorPrivate & noteEditor,
        QHash<QString, QString> & resourceFileStoragePathsByResourceLocalId,
        ResourceInfo & resourceInfo,
        const QList<qevercloud::Resource> & addedResources = {},
        const QStringList & resourceFileStoragePaths = {},
        QUndoCommand * parent = nullptr);

    InsertHtmlUndoCommand(
        const Callback & callback, NoteEditorPrivate & noteEditor,
        QHash<QString, QString> & resourceFileStoragePathsByResourceLocalId,
        ResourceInfo & resourceInfo, const QString & text,
        const QList<qevercloud::Resource> & addedResources = {},
        const QStringList & resourceFileStoragePaths = {},
        QUndoCommand * parent = nullptr);

    ~InsertHtmlUndoCommand() noexcept override;

    void undoImpl() override;
    void redoImpl() override;

private:
    QList<qevercloud::Resource> m_addedResources;
    QStringList m_resourceFileStoragePaths;
    Callback m_callback;

    QHash<QString, QString> & m_resourceFileStoragePathsByResourceLocalId;
    ResourceInfo & m_resourceInfo;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_INSERT_HTML_UNDO_COMMAND_H
