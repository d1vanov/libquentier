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

#include "LinkedNotebookTagsCleaner.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>

#include <utility>

namespace quentier::synchronization {

LinkedNotebookTagsCleaner::LinkedNotebookTagsCleaner(
    local_storage::ILocalStoragePtr localStorage) :
    m_localStorage{std::move(localStorage)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "LinkedNotebookTagsCleaner ctor: local storage is null")}};
    }
}

QFuture<void> LinkedNotebookTagsCleaner::clearStaleLinkedNotebookTags()
{
    const auto selfWeak = weak_from_this();

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();
    promise->start();

    local_storage::ILocalStorage::ListTagsOptions options;
    options.m_affiliation =
        local_storage::ILocalStorage::Affiliation::AnyLinkedNotebook;
    options.m_tagNotesRelation =
        local_storage::ILocalStorage::TagNotesRelation::WithoutNotes;

    auto listTagsFuture = m_localStorage->listTags(options);
    threading::thenOrFailed(
        std::move(listTagsFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise](const QList<qevercloud::Tag> & tags) {
                onListedTags(tags, promise);
            }});

    return future;
}

void LinkedNotebookTagsCleaner::onListedTags(
    const QList<qevercloud::Tag> & tags,
    const std::shared_ptr<QPromise<void>> & promise)
{
    if (tags.isEmpty()) {
        promise->finish();
        return;
    }

    QList<QFuture<void>> expungeTagFutures;
    expungeTagFutures.reserve(tags.size());
    for (const auto & tag: std::as_const(tags)) {
        QNDEBUG(
            "synchronization::LinkedNotebookTagsCleaner",
            "Expunging linked notebook's tag "
                << tag.name().value_or(QStringLiteral("<unknown>"))
                << " with local id of " << tag.localId() << ", guid of "
                << tag.guid().value_or(QStringLiteral("<unknown>"))
                << " and linked notebook guid of "
                << tag.linkedNotebookGuid().value_or(
                       QStringLiteral("<unknown>"))
                << " as it is not referenced by any note anymore");

        expungeTagFutures << m_localStorage->expungeTagByLocalId(tag.localId());
    }

    auto commonFuture = threading::whenAll(std::move(expungeTagFutures));
    threading::thenOrFailed(std::move(commonFuture), promise);
}

} // namespace quentier::synchronization
