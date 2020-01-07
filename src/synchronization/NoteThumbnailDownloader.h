/*
 * Copyright 2016-2019 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTE_THUMBNAIL_DOWNLOADER_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTE_THUMBNAIL_DOWNLOADER_H

#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>
#include <QObject>
#include <QString>
#include <QByteArray>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

class Q_DECL_HIDDEN NoteThumbnailDownloader: public QObject
{
    Q_OBJECT
public:
    explicit NoteThumbnailDownloader(const QString & host,
                                     const QString & noteGuid,
                                     const QString & authToken,
                                     const QString & shardId,
                                     const bool noteFromPublicLinkedNotebook,
                                     QObject * parent = nullptr);
    virtual ~NoteThumbnailDownloader();

    void start();

Q_SIGNALS:
    void finished(bool success, QString noteGuid,
                  QByteArray downloadedThumbnailData,
                  ErrorString errorDescription);

private:
    using EverCloudExceptionDataPtr = qevercloud::EverCloudExceptionDataPtr;
    using IRequestContextPtr = qevercloud::IRequestContextPtr;

private Q_SLOTS:
    void onDownloadFinished(
        QVariant result,
        EverCloudExceptionDataPtr exceptionData,
        IRequestContextPtr ctx);

private:
    QString                     m_host;
    QString                     m_noteGuid;
    QString                     m_authToken;
    QString                     m_shardId;
    bool                        m_noteFromPublicLinkedNotebook;
    qevercloud::AsyncResult *   m_pAsyncResult;
    qevercloud::Thumbnail *     m_pThumbnail;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_THUMBNAIL_DOWNLOADER_H
