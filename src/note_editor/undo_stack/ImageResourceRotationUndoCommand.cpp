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

#include "ImageResourceRotationUndoCommand.h"

#include <quentier/logging/QuentierLogger.h>

#include <limits>

namespace quentier {

ImageResourceRotationUndoCommand::ImageResourceRotationUndoCommand(
    QByteArray resourceDataBefore, QByteArray resourceHashBefore,
    QByteArray resourceRecognitionDataBefore,
    QByteArray resourceRecognitionDataHashBefore, QSize resourceImageSizeBefore,
    qevercloud::Resource resourceAfter,
    const INoteEditorBackend::Rotation rotationDirection,
    NoteEditorPrivate & noteEditor, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_resourceDataBefore(std::move(resourceDataBefore)),
    m_resourceHashBefore(std::move(resourceHashBefore)),
    m_resourceRecognitionDataBefore(std::move(resourceRecognitionDataBefore)),
    m_resourceRecognitionDataHashBefore(
        std::move(resourceRecognitionDataHashBefore)),
    m_resourceImageSizeBefore(resourceImageSizeBefore),
    m_resourceAfter(std::move(resourceAfter)),
    m_rotationDirection(rotationDirection)
{
    setText(
        QObject::tr("Image resource rotation") + QStringLiteral(" ") +
        ((m_rotationDirection == INoteEditorBackend::Rotation::Clockwise)
             ? QObject::tr("clockwise")
             : QObject::tr("counterclockwise")));
}

ImageResourceRotationUndoCommand::ImageResourceRotationUndoCommand(
    QByteArray resourceDataBefore, QByteArray resourceHashBefore,
    QByteArray resourceRecognitionDataBefore,
    QByteArray resourceRecognitionDataHashBefore, QSize resourceImageSizeBefore,
    qevercloud::Resource resourceAfter,
    const INoteEditorBackend::Rotation rotationDirection,
    NoteEditorPrivate & noteEditor, const QString & text,
    QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_resourceDataBefore(std::move(resourceDataBefore)),
    m_resourceHashBefore(std::move(resourceHashBefore)),
    m_resourceRecognitionDataBefore(std::move(resourceRecognitionDataBefore)),
    m_resourceRecognitionDataHashBefore(
        std::move(resourceRecognitionDataHashBefore)),
    m_resourceImageSizeBefore(resourceImageSizeBefore),
    m_resourceAfter(std::move(resourceAfter)),
    m_rotationDirection(rotationDirection)
{}

ImageResourceRotationUndoCommand::~ImageResourceRotationUndoCommand() noexcept =
    default;

void ImageResourceRotationUndoCommand::redoImpl()
{
    QNDEBUG("note_editor:undo", "ImageResourceRotationUndoCommand::redoImpl");

    const auto * pNote = m_noteEditorPrivate.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNDEBUG(
            "note_editor:undo",
            "Can't redo image resource rotation: "
                << "no note is set to the editor");
        return;
    }

    m_noteEditorPrivate.updateResource(
        m_resourceAfter.localId(), m_resourceHashBefore, m_resourceAfter);
}

void ImageResourceRotationUndoCommand::undoImpl()
{
    QNDEBUG("note_editor:undo", "ImageResourceRotationUndoCommand::undoImpl");

    const auto * pNote = m_noteEditorPrivate.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNDEBUG(
            "note_editor:undo",
            "Can't undo image resource rotation: "
                << "no note is set to the editor");
        return;
    }

    qevercloud::Resource resource(m_resourceAfter);
    if (!m_resourceDataBefore.isEmpty()) {
        if (!resource.data()) {
            resource.setData(qevercloud::Data{});
        }

        resource.mutableData()->setBody(m_resourceDataBefore);
        resource.mutableData()->setSize(m_resourceDataBefore.size());
        resource.mutableData()->setBodyHash(m_resourceHashBefore);
    }
    else {
        resource.setData(std::nullopt);
    }

    if (!m_resourceRecognitionDataBefore.isEmpty()) {
        if (!resource.recognition()) {
            resource.setRecognition(qevercloud::Data{});
        }

        resource.mutableRecognition()->setBody(m_resourceRecognitionDataBefore);

        resource.mutableRecognition()->setBodyHash(
            m_resourceRecognitionDataHashBefore);

        resource.mutableRecognition()->setSize(
            m_resourceRecognitionDataBefore.size());
    }
    else {
        resource.setRecognition(std::nullopt);
    }

    if (m_resourceImageSizeBefore.isValid()) {
        const int height = m_resourceImageSizeBefore.height();
        const int width = m_resourceImageSizeBefore.width();
        if ((height > 0) && (height < std::numeric_limits<qint16>::max()) &&
            (width > 0) && (width < std::numeric_limits<qint16>::max()))
        {
            resource.setHeight(static_cast<qint16>(height));
            resource.setWidth(static_cast<qint16>(width));
        }
    }

    const QByteArray resourceAfterDataHash =
        (m_resourceAfter.data() && m_resourceAfter.data()->bodyHash())
        ? *m_resourceAfter.data()->bodyHash()
        : QByteArray();

    m_noteEditorPrivate.updateResource(
        resource.localId(), resourceAfterDataHash, resource);
}

} // namespace quentier
