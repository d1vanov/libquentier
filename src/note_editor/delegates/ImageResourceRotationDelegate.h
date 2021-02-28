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

#ifndef LIB_QUENTIER_NOTE_EDITOR_DELEGATES_IMAGE_RESOURCE_ROTATION_DELEGATE_H
#define LIB_QUENTIER_NOTE_EDITOR_DELEGATES_IMAGE_RESOURCE_ROTATION_DELEGATE_H

#include "../NoteEditor_p.h"

#include "JsResultCallbackFunctor.hpp"

#include <quentier/types/Resource.h>

#include <QSize>

namespace quentier {

class Q_DECL_HIDDEN ImageResourceRotationDelegate final : public QObject
{
    Q_OBJECT
public:
    explicit ImageResourceRotationDelegate(
        const QByteArray & resourceHashBefore,
        const INoteEditorBackend::Rotation rotationDirection,
        NoteEditorPrivate & noteEditor, ResourceInfo & resourceInfo,
        ResourceDataInTemporaryFileStorageManager &
            resourceDataInTemporaryFileStorageManager,
        QHash<QString, QString> & resourceFileStoragePathsByLocalUid);

    void start();

Q_SIGNALS:
    void finished(
        QByteArray resourceDataBefore, QByteArray resourceHashBefore,
        QByteArray resourceRecognitionDataBefore,
        QByteArray resourceRecognitionDataHashBefore,
        QSize resourceImageSizeBefore, Resource resourceAfter,
        INoteEditorBackend::Rotation rotationDirection);

    void notifyError(ErrorString error);

    // private signals
    void saveResourceDataToTemporaryFile(
        QString noteLocalUid, QString resourceLocalUid, QByteArray data,
        QByteArray dataHash, QUuid requestId, bool isImage);

private Q_SLOTS:
    void onOriginalPageConvertedToNote(Note note);

    void onResourceDataSavedToTemporaryFile(
        QUuid requestId, QByteArray dataHash, ErrorString errorDescription);

    void onResourceTagHashUpdated(const QVariant & data);
    void onResourceTagSrcUpdated(const QVariant & data);

private:
    void rotateImageResource();

private:
    using JsCallback = JsResultCallbackFunctor<ImageResourceRotationDelegate>;

private:
    NoteEditorPrivate & m_noteEditor;
    ResourceInfo & m_resourceInfo;
    ResourceDataInTemporaryFileStorageManager &
        m_resourceDataInTemporaryFileStorageManager;
    QHash<QString, QString> & m_resourceFileStoragePathsByLocalUid;

    INoteEditorBackend::Rotation m_rotationDirection;

    Note * m_pNote = nullptr;

    QByteArray m_resourceDataBefore;
    QByteArray m_resourceHashBefore;
    QSize m_resourceImageSizeBefore;

    QByteArray m_resourceRecognitionDataBefore;
    QByteArray m_resourceRecognitionDataHashBefore;

    QString m_resourceFileStoragePathBefore;
    QString m_resourceFileStoragePathAfter;

    Resource m_rotatedResource;
    QUuid m_saveResourceDataToTemporaryFileRequestId;
};

} // namespace quentier

#endif // LIB_QUENTIER_NOTE_EDITOR_DELEGATES_IMAGE_RESOURCE_ROTATION_DELEGATE_H
