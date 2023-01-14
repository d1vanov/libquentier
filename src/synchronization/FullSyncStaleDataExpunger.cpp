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

#include "FullSyncStaleDataExpunger.h"

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Future.h>
#include <quentier/threading/TrackedTask.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/cancelers/ICanceler.h>

#include <utility>

namespace quentier::synchronization {

FullSyncStaleDataExpunger::FullSyncStaleDataExpunger(
    local_storage::ILocalStoragePtr localStorage,
    utility::cancelers::ICancelerPtr canceler) :
    m_localStorage{std::move(localStorage)},
    m_canceler{std::move(canceler)}
{
    if (Q_UNLIKELY(!m_localStorage)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "FullSyncStaleDataExpunger ctor: local storage is null")}};
    }

    if (Q_UNLIKELY(!m_canceler)) {
        throw InvalidArgument{ErrorString{QStringLiteral(
            "FullSyncStaleDataExpunger ctor: canceler is null")}};
    }
}

QFuture<void> FullSyncStaleDataExpunger::expungeStaleData(
    PreservedGuids preservedGuids,
    std::optional<qevercloud::Guid> linkedNotebookGuid)
{
    const local_storage::ILocalStorage::ListGuidsFilters modifiedFilters{
        local_storage::ILocalStorage::ListObjectsFilter::Include, // modified
        std::nullopt,                                             // favorited
    };

    const local_storage::ILocalStorage::ListGuidsFilters unmodifiedFilters{
        local_storage::ILocalStorage::ListObjectsFilter::Exclude, // modified
        std::nullopt,                                             // favorited
    };

    auto listModifiedNotebookGuidsFuture =
        m_localStorage->listNotebookGuids(modifiedFilters, linkedNotebookGuid);

    auto listUnmodifiedNotebookGuidsFuture = m_localStorage->listNotebookGuids(
        unmodifiedFilters, linkedNotebookGuid);

    auto listModifiedTagGuidsFuture =
        m_localStorage->listTagGuids(modifiedFilters, linkedNotebookGuid);

    auto listUnmodifiedTagGuidsFuture =
        m_localStorage->listTagGuids(unmodifiedFilters, linkedNotebookGuid);

    auto listModifiedNoteGuidsFuture =
        m_localStorage->listNoteGuids(modifiedFilters, linkedNotebookGuid);

    auto listUnmodifiedNoteGuidsFuture =
        m_localStorage->listNoteGuids(unmodifiedFilters, linkedNotebookGuid);

    auto listModifiedSavedSearchGuidsFuture =
        (linkedNotebookGuid == std::nullopt
             ? m_localStorage->listSavedSearchGuids(modifiedFilters)
             : threading::makeReadyFuture<QSet<qevercloud::Guid>>({}));

    auto listUnmodifiedSavedSearchGuidsFuture =
        (linkedNotebookGuid == std::nullopt
             ? m_localStorage->listSavedSearchGuids(unmodifiedFilters)
             : threading::makeReadyFuture<QSet<qevercloud::Guid>>({}));

    auto listAllFuture = threading::whenAll<QSet<qevercloud::Guid>>(
        QList<QFuture<QSet<qevercloud::Guid>>>{}
        << listModifiedNotebookGuidsFuture << listUnmodifiedNotebookGuidsFuture
        << listModifiedTagGuidsFuture << listUnmodifiedTagGuidsFuture
        << listModifiedNoteGuidsFuture << listUnmodifiedNoteGuidsFuture
        << listModifiedSavedSearchGuidsFuture
        << listUnmodifiedSavedSearchGuidsFuture);

    auto promise = std::make_shared<QPromise<void>>();
    auto future = promise->future();
    promise->start();

    auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(listAllFuture), promise,
        threading::TrackedTask{
            selfWeak,
            [this, promise, linkedNotebookGuid = std::move(linkedNotebookGuid),
             preservedGuids = std::move(preservedGuids)](
                QList<QSet<qevercloud::Guid>> result) mutable {
                if (m_canceler->isCanceled()) {
                    return;
                }

                Q_ASSERT(result.size() == 8);

                Guids guids;
                guids.locallyModifiedNotebookGuids = std::move(result[0]);
                guids.unmodifiedNotebookGuids = std::move(result[1]);
                guids.locallyModifiedTagGuids = std::move(result[2]);
                guids.unmodifiedTagGuids = std::move(result[3]);
                guids.locallyModifiedNoteGuids = std::move(result[4]);
                guids.unmodifiedNoteGuids = std::move(result[5]);
                guids.locallyModifiedSavedSearchGuids = std::move(result[6]);
                guids.unmodifiedSavedSearchGuids = std::move(result[7]);

                onGuidsListed(
                    guids, preservedGuids, std::move(linkedNotebookGuid),
                    promise);
            }});

    return future;
}

