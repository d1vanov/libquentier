/*
 * Copyright 2016-2021 Dmitry Ivanov
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

#include "ImageResourceRotationDelegate.h"

#include "../ResourceDataInTemporaryFileStorageManager.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/ResourceUtils.h>
#include <quentier/utility/Size.h>

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>

#include <algorithm>
#include <limits>

namespace quentier {

#define GET_PAGE()                                                             \
    auto * page = qobject_cast<NoteEditorPage *>(m_noteEditor.page());         \
    if (Q_UNLIKELY(!page)) {                                                   \
        ErrorString error(QT_TRANSLATE_NOOP(                                   \
            "ImageResourceRotationDelegate",                                   \
            "Can't rotate the image attachment: "                              \
            "no note editor page"));                                           \
        QNWARNING("note_editor:delegate", error);                              \
        Q_EMIT notifyError(error);                                             \
        return;                                                                \
    }

ImageResourceRotationDelegate::ImageResourceRotationDelegate(
    const QByteArray & resourceHashBefore,
    const INoteEditorBackend::Rotation rotationDirection,
    NoteEditorPrivate & noteEditor, ResourceInfo & resourceInfo,
    ResourceDataInTemporaryFileStorageManager &
        resourceDataInTemporaryFileStorageManager,
    QHash<QString, QString> & resourceFileStoragePathsByLocalUid) :
    m_noteEditor(noteEditor),
    m_resourceInfo(resourceInfo),
    m_resourceDataInTemporaryFileStorageManager(
        resourceDataInTemporaryFileStorageManager),
    m_resourceFileStoragePathsByLocalUid(resourceFileStoragePathsByLocalUid),
    m_rotationDirection(rotationDirection),
    m_resourceHashBefore(resourceHashBefore)
{}

void ImageResourceRotationDelegate::start()
{
    QNDEBUG("note_editor:delegate", "ImageResourceRotationDelegate::start");

    if (m_noteEditor.isEditorPageModified()) {
        QObject::connect(
            &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
            &ImageResourceRotationDelegate::onOriginalPageConvertedToNote);

        m_noteEditor.convertToNote();
    }
    else {
        rotateImageResource();
    }
}

void ImageResourceRotationDelegate::onOriginalPageConvertedToNote(
    qevercloud::Note note)
{
    QNDEBUG(
        "note_editor:delegate",
        "ImageResourceRotationDelegate::onOriginalPageConvertedToNote");

    Q_UNUSED(note)

    QObject::disconnect(
        &m_noteEditor, &NoteEditorPrivate::convertedToNote, this,
        &ImageResourceRotationDelegate::onOriginalPageConvertedToNote);

    rotateImageResource();
}

void ImageResourceRotationDelegate::rotateImageResource()
{
    QNDEBUG(
        "note_editor:delegate",
        "ImageResourceRotationDelegate::rotateImageResource");

    ErrorString error(QT_TR_NOOP("Can't rotate the image attachment"));

    m_pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(!m_pNote)) {
        error.appendBase(QT_TR_NOOP("No note is set to the editor"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    if (Q_UNLIKELY(!m_pNote->resources())) {
        error.appendBase(QT_TR_NOOP("Note has no attachments"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    int targetResourceIndex = -1;
    QList<qevercloud::Resource> resources = *m_pNote->resources();
    const int numResources = resources.size();
    for (int i = 0; i < numResources; ++i) {
        const auto & resource = qAsConst(resources)[i];
        if (!resource.data() || !resource.data()->bodyHash() ||
            *resource.data()->bodyHash() != m_resourceHashBefore)
        {
            continue;
        }

        if (Q_UNLIKELY(!resource.mime())) {
            error.appendBase(QT_TR_NOOP("The mime type is missing"));
            QNWARNING(
                "note_editor:delegate", error << ", resource: " << resource);
            Q_EMIT notifyError(error);
            return;
        }

        if (!resource.mime()->startsWith(QStringLiteral("image/"))) {
            error.appendBase(
                QT_TR_NOOP("The mime type indicates the attachment "
                           "is not an image"));
            QNWARNING(
                "note_editor:delegate", error << ", resource: " << resource);
            Q_EMIT notifyError(error);
            return;
        }

        targetResourceIndex = i;
        break;
    }

    if (Q_UNLIKELY(targetResourceIndex < 0)) {
        error.appendBase(
            QT_TR_NOOP("Can't find the attachment within the note"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_rotatedResource = qAsConst(resources)[targetResourceIndex];
    if (Q_UNLIKELY(!m_rotatedResource.data() ||
                   !m_rotatedResource.data()->body()))
    {
        error.appendBase(QT_TR_NOOP("The data body is missing"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_resourceDataBefore = *m_rotatedResource.data()->body();

    if (m_rotatedResource.recognition() &&
        m_rotatedResource.recognition()->body())
    {
        m_resourceRecognitionDataBefore =
            *m_rotatedResource.recognition()->body();
    }

    if (m_rotatedResource.recognition() &&
        m_rotatedResource.recognition()->bodyHash())
    {
        m_resourceRecognitionDataHashBefore =
            *m_rotatedResource.recognition()->bodyHash();
    }

    QImage resourceImage;
    const bool loaded =
        resourceImage.loadFromData(*m_rotatedResource.data()->body());
    if (Q_UNLIKELY(!loaded)) {
        error.appendBase(
            QT_TR_NOOP("Can't load the resource data as an image"));
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    m_resourceImageSizeBefore.setHeight(resourceImage.height());
    m_resourceImageSizeBefore.setWidth(resourceImage.width());

    qreal angle =
        ((m_rotationDirection == INoteEditorBackend::Rotation::Clockwise)
             ? 90.0
             : -90.0);

    QTransform transform;
    transform.rotate(angle);
    resourceImage = resourceImage.transformed(transform);

    resourceImage = resourceImage.scaled(
        m_resourceImageSizeBefore.height(), m_resourceImageSizeBefore.width(),
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QByteArray rotatedResourceData;
    QBuffer rotatedResourceDataBuffer(&rotatedResourceData);
    rotatedResourceDataBuffer.open(QIODevice::WriteOnly);
    resourceImage.save(&rotatedResourceDataBuffer, "PNG");

    m_rotatedResource.mutableData()->setBody(rotatedResourceData);
    m_rotatedResource.mutableData()->setSize(rotatedResourceData.size());
    m_rotatedResource.mutableData()->setBodyHash(std::nullopt);

    const int height = resourceImage.height();
    const int width = resourceImage.width();
    QNTRACE(
        "note_editor:delegate",
        "Rotated resource's height = " << height << ", width = " << width);

    if ((height > 0) && (height <= std::numeric_limits<qint16>::max()) &&
        (width > 0) && (width <= std::numeric_limits<qint16>::max()))
    {
        m_rotatedResource.setHeight(static_cast<qint16>(height));
        m_rotatedResource.setWidth(static_cast<qint16>(width));
    }
    else {
        m_rotatedResource.setHeight(-1);
        m_rotatedResource.setWidth(-1);
    }

    // Need to destroy the recognition data (if any) because it would no longer
    // correspond to the rotated image
    m_rotatedResource.setRecognition(std::nullopt);

    m_saveResourceDataToTemporaryFileRequestId = QUuid::createUuid();

    QObject::connect(
        this, &ImageResourceRotationDelegate::saveResourceDataToTemporaryFile,
        &m_resourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            onSaveResourceDataToTemporaryFileRequest);

    QObject::connect(
        &m_resourceDataInTemporaryFileStorageManager,
        &ResourceDataInTemporaryFileStorageManager::
            saveResourceDataToTemporaryFileCompleted,
        this,
        &ImageResourceRotationDelegate::onResourceDataSavedToTemporaryFile);

    Q_EMIT saveResourceDataToTemporaryFile(
        m_rotatedResource.parentLocalId(), m_rotatedResource.localId(),
        *m_rotatedResource.data()->body(), QByteArray(),
        m_saveResourceDataToTemporaryFileRequestId,
        /* is image = */ true);
}

