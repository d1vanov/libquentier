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

#include "NoteThumbnailDownloader.h"
#include <quentier/logging/QuentierLogger.h>

#include <qt5qevercloud/Thumbnail.h>

namespace quentier {

NoteThumbnailDownloader::NoteThumbnailDownloader(
    const QString & host, const QString & noteGuid, const QString & authToken,
    const QString & shardId, const bool noteFromPublicLinkedNotebook,
    QObject * parent) :
    QObject(parent),
    m_host(host), m_noteGuid(noteGuid), m_authToken(authToken),
    m_shardId(shardId),
    m_noteFromPublicLinkedNotebook(noteFromPublicLinkedNotebook)
{}

NoteThumbnailDownloader::~NoteThumbnailDownloader()
{
    delete m_pThumbnail;

    // NOTE: m_pAsyncResult deletes itself automatically
}

void NoteThumbnailDownloader::start()
{
    QNDEBUG(
        "synchronization:thumbnail",
        "NoteThumbnailDownloader::start: host = "
            << m_host << ", note guid = " << m_noteGuid << ", is public = "
            << (m_noteFromPublicLinkedNotebook ? "true" : "false"));

#define SET_ERROR(error)                                                       \
    ErrorString errorDescription(error);                                       \
    QNDEBUG("synchronization:thumbnail", errorDescription);                    \
    Q_EMIT finished(false, m_noteGuid, QByteArray(), errorDescription);        \
    return

    if (Q_UNLIKELY(m_host.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("host is empty"));
    }

    if (Q_UNLIKELY(m_noteGuid.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("note guid is empty"));
    }

    if (Q_UNLIKELY(m_shardId.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("shard id is empty"));
    }

    if (Q_UNLIKELY(!m_noteFromPublicLinkedNotebook && m_authToken.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("authentication data is incomplete"));
    }

    delete m_pThumbnail;
    m_pThumbnail = nullptr;

    if (m_pAsyncResult) {
        // NOTE: m_pAsyncResult deletes itself automatically
        m_pAsyncResult = nullptr;
    }

    m_pThumbnail = new qevercloud::Thumbnail(m_host, m_shardId, m_authToken);
    m_pAsyncResult = m_pThumbnail->downloadAsync(
        m_noteGuid, m_noteFromPublicLinkedNotebook,
        /* is resource guid = */ false);

    if (Q_UNLIKELY(!m_pAsyncResult)) {
        SET_ERROR(
            QT_TR_NOOP("failed to download the note thumbnail, QEverCloud "
                       "returned null pointer to AsyncResult"));
    }

    QObject::connect(
        m_pAsyncResult, &qevercloud::AsyncResult::finished, this,
        &NoteThumbnailDownloader::onDownloadFinished,
        Qt::ConnectionType(Qt::UniqueConnection | Qt::QueuedConnection));
}

void NoteThumbnailDownloader::onDownloadFinished(
    QVariant result, EverCloudExceptionDataPtr exceptionData,
    IRequestContextPtr ctx)
{
    QNDEBUG(
        "synchronization:thumbnail",
        "NoteThumbnailDownloader::onDownloadFinished");

    Q_UNUSED(ctx)

    delete m_pThumbnail;
    m_pThumbnail = nullptr;

    // NOTE: after AsyncResult finishes, it destroys itself so must lose
    // the pointer to it here
    m_pAsyncResult = nullptr;

    if (exceptionData) {
        ErrorString errorDescription(
            QT_TR_NOOP("failed to download the note thumbnail"));
        errorDescription.details() = exceptionData->errorMessage;
        QNDEBUG("synchronization:thumbnail", errorDescription);
        Q_EMIT finished(false, m_noteGuid, QByteArray(), errorDescription);
        return;
    }

    QByteArray thumbnailImageData = result.toByteArray();
    if (Q_UNLIKELY(thumbnailImageData.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("received empty note thumbnail data"));
    }

    Q_EMIT finished(true, m_noteGuid, thumbnailImageData, ErrorString());
}

} // namespace quentier