void FullSyncStaleDataExpunger::onGuidsListed(
    const FullSyncStaleDataExpunger::Guids & guids,
    const PreservedGuids & preservedGuids,
    std::optional<qevercloud::Guid> linkedNotebookGuid,
    const std::shared_ptr<QPromise<void>> & promise)
{
    QSet<qevercloud::Guid> notebookGuidsToExpunge;
    QSet<qevercloud::Guid> tagGuidsToExpunge;
    QSet<qevercloud::Guid> noteGuidsToExpunge;
    QSet<qevercloud::Guid> savedSearchGuidsToExpunge;

    QSet<qevercloud::Guid> notebookGuidsToReCreateLocally;
    QSet<qevercloud::Guid> tagGuidsToReCreateLocally;
    QSet<qevercloud::Guid> noteGuidsToUpdate;
    QSet<qevercloud::Guid> savedSearchGuidsToUpdate;

    const auto processGuids = [](const QSet<qevercloud::Guid> & modifiedGuids,
                                 const QSet<qevercloud::Guid> & unmodifiedGuids,
                                 const QSet<qevercloud::Guid> & preservedGuids,
                                 QSet<qevercloud::Guid> & guidsToExpunge,
                                 QSet<qevercloud::Guid> & guidsToUpdate) {
        for (const auto & guid: qAsConst(modifiedGuids)) {
            if (preservedGuids.contains(guid)) {
                continue;
            }

            guidsToUpdate.insert(guid);
        }

        for (const auto & guid: qAsConst(unmodifiedGuids)) {
            if (preservedGuids.contains(guid)) {
                continue;
            }

            guidsToExpunge.insert(guid);
        }
    };

    processGuids(
        guids.locallyModifiedNotebookGuids, guids.unmodifiedNotebookGuids,
        preservedGuids.notebookGuids, notebookGuidsToExpunge,
        notebookGuidsToReCreateLocally);

    processGuids(
        guids.locallyModifiedTagGuids, guids.unmodifiedTagGuids,
        preservedGuids.tagGuids, tagGuidsToExpunge, tagGuidsToReCreateLocally);

    processGuids(
        guids.locallyModifiedNoteGuids, guids.unmodifiedNoteGuids,
        preservedGuids.noteGuids, noteGuidsToExpunge, noteGuidsToUpdate);

    if (!linkedNotebookGuid) {
        processGuids(
            guids.locallyModifiedSavedSearchGuids,
            guids.unmodifiedSavedSearchGuids, preservedGuids.savedSearchGuids,
            savedSearchGuidsToExpunge, savedSearchGuidsToUpdate);
    }

    QList<QFuture<void>> expungeFutures;
    expungeFutures.reserve(std::max(
        0,
        notebookGuidsToExpunge.size() + tagGuidsToExpunge.size() +
            noteGuidsToExpunge.size() + savedSearchGuidsToExpunge.size()));

    for (const auto & guid: qAsConst(noteGuidsToExpunge)) {
        auto expungeNoteFuture = m_localStorage->expungeNoteByGuid(guid);
        expungeFutures << expungeNoteFuture;
    }

    for (const auto & guid: qAsConst(notebookGuidsToExpunge)) {
        auto expungeNotebookFuture =
            m_localStorage->expungeNotebookByGuid(guid);

        expungeFutures << expungeNotebookFuture;
    }

    for (const auto & guid: qAsConst(tagGuidsToExpunge)) {
        auto expungeTagFuture = m_localStorage->expungeTagByGuid(guid);
        expungeFutures << expungeTagFuture;
    }

    for (const auto & guid: qAsConst(savedSearchGuidsToExpunge)) {
        auto expungeSavedSearchFuture =
            m_localStorage->expungeSavedSearchByGuid(guid);

        expungeFutures << expungeSavedSearchFuture;
    }

    QFuture<void> expungeAllFuture =
        (expungeFutures.isEmpty() ? threading::makeReadyFuture()
                                  : threading::whenAll(expungeFutures));

    auto newNotebooksMapFuture = processModifiedNotebooks(
        notebookGuidsToReCreateLocally, linkedNotebookGuid);

    auto newTagsMapFuture =
        processModifiedTags(tagGuidsToReCreateLocally, linkedNotebookGuid);

    auto processSavedSearchesFuture =
        processModifiedSavedSearches(savedSearchGuidsToUpdate);

    auto newNotebooksMap = std::make_shared<GuidToLocalIdHash>();
    auto newTagsMap = std::make_shared<GuidToTagDataHash>();

    auto processNotebooksFuture = [&] {
        auto p = std::make_shared<QPromise<void>>();
        auto future = p->future();
        p->start();

        threading::thenOrFailed(
            std::move(newNotebooksMapFuture), p,
            [p, newNotebooksMap](GuidToLocalIdHash hash) {
                *newNotebooksMap = std::move(hash);
                p->finish();
            });

        return future;
    }();

    auto processTagsFuture = [&] {
        auto p = std::make_shared<QPromise<void>>();
        auto future = p->future();
        p->start();

        threading::thenOrFailed(
            std::move(newTagsMapFuture), p,
            [p, newTagsMap](GuidToTagDataHash hash) {
                *newTagsMap = std::move(hash);
                p->finish();
            });

        return future;
    }();

    auto allButNotesFuture = threading::whenAll(
        QList<QFuture<void>>{} << processSavedSearchesFuture
                               << processNotebooksFuture << processTagsFuture);

    auto selfWeak = weak_from_this();

    threading::thenOrFailed(
        std::move(allButNotesFuture), promise,
        [promise, newNotebooksMap, newTagsMap, selfWeak,
         noteGuidsToUpdate = std::move(noteGuidsToUpdate),
         linkedNotebookGuid = std::move(linkedNotebookGuid)] {
            const auto self = selfWeak.lock();
            if (!self || self->m_canceler->isCanceled()) {
                return;
            }

            auto processNotesFuture = self->processModifiedNotes(
                noteGuidsToUpdate, newNotebooksMap, newTagsMap);

            threading::thenOrFailed(std::move(processNotesFuture), promise);
        });
}

