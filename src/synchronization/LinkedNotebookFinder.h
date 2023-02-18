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

#include "ILinkedNotebookFinder.h"

#include <quentier/local_storage/Fwd.h>

#include <QHash>
#include <QMutex>

#include <memory>

namespace quentier::synchronization {

class LinkedNotebookFinder final :
    public ILinkedNotebookFinder,
    public std::enable_shared_from_this<LinkedNotebookFinder>
{
public:
    explicit LinkedNotebookFinder(local_storage::ILocalStoragePtr localStorage);

public: // ILinkedNotebookFinder
    [[nodiscard]] QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByNotebookLocalId(
            const QString & notebookLocalId) override;

private:
    [[nodiscard]] QFuture<std::optional<qevercloud::LinkedNotebook>>
        findLinkedNotebookByNotebookLocalIdImpl(
            const QString & notebookLocalId);

private:
    const local_storage::ILocalStoragePtr m_localStorage;

    QHash<QString, QFuture<std::optional<qevercloud::LinkedNotebook>>>
        m_linkedNotebooksByNotebookLocalId;

    QMutex m_linkedNotebooksByNotebookLocalIdMutex;

    QHash<QString, QFuture<std::optional<qevercloud::LinkedNotebook>>>
        m_linkedNotebooksByGuid;

    QMutex m_linkedNotebooksByGuidMutex;
};

} // namespace quentier::synchronization
