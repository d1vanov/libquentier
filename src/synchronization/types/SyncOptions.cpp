/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include "SyncOptions.h"

#include <qevercloud/IRequestContext.h>

namespace quentier::synchronization {

bool SyncOptions::downloadNoteThumbnails() const noexcept
{
    return m_downloadNoteThumbnails;
}

std::optional<QDir> SyncOptions::inkNoteImagesStorageDir() const
{
    return m_inkNoteImagesStorageDir;
}

qevercloud::IRequestContextPtr SyncOptions::requestContext() const noexcept
{
    return m_ctx;
}

qevercloud::IRetryPolicyPtr SyncOptions::retryPolicy() const noexcept
{
    return m_retryPolicy;
}

std::optional<quint32> SyncOptions::maxConcurrentNoteDownloads() const noexcept
{
    return m_maxConcurrentNoteDownloads;
}

std::optional<quint32> SyncOptions::maxConcurrentResourceDownloads()
    const noexcept
{
    return m_maxConcurrentResourceDownloads;
}

QTextStream & SyncOptions::print(QTextStream & strm) const
{
    strm << "SyncOptions: downloadNoteThumbnails = "
         << (m_downloadNoteThumbnails ? "true" : "false")
         << ", inkNoteImagesStorageDir = "
         << (m_inkNoteImagesStorageDir
                 ? m_inkNoteImagesStorageDir->absolutePath()
                 : QString::fromUtf8("<not set>"));

    strm << ", request context = ";
    if (m_ctx) {
        strm << "{timeout = " << m_ctx->connectionTimeout()
             << ", increase connection timeout exponentially = "
             << (m_ctx->increaseConnectionTimeoutExponentially() ? "true"
                                                              : "false")
             << ", max connection timeout = " << m_ctx->maxConnectionTimeout()
             << ", max request retry count = " << m_ctx->maxRequestRetryCount()
             << ", cookies: ";
        const auto cookies = m_ctx->cookies();
        for (const auto & cookie: qAsConst(cookies)) {
            strm << "[" << cookie.name() << ": " << cookie.value() << "]; ";
        }
        strm << "}";
    }
    else {
        strm << "<null>";
    }

    strm << ", retry policy = " << (m_retryPolicy ? "<not null>" : "<null>");

    strm << ", max concurrent note downloads = ";
    if (m_maxConcurrentNoteDownloads) {
        strm << *m_maxConcurrentNoteDownloads;
    }
    else {
        strm << "<nullopt>";
    }

    strm << ", max concurrent resource downloads = ";
    if (m_maxConcurrentResourceDownloads) {
        strm << *m_maxConcurrentResourceDownloads;
    }
    else {
        strm << "<nullopt>";
    }

    return strm;
}

bool operator==(const SyncOptions & lhs, const SyncOptions & rhs) noexcept
{
    // clang-format off
    return lhs.m_downloadNoteThumbnails == rhs.m_downloadNoteThumbnails &&
        lhs.m_inkNoteImagesStorageDir == rhs.m_inkNoteImagesStorageDir &&
        lhs.m_ctx == rhs.m_ctx &&
        lhs.m_retryPolicy == rhs.m_retryPolicy &&
        lhs.m_maxConcurrentNoteDownloads == rhs.m_maxConcurrentNoteDownloads &&
        lhs.m_maxConcurrentResourceDownloads ==
            rhs.m_maxConcurrentResourceDownloads;
    // clang-format on
}

bool operator!=(const SyncOptions & lhs, const SyncOptions & rhs) noexcept
{
    return !(lhs == rhs);
}

} // namespace quentier::synchronization
