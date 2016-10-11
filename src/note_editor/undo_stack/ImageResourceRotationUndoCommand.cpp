#include "ImageResourceRotationUndoCommand.h"
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/DesktopServices.h>

namespace quentier {

ImageResourceRotationUndoCommand::ImageResourceRotationUndoCommand(const QByteArray & resourceDataBefore, const QByteArray & resourceHashBefore,
                                                                   const QByteArray & resourceRecognitionDataBefore, const QByteArray & resourceRecognitionDataHashBefore,
                                                                   const Resource & resourceAfter, const INoteEditorBackend::Rotation::type rotationDirection,
                                                                   NoteEditorPrivate & noteEditor, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, parent),
    m_resourceDataBefore(resourceDataBefore),
    m_resourceHashBefore(resourceHashBefore),
    m_resourceRecognitionDataBefore(resourceRecognitionDataBefore),
    m_resourceRecognitionDataHashBefore(resourceRecognitionDataHashBefore),
    m_resourceAfter(resourceAfter),
    m_rotationDirection(rotationDirection)
{
    setText(QObject::tr("Image resource rotation") + (m_rotationDirection == INoteEditorBackend::Rotation::Clockwise
                                                      ? QObject::tr("clockwise")
                                                      : QObject::tr("counterclockwise")));
}

ImageResourceRotationUndoCommand::ImageResourceRotationUndoCommand(const QByteArray & resourceDataBefore, const QByteArray & resourceHashBefore,
                                                                   const QByteArray & resourceRecognitionDataBefore, const QByteArray & resourceRecognitionDataHashBefore,
                                                                   const Resource & resourceAfter, const INoteEditorBackend::Rotation::type rotationDirection,
                                                                   NoteEditorPrivate & noteEditor, const QString & text, QUndoCommand * parent) :
    INoteEditorUndoCommand(noteEditor, text, parent),
    m_resourceDataBefore(resourceDataBefore),
    m_resourceHashBefore(resourceHashBefore),
    m_resourceRecognitionDataBefore(resourceRecognitionDataBefore),
    m_resourceRecognitionDataHashBefore(resourceRecognitionDataHashBefore),
    m_resourceAfter(resourceAfter),
    m_rotationDirection(rotationDirection)
{}

ImageResourceRotationUndoCommand::~ImageResourceRotationUndoCommand()
{}

void ImageResourceRotationUndoCommand::redoImpl()
{
    QNDEBUG(QStringLiteral("ImageResourceRotationUndoCommand::redoImpl"));

    const Note * pNote = m_noteEditorPrivate.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNDEBUG(QStringLiteral("Can't redo image resource rotation: no note set to the editor"));
        return;
    }

    m_noteEditorPrivate.updateResource(m_resourceAfter.localUid(), m_resourceHashBefore, m_resourceAfter);
}

void ImageResourceRotationUndoCommand::undoImpl()
{
    QNDEBUG(QStringLiteral("ImageResourceRotationUndoCommand::undoImpl"));

    const Note * pNote = m_noteEditorPrivate.notePtr();
    if (Q_UNLIKELY(!pNote)) {
        QNDEBUG(QStringLiteral("Can't undo image resource rotation: no note set to the editor"));
        return;
    }

    Resource resource(m_resourceAfter);
    resource.setDataBody(m_resourceDataBefore);
    resource.setDataSize(m_resourceDataBefore.size());
    resource.setRecognitionDataBody(m_resourceRecognitionDataBefore);
    resource.setRecognitionDataHash(m_resourceRecognitionDataHashBefore);
    if (!m_resourceRecognitionDataBefore.isEmpty()) {
        resource.setRecognitionDataSize(m_resourceRecognitionDataBefore.size());
    }

    m_noteEditorPrivate.updateResource(resource.localUid(), m_resourceAfter.dataHash(), resource);
}

} // namespace quentier
