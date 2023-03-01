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

#include <synchronization/INoteThumbnailDownloaderFactory.h>

#include <gmock/gmock.h>

namespace quentier::synchronization::tests::mocks {

class MockINoteThumbnailDownloaderFactory :
    public INoteThumbnailDownloaderFactory
{
public:
    MOCK_METHOD(
        QFuture<qevercloud::INoteThumbnailDownloaderPtr>,
        createNoteThumbnailDownloader,
        (QString notebookLocalId, qevercloud::IRequestContextPtr ctx),
        (override));
};

} // namespace quentier::synchronization::tests::mocks
