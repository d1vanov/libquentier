/*
 * Copyright 2016-2023 Dmitry Ivanov
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

#include "InkNoteImageDownloader.h"

#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/IInkNoteImageDownloader.h>
#include <qevercloud/RequestContextBuilder.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>

namespace quentier {

InkNoteImageDownloader::InkNoteImageDownloader(
    QString host, QString resourceGuid, QString noteGuid, QString authToken,
    QString shardId, const int height, const int width,
    QString storageFolderPath,
    QObject * parent) :
    QObject(parent),
    m_host(std::move(host)), m_resourceGuid(std::move(resourceGuid)),
    m_noteGuid(std::move(noteGuid)), m_authToken(std::move(authToken)),
    m_shardId(std::move(shardId)),
    m_storageFolderPath(std::move(storageFolderPath)), m_height(height),
    m_width(width)
{}

void InkNoteImageDownloader::run()
{
    QNDEBUG(
        "synchronization:ink_note",
        "InkNoteImageDownloader::run: host = "
            << m_host << ", resource guid = " << m_resourceGuid
            << ", note guid = " << m_noteGuid
            << ", storage folder path = " << m_storageFolderPath);

#define SET_ERROR(error)                                                       \
    ErrorString errorDescription(error);                                       \
    Q_EMIT finished(false, m_resourceGuid, m_noteGuid, errorDescription);      \
    return // SET_ERROR

    if (Q_UNLIKELY(m_host.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("host is empty"));
    }

    if (Q_UNLIKELY(m_resourceGuid.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("resource guid is empty"));
    }

    if (Q_UNLIKELY(m_shardId.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("shard id is empty"));
    }

    auto ctx = qevercloud::RequestContextBuilder{}
        .setAuthenticationToken(m_authToken)
        .build();

    auto downloader = qevercloud::newInkNoteImageDownloader(
        m_host, m_shardId, QSize{m_width, m_height}, ctx);

    QByteArray inkNoteImageData;
    try {
        inkNoteImageData = downloader->download(m_resourceGuid);
    }
    catch (const qevercloud::EverCloudException & everCloudException) {
        ErrorString errorDescription(
            QT_TR_NOOP("Caught EverCloudException on attempt to download "
                       "the ink note image data"));
        errorDescription.details() =
            QString::fromUtf8(everCloudException.what());

        Q_EMIT finished(false, m_resourceGuid, m_noteGuid, errorDescription);
        return;
    }
    catch (const std::exception & stdException) {
        ErrorString errorDescription(
            QT_TR_NOOP("Caught std::exception on attempt to download the ink "
                       "note image data"));
        errorDescription.details() = QString::fromUtf8(stdException.what());
        Q_EMIT finished(false, m_resourceGuid, m_noteGuid, errorDescription);
        return;
    }
    catch (...) {
        ErrorString errorDescription(
            QT_TR_NOOP("Caught unknown exception on attempt to download "
                       "the ink note image data"));
        Q_EMIT finished(false, m_resourceGuid, m_noteGuid, errorDescription);
        return;
    }

    if (Q_UNLIKELY(inkNoteImageData.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("received empty note thumbnail data"));
    }

    const QFileInfo folderPathInfo(m_storageFolderPath);
    if (Q_UNLIKELY(!folderPathInfo.exists())) {
        QDir dir(m_storageFolderPath);
        if (Q_UNLIKELY(!dir.mkpath(m_storageFolderPath))) {
            SET_ERROR(
                QT_TR_NOOP("can't create a folder to store the ink note "
                           "images in"));
        }
    }
    else if (Q_UNLIKELY(!folderPathInfo.isDir())) {
        SET_ERROR(
            QT_TR_NOOP("can't create a folder to store the ink note "
                       "images in: a file with similar name and path "
                       "already exists"));
    }
    else if (Q_UNLIKELY(!folderPathInfo.isWritable())) {
        SET_ERROR(
            QT_TR_NOOP("the folder for ink note images storage is not "
                       "writable"));
    }

    const QString filePath = m_storageFolderPath + QStringLiteral("/") +
        m_resourceGuid + QStringLiteral(".png");

    QFile file(filePath);
    if (Q_UNLIKELY(!file.open(QIODevice::WriteOnly))) {
        SET_ERROR(QT_TR_NOOP("can't open the ink note image file for writing"));
    }

    file.write(inkNoteImageData);
    file.close();

    Q_EMIT finished(true, m_resourceGuid, m_noteGuid, ErrorString());
}

} // namespace quentier