QFuture<FullSyncStaleDataExpunger::GuidToLocalIdHash>
    FullSyncStaleDataExpunger::processModifiedNotebooks(
        const QSet<qevercloud::Guid> & notebookGuids,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid)
{
    if (notebookGuids.isEmpty()) {
        return threading::makeReadyFuture<GuidToLocalIdHash>({});
    }

    const auto selfWeak = weak_from_this();

    QList<QFuture<std::pair<qevercloud::Guid, QString>>> processNotebookFutures;
    processNotebookFutures.reserve(std::max(0, notebookGuids.size()));
    for (const auto & guid: qAsConst(notebookGuids)) {
        QFuture<std::optional<qevercloud::Notebook>> notebookFuture =
            m_localStorage->findNotebookByGuid(guid);

        QFuture<std::pair<qevercloud::Guid, QString>> processNotebookFuture =
            threading::then(
                std::move(notebookFuture),
                [guid, linkedNotebookGuid,
                 selfWeak](std::optional<qevercloud::Notebook> notebook) {
                    if (Q_UNLIKELY(!notebook)) {
                        QNWARNING(
                            "synchronization::FullSyncStaleDataExpunger",
                            "Could not find the supposedly existing notebook "
                                << "in the local storage by guid: " << guid);

                        return threading::makeReadyFuture<
                            std::pair<qevercloud::Guid, QString>>({});
                    }

                    const auto self = selfWeak.lock();
                    if (!self || self->m_canceler->isCanceled()) {
                        return threading::makeReadyFuture<
                            std::pair<qevercloud::Guid, QString>>({});
                    }

                    auto promise = std::make_shared<
                        QPromise<std::pair<qevercloud::Guid, QString>>>();

                    auto future = promise->future();
                    promise->start();

                    notebook->setGuid(std::nullopt);
                    notebook->setLinkedNotebookGuid(linkedNotebookGuid);
                    notebook->setUpdateSequenceNum(std::nullopt);
                    notebook->setRestrictions(std::nullopt);
                    notebook->setContact(std::nullopt);
                    notebook->setPublished(std::nullopt);
                    notebook->setPublishing(std::nullopt);
                    notebook->setDefaultNotebook(std::nullopt);
                    notebook->setLocallyModified(true);

                    const auto newLocalId = UidGenerator::Generate();
                    notebook->setLocalId(newLocalId);

                    auto expungeNotebookFuture =
                        self->m_localStorage->expungeNotebookByGuid(guid);

                    threading::thenOrFailed(
                        std::move(expungeNotebookFuture), promise,
                        [promise, guid, newLocalId, selfWeak,
                         notebook = std::move(*notebook)]() mutable {
                            const auto self = selfWeak.lock();
                            if (!self || self->m_canceler->isCanceled()) {
                                promise->addResult(
                                    std::pair<qevercloud::Guid, QString>{});
                                promise->finish();
                                return;
                            }

                            auto putNotebookFuture =
                                self->m_localStorage->putNotebook(
                                    std::move(notebook));

                            threading::thenOrFailed(
                                std::move(putNotebookFuture), promise,
                                [promise, guid, newLocalId] {
                                    promise->addResult(
                                        std::pair{guid, newLocalId});
                                    promise->finish();
                                });
                        });

                    return future;
                });

        processNotebookFutures << processNotebookFuture;
    }

    auto allNotebooksFuture =
        threading::whenAll<std::pair<qevercloud::Guid, QString>>(
            std::move(processNotebookFutures));

    auto promise = std::make_shared<QPromise<GuidToLocalIdHash>>();
    auto future = promise->future();
    promise->start();

    threading::thenOrFailed(
        std::move(allNotebooksFuture), promise,
        [promise, canceler = m_canceler](
            QList<std::pair<qevercloud::Guid, QString>> pairs) {
            if (canceler->isCanceled()) {
                return;
            }

            GuidToLocalIdHash hash;
            hash.reserve(pairs.size());
            for (auto & pair: pairs) {
                hash[pair.first] = std::move(pair.second);
            }

            promise->addResult(std::move(hash));
            promise->finish();
        });

    return future;
}

