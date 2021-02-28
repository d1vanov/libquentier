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

#include "ImageResourceRotationUndoCommand.h"

#include <quentier/logging/QuentierLogger.h>

#include <limits>

namespace quentier {

ImageResourceRotationUndoCommand::ImageResourceRotationUndoCommand(
    const QByteArray & resourceDataBefore,
    const QByteArray & resourceHashBefore,
    const QByteArray & resourceRecognitionDataBefore,
    const QByteArray & resourceRecognitionDataHashBefore,
    const QSize & resourceImageSizeBefore, const Resource & resourceAfter,
    const INoteEditorBackend::Rotation rotationDirection,
    NoteEditorPrivate & noteEditor, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_resourceDataBefore(resourceDataBefore),
    m_resourceHashBefore(resourceHashBefore),
    m_resourceRecognitionDataBefore(resourceRecognitionDataBefore),
    m_resourceRecognitionDataHashBefore(resourceRecognitionDataHashBefore),
    m_resourceImageSizeBefore(resourceImageSizeBefore),
    m_resourceAfter(resourceAfter), m_rotationDirection(rotationDirection)
{
    setText(
        QObject::tr("Image resource rotation") + QStringLiteral(" ") +
        ((m_rotationDirection == INoteEditorBackend::Rotation::Clockwise)
             ? QObject::tr("clockwise")
             : QObject::tr("counterclockwise")));
}

ImageResourceRotationUndoCommand::ImageResourceRotationUndoCommand(
    const QByteArray & resourceDataBefore,
    const QByteArray & resourceHashBefore,
    const QByteArray & resourceRecognitionDataBefore,
    const QByteArray & resourceRecognitionDataHashBefore,
    const QSize & resourceImageSizeBefore, const Resource & resourceAfter,
    const INoteEditorBackend::Rotation rotationDirection,
    NoteEditorPrivate & noteEditor, const QString & text,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_resourceDataBefore(resourceDataBefore),
    m_resourceHashBefore(resourceHashBefore),
    m_resourceRecognitionDataBefore(resourceRecognitionDataBefore),
    m_resourceRecognitionDataHashBefore(resourceRecognitionDataHashBefore),
    m_resourceImageSizeBefore(resourceImageSizeBefore),
    m_resourceAfter(resourceAfter), m_rotationDirection(rotationDirection)
{}

ImageResourceRotationUndoCommand::~ImageResourceRotationUndoCommand() {}

void ImageResourceRotationUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "ImageResourceRotationUndoCommand::redoImpl");

    const Note * pNote = m_noteEditorPrivate.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNDEBUG(
            "note_editor:undo",
            "Can't redo image resource rotation: "
                << "no note is set to the editor");
        return;
    }

    m_noteEditorPrivate.updateResource(
        m_resourceAfter.localUid(), m_resourceHashBefore, m_resourceAfter);
}

void ImageResourceRotationUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "ImageResourceRotationUndoCommand::undoImpl");

    const Note * pNote = m_noteEditorPrivate.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNDEBUG(
            "note_editor:undo",
            "Can't undo image resource rotation: "
                << "no note is set to the editor");
        return;
    }

    Resource resource(m_resourceAfter);
    resource.setDataBody(m_resourceDataBefore);
    resource.setDataSize(m_resourceDataBefore.size());
    resource.setDataHash(m_resourceHashBefore);
    resource.setRecognitionDataBody(m_resourceRecognitionDataBefore);
    resource.setRecognitionDataHash(m_resourceRecognitionDataHashBefore);

    if (m_resourceImageSizeBefore.isValid()) {
        int height = m_resourceImageSizeBefore.height();
        int width = m_resourceImageSizeBefore.width();
        if ((height > 0) && (height < std::numeric_limits<qint16>::max()) &&
            (width > 0) && (width < std::numeric_limits<qint16>::max()))
        {
            resource.setHeight(static_cast<qint16>(height));
            resource.setWidth(static_cast<qint16>(width));
        }
    }

    if (!m_resourceRecognitionDataBefore.isEmpty()) {
        resource.setRecognitionDataSize(m_resourceRecognitionDataBefore.size());
    }

    m_noteEditorPrivate.updateResource(
        resource.localUid(), m_resourceAfter.dataHash(), resource);
}

} // namespace quentier