void ImageResourceRotationDelegate::onResourceDataSavedToTemporaryFile(
    QUuid requestId, QByteArray dataHash, ErrorString errorDescription)
{
    if (requestId != m_saveResourceDataToTemporaryFileRequestId) {
        return;
    }

    QNDEBUG(
        "note_editor:delegate",
        "ImageResourceRotationDelegate::onResourceDataSavedToTemporaryFile: "
            << "hash = " << dataHash.toHex() << ", error description = "
            << errorDescription);

    if (Q_UNLIKELY(!errorDescription.isEmpty())) {
        ErrorString error(
            QT_TR_NOOP("Can't rotate the image attachment: can't "
                       "write modified resource data to local file"));
        error.appendBase(errorDescription.base());
        error.appendBase(errorDescription.additionalBases());
        error.details() = errorDescription.details();
        QNWARNING("note_editor:delegate", error);
        Q_EMIT notifyError(error);
        return;
    }

    auto * pNote = m_noteEditor.notePtr();
    if (Q_UNLIKELY(pNote != m_pNote)) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't rotate the image attachment: "
                       "note was changed during the processing "
                       "of image rotation"));
        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    const QString localId = m_rotatedResource.localId();
    m_noteEditor.removeSymlinksToImageResourceFile(localId);

    QString fileStoragePath = ResourceDataInTemporaryFileStorageManager::
                                  imageResourceFileStorageFolderPath() +
        QStringLiteral("/") + pNote->localId() + QStringLiteral("/") +
        localId + QStringLiteral(".dat");

    QFile rotatedImageResourceFile(fileStoragePath);
    QString linkFilePath = fileStoragePath;
    linkFilePath.remove(linkFilePath.size() - 4, 4);
    linkFilePath += QStringLiteral("_");
    linkFilePath += QString::number(QDateTime::currentMSecsSinceEpoch());

