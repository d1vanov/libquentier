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

#include <synchronization/ILinkedNotebookTagsCleaner.h>

#include <quentier/local_storage/Fwd.h>

#include <qevercloud/types/Fwd.h>

#include <QList>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <memory>

namespace quentier::synchronization {

class LinkedNotebookTagsCleaner :
    public ILinkedNotebookTagsCleaner,
    public std::enable_shared_from_this<LinkedNotebookTagsCleaner>
{
public:
    explicit LinkedNotebookTagsCleaner(
        local_storage::ILocalStoragePtr localStorage);

public: // ILinkedNotebookTagsCleaner
    [[nodiscard]] QFuture<void> clearStaleLinkedNotebookTags() override;

private:
    void onListedTags(
        const QList<qevercloud::Tag> & tags,
        const std::shared_ptr<QPromise<void>> & promise);

private:
    const local_storage::ILocalStoragePtr m_localStorage;
};

} // namespace quentier::synchronization
