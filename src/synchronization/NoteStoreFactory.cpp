/*
 * Copyright 2022-2024 Dmitry Ivanov
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

#include "NoteStoreFactory.h"

#include <qevercloud/services/INoteStore.h>

namespace quentier::synchronization {

qevercloud::INoteStorePtr NoteStoreFactory::createNoteStore(
    QString noteStoreUrl, std::optional<qevercloud::Guid> linkedNotebookGuid,
    qevercloud::IRequestContextPtr ctx, qevercloud::IRetryPolicyPtr retryPolicy)
{
    return qevercloud::newNoteStore(
        std::move(noteStoreUrl), std::move(linkedNotebookGuid), std::move(ctx),
        std::move(retryPolicy));
}

} // namespace quentier::synchronization
