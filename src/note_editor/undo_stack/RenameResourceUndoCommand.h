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

#include <qevercloud/types/Resource.h>

#include <QHash>

namespace quentier {

class GenericResourceImageManager;

class RenameResourceUndoCommand final : public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    RenameResourceUndoCommand(
        qevercloud::Resource resource, QString previousResourceName,
        NoteEditorPrivate & noteEditor,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> &
            genericResourceImageFilePathsByResourceHash,
        QUndoCommand * parent = nullptr);

    RenameResourceUndoCommand(
        qevercloud::Resource resource, QString previousResourceName,
        NoteEditorPrivate & noteEditor,
        GenericResourceImageManager * pGenericResourceImageManager,
        QHash<QByteArray, QString> &
            genericResourceImageFilePathsByResourceHash,
        const QString & text, QUndoCommand * parent = nullptr);

    ~RenameResourceUndoCommand() noexcept override;

    void undoImpl() override;
    void redoImpl() override;

private:
    qevercloud::Resource m_resource;
    QString m_previousResourceName;
    QString m_newResourceName;
    GenericResourceImageManager * m_pGenericResourceImageManager;
    QHash<QByteArray, QString> & m_genericResourceImageFilePathsByResourceHash;
};

} // namespace quentier