QFuture<FullSyncStaleDataExpunger::GuidToTagDataHash>
    FullSyncStaleDataExpunger::processModifiedTags(
        const QSet<qevercloud::Guid> & tagGuids,
        const std::optional<qevercloud::Guid> & linkedNotebookGuid)
{
    if (tagGuids.isEmpty()) {
        return threading::makeReadyFuture<GuidToTagDataHash>({});
    }

    auto selfWeak = weak_from_this();

    QList<QFuture<std::pair<qevercloud::Guid, TagData>>> processTagFutures;
    processTagFutures.reserve(std::max(0, tagGuids.size()));
    for (const auto & guid: qAsConst(tagGuids)) {
        QFuture<std::optional<qevercloud::Tag>> tagFuture =
            m_localStorage->findTagByGuid(guid);

        QFuture<std::pair<qevercloud::Guid, TagData>> processTagFuture =
            threading::then(
                std::move(tagFuture),
                [guid, linkedNotebookGuid,
                 selfWeak](std::optional<qevercloud::Tag> tag) {
                    if (Q_UNLIKELY(!tag)) {
                        QNWARNING(
                            "synchronization::FullSyncStaleDataExpunger",
                            "Could not find the supposedly existing tag "
                                << "in the local storage by guid: " << guid);

                        return threading::makeReadyFuture<
                            std::pair<qevercloud::Guid, TagData>>({});
                    }

                    const auto self = selfWeak.lock();
                    if (!self || self->m_canceler->isCanceled()) {
                        return threading::makeReadyFuture<
                            std::pair<qevercloud::Guid, TagData>>({});
                    }

                    auto promise = std::make_shared<
                        QPromise<std::pair<qevercloud::Guid, TagData>>>();

                    auto future = promise->future();
                    promise->start();

                    tag->setGuid(std::nullopt);
                    tag->setLinkedNotebookGuid(linkedNotebookGuid);
                    tag->setUpdateSequenceNum(std::nullopt);
                    tag->setParentGuid(std::nullopt);
                    tag->setParentTagLocalId(QString{});
                    tag->setLocallyModified(true);

                    auto oldLocalId = tag->localId();
                    auto newLocalId = UidGenerator::Generate();
                    tag->setLocalId(newLocalId);

                    auto expungeTagFuture =
                        self->m_localStorage->expungeTagByGuid(guid);

                    auto expungeTagThenFuture = threading::then(
                        std::move(expungeTagFuture),
                        [promise, guid, selfWeak,
                         oldLocalId = std::move(oldLocalId),
                         newLocalId = std::move(newLocalId),
                         tag = std::move(*tag)]() mutable {
                            const auto self = selfWeak.lock();
                            if (!self || self->m_canceler->isCanceled()) {
                                promise->addResult(
                                    std::pair<qevercloud::Guid, TagData>{});
                                promise->finish();
                                return;
                            }

                            auto putTagFuture =
                                self->m_localStorage->putTag(std::move(tag));

                            auto putTagThenFuture = threading::then(
                                std::move(putTagFuture),
                                [promise, guid,
                                 oldLocalId = std::move(oldLocalId),
                                 newLocalId = std::move(newLocalId)]() mutable {
                                    promise->addResult(std::pair{
                                        guid,
                                        TagData{
                                            std::move(oldLocalId),
                                            std::move(newLocalId)}});
                                    promise->finish();
                                });

                            threading::onFailed(
                                std::move(putTagThenFuture),
                                [promise](const QException & e) {
                                    QNWARNING(
                                        "synchronization::"
                                        "FullSyncStaleDataExpunger",
                                        "Failed to put recreated locally "
                                            << "modified tag to the "
                                            << "local storage: " << e.what());

                                    promise->addResult(
                                        std::pair<qevercloud::Guid, TagData>{});
                                    promise->finish();
                                });
                        });

                    threading::onFailed(
                        std::move(expungeTagThenFuture),
                        [promise]([[maybe_unused]] const QException & e) {
                            promise->addResult(
                                std::pair<qevercloud::Guid, TagData>{});
                            promise->finish();
                        });

                    return future;
                });

        processTagFutures << processTagFuture;
    }

    auto allTagsFuture =
        threading::whenAll<std::pair<qevercloud::Guid, TagData>>(
            std::move(processTagFutures));

    auto promise = std::make_shared<QPromise<GuidToTagDataHash>>();
    auto future = promise->future();
    promise->start();

    threading::thenOrFailed(
        std::move(allTagsFuture), promise,
        [promise, canceler = m_canceler](
            QList<std::pair<qevercloud::Guid, TagData>> pairs) {
            if (canceler->isCanceled()) {
                return;
            }

            GuidToTagDataHash hash;
            hash.reserve(pairs.size());
            for (auto & pair: pairs) {
                hash[pair.first] = std::move(pair.second);
            }

            promise->addResult(std::move(hash));
            promise->finish();
        });

    return future;
}

