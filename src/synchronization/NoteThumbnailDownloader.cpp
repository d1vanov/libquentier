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

#include "NoteThumbnailDownloader.h"
#include <quentier/logging/QuentierLogger.h>

#include <qevercloud/Thumbnail.h>

namespace quentier {

NoteThumbnailDownloader::NoteThumbnailDownloader(
    QString host, QString noteGuid, QString authToken, QString shardId,
    const bool noteFromPublicLinkedNotebook, QObject * parent) :
    QObject(parent),
    m_host(std::move(host)), m_noteGuid(std::move(noteGuid)),
    m_authToken(std::move(authToken)), m_shardId(std::move(shardId)),
    m_noteFromPublicLinkedNotebook(noteFromPublicLinkedNotebook)
{}

NoteThumbnailDownloader::~NoteThumbnailDownloader()
{
    delete m_pThumbnail;
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

    m_pThumbnail = new qevercloud::Thumbnail(m_host, m_shardId, m_authToken);
    m_future = m_pThumbnail->downloadAsync(
        m_noteGuid, m_noteFromPublicLinkedNotebook,
        /* is resource guid = */ false);

    m_futureWatcher.setFuture(m_future);
    QObject::connect(
        &m_futureWatcher,
        &QFutureWatcher<QVariant>::finished,
        this,
        [this]
        {
            std::exception_ptr e;
            QVariant value;

            try
            {
                value = m_future.result();
            }
            catch (...)
            {
                e = std::current_exception();
            }

            onDownloadFinished(value, e);
        });
}

void NoteThumbnailDownloader::onDownloadFinished(
    const QVariant & result, const std::exception_ptr & e)
{
    QNDEBUG(
        "synchronization:thumbnail",
        "NoteThumbnailDownloader::onDownloadFinished");

    delete m_pThumbnail;
    m_pThumbnail = nullptr;

    if (e) {
        ErrorString errorDescription(
            QT_TR_NOOP("failed to download the note thumbnail"));

        try
        {
            std::rethrow_exception(e);
        }
        catch (const std::exception & exc)
        {
            errorDescription.details() = QString::fromUtf8(exc.what());
        }

        QNDEBUG("synchronization:thumbnail", errorDescription);
        Q_EMIT finished(false, m_noteGuid, QByteArray(), errorDescription);
        return;
    }

    const QByteArray thumbnailImageData = result.toByteArray();
    if (Q_UNLIKELY(thumbnailImageData.isEmpty())) {
        SET_ERROR(QT_TR_NOOP("received empty note thumbnail data"));
    }

    Q_EMIT finished(true, m_noteGuid, thumbnailImageData, ErrorString());
}

} // namespace quentier