#ifdef Q_OS_WIN
    linkFilePath += QStringLiteral(".lnk");
#else
    linkFilePath += QStringLiteral(".png");
#endif

    if (Q_UNLIKELY(!rotatedImageResourceFile.link(linkFilePath))) {
        errorDescription.setBase(
            QT_TR_NOOP("Can't rotate the image attachment: "
                       "can't create a link to the resource "
                       "file to use within the note editor"));
        errorDescription.details() = rotatedImageResourceFile.errorString();
        errorDescription.details() += QStringLiteral(", error code: ");

        errorDescription.details() +=
            QString::number(rotatedImageResourceFile.error());

        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    QNTRACE(
        "note_editor:delegate",
        "Created a link to the original file ("
            << QDir::toNativeSeparators(fileStoragePath)
            << "): " << QDir::toNativeSeparators(linkFilePath));

    m_resourceFileStoragePathAfter = linkFilePath;

    const auto resourceFileStoragePathIt =
        m_resourceFileStoragePathsByLocalUid.find(localId);

    if (Q_UNLIKELY(
            resourceFileStoragePathIt ==
            m_resourceFileStoragePathsByLocalUid.end()))
    {
        errorDescription.setBase(
            QT_TR_NOOP("Can't rotate the image attachment: "
                       "can't find path to the attachment "
                       "file before the rotation"));
        QNWARNING("note_editor:delegate", errorDescription);
        Q_EMIT notifyError(errorDescription);
        return;
    }

    m_resourceFileStoragePathBefore = resourceFileStoragePathIt.value();
    resourceFileStoragePathIt.value() = linkFilePath;

    const QString displayName = resourceDisplayName(m_rotatedResource);

    const QString displaySize = humanReadableSize(
        static_cast<quint64>(*m_rotatedResource.data()->size()));

    m_rotatedResource.mutableData()->setBodyHash(dataHash);

    if (Q_UNLIKELY(!m_pNote->resources())) {
        m_pNote->setResources(QList<qevercloud::Resource>());
        m_pNote->mutableResources()->push_back(m_rotatedResource);
    }
    else {
        auto it = std::find_if(
            m_pNote->mutableResources()->begin(),
            m_pNote->mutableResources()->end(),
            [this](const qevercloud::Resource & resource)
            {
                return resource.localId() == m_rotatedResource.localId();
            });
        if (it != m_pNote->mutableResources()->end()) {
            *it = m_rotatedResource;
        }
        else {
            m_pNote->mutableResources()->push_back(m_rotatedResource);
        }
    }

    m_resourceInfo.removeResourceInfo(m_resourceHashBefore);

    m_resourceInfo.cacheResourceInfo(
        dataHash, displayName, displaySize, linkFilePath,
        QSize(*m_rotatedResource.width(), *m_rotatedResource.height()));

    if (m_resourceFileStoragePathBefore != fileStoragePath) {
        QFile oldResourceFile(m_resourceFileStoragePathBefore);
        if (Q_UNLIKELY(!oldResourceFile.remove())) {
#ifdef Q_OS_WIN
            if (m_resourceFileStoragePathBefore.endsWith(
                    QStringLiteral(".lnk"))) {
                // NOTE: there appears to be a bug in Qt for Windows,
                // QFile::remove returns false for any *.lnk files even though
                // the files are actually getting removed
                QNDEBUG(
                    "note_editor:delegate",
                    "Skipping the reported failure at removing the .lnk file");
            }
            else {
#endif

                QNWARNING(
                    "note_editor:delegate",
                    "Can't remove stale resource file "
                        << m_resourceFileStoragePathBefore);

#ifdef Q_OS_WIN
            }
#endif
        }
    }

    const QString javascript = QStringLiteral("updateResourceHash('") +
        QString::fromLocal8Bit(m_resourceHashBefore.toHex()) +
        QStringLiteral("', '") + QString::fromLocal8Bit(dataHash.toHex()) +
        QStringLiteral("');");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(
            *this, &ImageResourceRotationDelegate::onResourceTagHashUpdated));
}