QFuture<void> FullSyncStaleDataExpunger::processModifiedSavedSearches(
    const QSet<qevercloud::Guid> & savedSearchGuids)
{
    if (savedSearchGuids.isEmpty()) {
        return threading::makeReadyFuture();
    }

    const auto selfWeak = weak_from_this();

    QList<QFuture<void>> processSavedSearchFutures;
    processSavedSearchFutures.reserve(std::max(0, savedSearchGuids.size()));
    for (const auto & guid: qAsConst(savedSearchGuids)) {
        QFuture<std::optional<qevercloud::SavedSearch>> savedSearchFuture =
            m_localStorage->findSavedSearchByGuid(guid);

        QFuture<void> processSavedSearchFuture = threading::then(
            std::move(savedSearchFuture),
            [guid,
             selfWeak](std::optional<qevercloud::SavedSearch> savedSearch) {
                if (Q_UNLIKELY(!savedSearch)) {
                    QNWARNING(
                        "synchronization::FullSyncStaleDataExpunger",
                        "Could not find the supposedly existing saved "
                            << "search in the local storage by guid: " << guid);

                    return threading::makeReadyFuture();
                }

                const auto self = selfWeak.lock();
                if (!self || self->m_canceler->isCanceled()) {
                    return threading::makeReadyFuture();
                }

                auto promise = std::make_shared<QPromise<void>>();
                auto future = promise->future();
                promise->start();

                savedSearch->setGuid(std::nullopt);
                savedSearch->setUpdateSequenceNum(std::nullopt);
                savedSearch->setLocalId(UidGenerator::Generate());
                savedSearch->setLocallyModified(true);

                auto expungeSavedSearchFuture =
                    self->m_localStorage->expungeSavedSearchByGuid(guid);

                threading::thenOrFailed(
                    std::move(expungeSavedSearchFuture), promise,
                    [promise, guid, selfWeak,
                     savedSearch = std::move(*savedSearch)]() mutable {
                        const auto self = selfWeak.lock();
                        if (!self || self->m_canceler->isCanceled()) {
                            promise->finish();
                            return;
                        }

                        auto putSavedSearchFuture =
                            self->m_localStorage->putSavedSearch(
                                std::move(savedSearch));

                        threading::thenOrFailed(
                            std::move(putSavedSearchFuture), promise);
                    });

                return future;
            });

        processSavedSearchFutures << processSavedSearchFuture;
    }

    return threading::whenAll(std::move(processSavedSearchFutures));
}

