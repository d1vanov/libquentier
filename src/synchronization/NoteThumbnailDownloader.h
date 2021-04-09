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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTE_THUMBNAIL_DOWNLOADER_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTE_THUMBNAIL_DOWNLOADER_H

#include <quentier/types/ErrorString.h>

#include <qevercloud/QEverCloud.h>

#include <QByteArray>
#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariant>

namespace quentier {

class Q_DECL_HIDDEN NoteThumbnailDownloader final : public QObject
{
    Q_OBJECT
public:
    explicit NoteThumbnailDownloader(
        QString host, QString noteGuid, QString authToken, QString shardId,
        bool noteFromPublicLinkedNotebook, QObject * parent = nullptr);

    ~NoteThumbnailDownloader() override;

    void start();

Q_SIGNALS:
    void finished(
        bool success, QString noteGuid, QByteArray downloadedThumbnailData,
        ErrorString errorDescription);

private:
    using EverCloudExceptionDataPtr = qevercloud::EverCloudExceptionDataPtr;
    using IRequestContextPtr = qevercloud::IRequestContextPtr;

    void onDownloadFinished(
        const QVariant & result,
        const EverCloudExceptionDataPtr & exceptionData);

private:
    QString m_host;
    QString m_noteGuid;
    QString m_authToken;
    QString m_shardId;
    bool m_noteFromPublicLinkedNotebook;

    QFuture<QVariant> m_future;
    QFutureWatcher<QVariant> m_futureWatcher;

    qevercloud::Thumbnail * m_pThumbnail = nullptr;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_THUMBNAIL_DOWNLOADER_H
