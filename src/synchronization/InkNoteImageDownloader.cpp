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

#include "InkNoteImageDownloader.h"

#include <quentier/logging/QuentierLogger.h>

#include <qt5qevercloud/InkNoteImageDownloader.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QNetworkRequest>
#include <QUrl>

#include <memory>

namespace quentier {

InkNoteImageDownloader::InkNoteImageDownloader(
    const QString & host, const QString & resourceGuid,
    const QString & noteGuid, const QString & authToken,
    const QString & shardId, const int height, const int width,
    const bool noteFromPublicLinkedNotebook, const QString & storageFolderPath,
    QObject * parent) :
    QObject(parent),
    m_host(host), m_resourceGuid(resourceGuid), m_noteGuid(noteGuid),
    m_authToken(authToken), m_shardId(shardId),
    m_storageFolderPath(storageFolderPath), m_height(height), m_width(width),
    m_noteFromPublicLinkedNotebook(noteFromPublicLinkedNotebook)
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

    if (Q_UNLIKELY(!m_noteFromPublicLinkedNotebook && m_authToken.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("the authentication data is incomplete"));
    }

    qevercloud::InkNoteImageDownloader downloader(
        m_host, m_shardId, m_authToken, m_width, m_height);

    QByteArray inkNoteImageData;
    try {
        inkNoteImageData =
            downloader.download(m_resourceGuid, m_noteFromPublicLinkedNotebook);
    }
    catch (const qevercloud::EverCloudException & everCloudException) {
        ErrorString errorDescription(
            QT_TR_NOOP("Caught EverCloudException on attempt to download "
                       "the ink note image data"));
        auto exceptionData = everCloudException.exceptionData();
        if (exceptionData) {
            errorDescription.details() = exceptionData->errorMessage;
        }

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

    QFileInfo folderPathInfo(m_storageFolderPath);
    if (Q_UNLIKELY(!folderPathInfo.exists())) {
        QDir dir(m_storageFolderPath);
        bool res = dir.mkpath(m_storageFolderPath);
        if (Q_UNLIKELY(!res)) {
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

    QString filePath = m_storageFolderPath + QStringLiteral("/") +
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