void ImageResourceRotationDelegate::onResourceTagHashUpdated(
    const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "ImageResourceRotationDelegate::onResourceTagHashUpdated");

    Q_UNUSED(data)

    const QString javascript = QStringLiteral("updateImageResourceSrc('") +
        QString::fromLocal8Bit(m_rotatedResource.data()->bodyHash()->toHex()) +
        QStringLiteral("', '") + m_resourceFileStoragePathAfter +
        QStringLiteral("', ") +
        QString::number(m_rotatedResource.height()
                            ? *m_rotatedResource.height()
                            : qint16(0)) +
        QStringLiteral(", ") +
        QString::number(m_rotatedResource.width() ? *m_rotatedResource.width()
                                                  : qint16(0)) +
        QStringLiteral(");");

    GET_PAGE()
    page->executeJavaScript(
        javascript,
        JsCallback(
            *this, &ImageResourceRotationDelegate::onResourceTagSrcUpdated));
}

void ImageResourceRotationDelegate::onResourceTagSrcUpdated(
    const QVariant & data)
{
    QNDEBUG(
        "note_editor:delegate",
        "ImageResourceRotationDelegate::onResourceTagSrcUpdated");

    Q_UNUSED(data)

    Q_EMIT finished(
        m_resourceDataBefore, m_resourceHashBefore,
        m_resourceRecognitionDataBefore, m_resourceRecognitionDataHashBefore,
        m_resourceImageSizeBefore, m_rotatedResource, m_rotationDirection);
}

} // namespace quentier
