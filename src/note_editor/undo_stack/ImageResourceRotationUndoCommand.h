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

#include "../NoteEditor_p.h"

#include <qevercloud/types/Resource.h>

#include <QSize>

namespace quentier {

class ImageResourceRotationUndoCommand final : public INoteEditorUndoCommand
{
    Q_OBJECT
public:
    ImageResourceRotationUndoCommand(
        QByteArray resourceDataBefore,
        QByteArray resourceHashBefore,
        QByteArray resourceRecognitionDataBefore,
        QByteArray resourceRecognitionDataHashBefore,
        QSize resourceImageSizeBefore,
        qevercloud::Resource resourceAfter,
        INoteEditorBackend::Rotation rotationDirection,
        NoteEditorPrivate & noteEditor, QUndoCommand * parent = nullptr);

    ImageResourceRotationUndoCommand(
        QByteArray resourceDataBefore,
        QByteArray resourceHashBefore,
        QByteArray resourceRecognitionDataBefore,
        QByteArray resourceRecognitionDataHashBefore,
        QSize resourceImageSizeBefore,
        qevercloud::Resource resourceAfter,
        INoteEditorBackend::Rotation rotationDirection,
        NoteEditorPrivate & noteEditor, const QString & text,
        QUndoCommand * parent = nullptr);

    ~ImageResourceRotationUndoCommand() noexcept override;

    void redoImpl() override;
    void undoImpl() override;

private:
    const QByteArray m_resourceDataBefore;
    const QByteArray m_resourceHashBefore;
    const QByteArray m_resourceRecognitionDataBefore;
    const QByteArray m_resourceRecognitionDataHashBefore;
    const QSize m_resourceImageSizeBefore;
    const qevercloud::Resource m_resourceAfter;
    const INoteEditorBackend::Rotation m_rotationDirection;
};

} // namespace quentier
