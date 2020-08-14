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

#ifndef LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_IMAGE_RESOURCE_ROTATION_UNDO_COMMAND_H
#define LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_IMAGE_RESOURCE_ROTATION_UNDO_COMMAND_H

#include "INoteEditorUndoCommand.h"

#include "../NoteEditor_p.h"

#include <quentier/types/Resource.h>

#include <QSize>

namespace quentier {

class Q_DECL_HIDDEN ImageResourceRotationUndoCommand final :
    public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    ImageResourceRotationUndoCommand(
        const QByteArray & resourceDataBefore,
        const QByteArray & resourceHashBefore,
        const QByteArray & resourceRecognitionDataBefore,
        const QByteArray & resourceRecognitionDataHashBefore,
        const QSize & resourceImageSizeBefore, const Resource & resourceAfter,
        const INoteEditorBackend::Rotation rotationDirection,
        NoteEditorPrivate & noteEditor, QUndoCommand * parent = nullptr);

    ImageResourceRotationUndoCommand(
        const QByteArray & resourceDataBefore,
        const QByteArray & resourceHashBefore,
        const QByteArray & resourceRecognitionDataBefore,
        const QByteArray & resourceRecognitionDataHashBefore,
        const QSize & resourceImageSizeBefore, const Resource & resourceAfter,
        const INoteEditorBackend::Rotation rotationDirection,
        NoteEditorPrivate & noteEditor, const QString & text,
        QUndoCommand * parent = nullptr);

    virtual ~ImageResourceRotationUndoCommand();

    virtual void redoImpl() override;
    virtual void undoImpl() override;

private:
    const QByteArray m_resourceDataBefore;
    const QByteArray m_resourceHashBefore;
    const QByteArray m_resourceRecognitionDataBefore;
    const QByteArray m_resourceRecognitionDataHashBefore;
    const QSize m_resourceImageSizeBefore;
    const Resource m_resourceAfter;
    const INoteEditorBackend::Rotation m_rotationDirection;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_UNDO_STACK_IMAGE_RESOURCE_ROTATION_UNDO_COMMAND_H