QFuture<void> FullSyncStaleDataExpunger::processModifiedNotes(
    const QSet<qevercloud::Guid> & noteGuids,
    const std::shared_ptr<const GuidToLocalIdHash> & newNotebooksMap,
    const std::shared_ptr<const GuidToTagDataHash> & newTagsMap)
{
    if (noteGuids.isEmpty()) {
        return threading::makeReadyFuture();
    }

    const auto selfWeak = weak_from_this();

    const auto fetchNoteOptions =
        local_storage::ILocalStorage::FetchNoteOptions{} |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceMetadata |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    QList<QFuture<void>> processNoteFutures;
    processNoteFutures.reserve(std::max(0, noteGuids.size()));
    for (const auto & guid: qAsConst(noteGuids)) {
        QFuture<std::optional<qevercloud::Note>> noteFuture =
            m_localStorage->findNoteByGuid(guid, fetchNoteOptions);

        QFuture<void> processNoteFuture = threading::then(
            std::move(noteFuture),
            [guid, selfWeak, newNotebooksMap,
             newTagsMap](std::optional<qevercloud::Note> note) {
                if (Q_UNLIKELY(!note)) {
                    QNWARNING(
                        "synchronization::FullSyncStaleDataExpunger",
                        "Could not find the supposedly existing note in the "
                            << "local storage by guid: " << guid);
                    return threading::makeReadyFuture();
                }

                if (Q_UNLIKELY(!note->notebookGuid())) {
                    QNWARNING(
                        "synchronization::FullSyncStaleDataExpunger",
                        "Found note with guid which somehow doesn't have "
                            << "notebook guid: " << *note);
                    return threading::makeReadyFuture();
                }

                const auto self = selfWeak.lock();
                if (!self || self->m_canceler->isCanceled()) {
                    return threading::makeReadyFuture();
                }

                auto promise = std::make_shared<QPromise<void>>();
                auto future = promise->future();
                promise->start();

                note->setGuid(std::nullopt);
                note->setUpdateSequenceNum(std::nullopt);
                note->setLocalId(UidGenerator::Generate());
                note->setLocallyModified(true);

                if (const auto it =
                        newNotebooksMap->constFind(*note->notebookGuid());
                    it != newNotebooksMap->constEnd())
                {
                    note->setNotebookLocalId(it.value());
                }

                note->setNotebookGuid(std::nullopt);

                if (note->resources()) {
                    for (auto & resource: *note->mutableResources()) {
                        resource.setNoteLocalId(note->localId());
                        resource.setNoteGuid(std::nullopt);
                        resource.setGuid(std::nullopt);
                        resource.setUpdateSequenceNum(std::nullopt);
                        resource.setLocallyModified(true);
                        resource.setLocalId(UidGenerator::Generate());
                    }
                }

                if (note->tagGuids()) {
                    QStringList tagLocalIds = note->tagLocalIds();
                    for (const auto & tagGuid: qAsConst(*note->tagGuids())) {
                        const auto it = newTagsMap->constFind(tagGuid);
                        if (it != newTagsMap->constEnd()) {
                            const auto & tagData = it.value();
                            if (const auto index =
                                    tagLocalIds.indexOf(tagData.oldLocalId);
                                index != -1) {
                                tagLocalIds[index] = tagData.newLocalId;
                            }
                        }
                    }
                    note->setTagLocalIds(std::move(tagLocalIds));
                    note->setTagGuids(std::nullopt);
                }

                auto expungeNoteFuture =
                    self->m_localStorage->expungeNoteByGuid(guid);

                threading::thenOrFailed(
                    std::move(expungeNoteFuture), promise,
                    [promise, guid, selfWeak,
                     note = std::move(*note)]() mutable {
                        const auto self = selfWeak.lock();
                        if (!self || self->m_canceler->isCanceled()) {
                            promise->finish();
                            return;
                        }

                        auto putNoteFuture =
                            self->m_localStorage->putNote(std::move(note));

                        threading::thenOrFailed(
                            std::move(putNoteFuture), promise);
                    });

                return future;
            });

        processNoteFutures << processNoteFuture;
    }

    return threading::whenAll(std::move(processNoteFutures));
}

} // namespace quentier::synchronization
