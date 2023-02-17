/*
 * Copyright 2023 Dmitry Ivanov
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

#pragma once

#include <qevercloud/INoteThumbnailDownloader.h>

#include <gmock/gmock.h>

namespace quentier::synchronization::tests::mocks::qevercloud {

class MockINoteThumnailDownloader : public ::qevercloud::INoteThumbnailDownloader
{
public:
    MOCK_METHOD(
        QByteArray, downloadNoteThumbnail,
        (::qevercloud::Guid guid, int size, ImageType imageType,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QByteArray, downloadResourceThumbnail,
        (::qevercloud::Guid guid, int size, ImageType imageType,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QByteArray>, downloadNoteThumbnailAsync,
        (::qevercloud::Guid guid, int size, ImageType imageType,
         ::qevercloud::IRequestContextPtr ctx),
        (override));

    MOCK_METHOD(
        QFuture<QByteArray>, downloadResourceThumbnailAsync,
        (::qevercloud::Guid guid, int size, ImageType imageType,
         ::qevercloud::IRequestContextPtr ctx),
        (override));
};

} // namespace quentier::synchronization::tests::mocks::qevercloud
