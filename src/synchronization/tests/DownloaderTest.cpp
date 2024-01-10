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

#include "Utils.h"

#include <synchronization/Downloader.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/exception/RuntimeError.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/tests/mocks/MockISyncStateStorage.h>
#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/Unreachable.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockIDownloader.h>
#include <synchronization/tests/mocks/MockIDurableNotesProcessor.h>
#include <synchronization/tests/mocks/MockIDurableResourcesProcessor.h>
#include <synchronization/tests/mocks/MockIFullSyncStaleDataExpunger.h>
#include <synchronization/tests/mocks/MockILinkedNotebooksProcessor.h>
#include <synchronization/tests/mocks/MockILinkedNotebookTagsCleaner.h>
#include <synchronization/tests/mocks/MockINoteStoreProvider.h>
#include <synchronization/tests/mocks/MockINotebooksProcessor.h>
#include <synchronization/tests/mocks/MockISavedSearchesProcessor.h>
#include <synchronization/tests/mocks/MockISyncChunksProvider.h>
#include <synchronization/tests/mocks/MockITagsProcessor.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>
#include <synchronization/types/AuthenticationInfo.h>
#include <synchronization/types/DownloadNotesStatus.h>
#include <synchronization/types/DownloadResourcesStatus.h>
#include <synchronization/types/SyncChunksDataCounters.h>
#include <synchronization/types/SyncState.h>

#include <qevercloud/types/builders/DataBuilder.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/SyncChunkBuilder.h>
#include <qevercloud/types/builders/SyncStateBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>
#include <qevercloud/types/builders/UserBuilder.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QFlags>
#include <QList>

#include <gtest/gtest.h>

#include <array>
#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::AtMost;
using testing::Eq;
using testing::InSequence;
using testing::Ne;
using testing::Return;
using testing::StrictMock;

namespace {

enum class SyncChunksFlag
{
    WithLinkedNotebooks = 1 << 0,
    WithNotes = 1 << 1,
    WithNotebooks = 1 << 2,
    WithResources = 1 << 3,
    WithSavedSearches = 1 << 4,
    WithTags = 1 << 5,
};

Q_DECLARE_FLAGS(SyncChunksFlags, SyncChunksFlag);

[[nodiscard]] QList<qevercloud::SyncChunk> generateSyncChunks(
    const SyncChunksFlags flags, const qint32 afterUsn = 0, // NOLINT
    const qint32 syncChunksCount = 3,
    const qint32 itemCountPerSyncChunk = 3) // NOLINT
{
    Q_ASSERT(syncChunksCount > 0);
    Q_ASSERT(itemCountPerSyncChunk > 0);
    Q_ASSERT(afterUsn >= 0);

    QList<qevercloud::SyncChunk> result;
    result.reserve(syncChunksCount);

    qint32 usn = afterUsn + 1;

    for (qint32 i = 0; i < syncChunksCount; ++i) {
        qevercloud::SyncChunkBuilder builder;

        if (flags.testFlag(SyncChunksFlag::WithLinkedNotebooks)) {
            QList<qevercloud::LinkedNotebook> linkedNotebooks;
            linkedNotebooks.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedLinkedNotebooks;
            expungedLinkedNotebooks.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                linkedNotebooks
                    << qevercloud::LinkedNotebookBuilder{}
                           .setGuid(UidGenerator::Generate())
                           .setUpdateSequenceNum(usn++)
                           .setUsername(QString::fromUtf8("Linked notebook #%1")
                                            .arg(j + 1))
                           .build();

                expungedLinkedNotebooks << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(
                linkedNotebooks.last().updateSequenceNum().value());

            builder.setLinkedNotebooks(std::move(linkedNotebooks));

            builder.setExpungedLinkedNotebooks(
                std::move(expungedLinkedNotebooks));
        }

        if (flags.testFlag(SyncChunksFlag::WithNotes)) {
            QList<qevercloud::Note> notes;
            notes.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedNotes;
            expungedNotes.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                notes << qevercloud::NoteBuilder{}
                             .setGuid(UidGenerator::Generate())
                             .setUpdateSequenceNum(usn++)
                             .setTitle(QString::fromUtf8("Note #%1").arg(j + 1))
                             .setNotebookGuid(UidGenerator::Generate())
                             .build();

                expungedNotes << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(notes.last().updateSequenceNum().value());
            builder.setNotes(std::move(notes));
            builder.setExpungedNotes(std::move(expungedNotes));
        }

        if (flags.testFlag(SyncChunksFlag::WithNotebooks)) {
            QList<qevercloud::Notebook> notebooks;
            notebooks.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedNotebooks;
            expungedNotebooks.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                notebooks << qevercloud::NotebookBuilder{}
                                 .setGuid(UidGenerator::Generate())
                                 .setUpdateSequenceNum(usn++)
                                 .setName(QString::fromUtf8("Notebook #%1")
                                              .arg(j + 1))
                                 .build();

                expungedNotebooks << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(
                notebooks.last().updateSequenceNum().value());

            builder.setNotebooks(std::move(notebooks));
            builder.setExpungedNotebooks(std::move(expungedNotebooks));
        }

        if (flags.testFlag(SyncChunksFlag::WithResources)) {
            QList<qevercloud::Resource> resources;
            resources.reserve(itemCountPerSyncChunk);
            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                QByteArray dataBody =
                    QString::fromUtf8("Resource #%1").arg(j + 1).toUtf8();
                const auto dataBodySize = dataBody.size();
                QByteArray dataBodyHash =
                    QCryptographicHash::hash(dataBody, QCryptographicHash::Md5);

                resources << qevercloud::ResourceBuilder{}
                                 .setGuid(UidGenerator::Generate())
                                 .setUpdateSequenceNum(usn++)
                                 .setNoteGuid(UidGenerator::Generate())
                                 .setData(
                                     qevercloud::DataBuilder{}
                                         .setBody(std::move(dataBody))
                                         .setSize(
                                             static_cast<qint32>(dataBodySize))
                                         .setBodyHash(std::move(dataBodyHash))
                                         .build())
                                 .build();
            }
            builder.setChunkHighUSN(
                resources.last().updateSequenceNum().value());

            builder.setResources(std::move(resources));
        }

        if (flags.testFlag(SyncChunksFlag::WithSavedSearches)) {
            QList<qevercloud::SavedSearch> savedSearches;
            savedSearches.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedSavedSearches;
            expungedSavedSearches.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                savedSearches
                    << qevercloud::SavedSearchBuilder{}
                           .setGuid(UidGenerator::Generate())
                           .setUpdateSequenceNum(usn++)
                           .setName(
                               QString::fromUtf8("Saved search #%1").arg(j + 1))
                           .build();

                expungedSavedSearches << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(
                savedSearches.last().updateSequenceNum().value());

            builder.setSearches(std::move(savedSearches));
            builder.setExpungedSearches(std::move(expungedSavedSearches));
        }

        if (flags.testFlag(SyncChunksFlag::WithTags)) {
            QList<qevercloud::Tag> tags;
            tags.reserve(itemCountPerSyncChunk);

            QList<qevercloud::Guid> expungedTags;
            expungedTags.reserve(itemCountPerSyncChunk);

            for (qint32 j = 0; j < itemCountPerSyncChunk; ++j) {
                tags << qevercloud::TagBuilder{}
                            .setGuid(UidGenerator::Generate())
                            .setUpdateSequenceNum(usn++)
                            .setName(QString::fromUtf8("Tag #%1").arg(j + 1))
                            .build();

                expungedTags << UidGenerator::Generate();
            }
            builder.setChunkHighUSN(tags.last().updateSequenceNum().value());
            builder.setTags(std::move(tags));
            builder.setExpungedTags(std::move(expungedTags));
        }

        result << builder.build();
    }

    if (!result.isEmpty()) {
        const qint32 updateCount = result.constLast().chunkHighUSN().value();
        for (auto & syncChunk: result) {
            syncChunk.setUpdateCount(updateCount);
        }
    }

    return result;
}

[[nodiscard]] qint32 syncChunksNoteCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.notes()) {
            result += syncChunk.notes()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksExpungedNoteCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.expungedNotes()) {
            result += syncChunk.expungedNotes()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksResourceCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.resources()) {
            result += syncChunk.resources()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksNotebookCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.notebooks()) {
            result += syncChunk.notebooks()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksExpungedNotebookCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.expungedNotebooks()) {
            result += syncChunk.expungedNotebooks()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksTagCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.tags()) {
            result += syncChunk.tags()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksExpungedTagCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.expungedTags()) {
            result += syncChunk.expungedTags()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksSavedSearchCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.searches()) {
            result += syncChunk.searches()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksExpungedSavedSearchCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.expungedSearches()) {
            result += syncChunk.expungedSearches()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksLinkedNotebookCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.linkedNotebooks()) {
            result += syncChunk.linkedNotebooks()->size();
        }
    }
    return result;
}

[[nodiscard]] qint32 syncChunksExpungedLinkedNotebookCount(
    const QList<qevercloud::SyncChunk> & syncChunks) noexcept
{
    qint32 result = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.expungedLinkedNotebooks()) {
            result += syncChunk.expungedLinkedNotebooks()->size();
        }
    }
    return result;
}

struct SyncChunksItemCounts
{
    SyncChunksItemCounts(const QList<qevercloud::SyncChunk> & syncChunks) :
        totalSavedSearches{syncChunksSavedSearchCount(syncChunks)},
        totalExpungedSavedSearches{
            syncChunksExpungedSavedSearchCount(syncChunks)},
        totalNotebooks{syncChunksNotebookCount(syncChunks)},
        totalExpungedNotebooks{syncChunksExpungedNotebookCount(syncChunks)},
        totalTags{syncChunksTagCount(syncChunks)},
        totalExpungedTags{syncChunksExpungedTagCount(syncChunks)},
        totalLinkedNotebooks{syncChunksLinkedNotebookCount(syncChunks)},
        totalExpungedLinkedNotebooks{
            syncChunksExpungedLinkedNotebookCount(syncChunks)},
        totalNotes{syncChunksNoteCount(syncChunks)},
        totalExpungedNotes{syncChunksExpungedNoteCount(syncChunks)},
        totalResources{syncChunksResourceCount(syncChunks)}
    {}

    qint32 totalSavedSearches{0};
    qint32 totalExpungedSavedSearches{0};
    qint32 totalNotebooks{0};
    qint32 totalExpungedNotebooks{0};
    qint32 totalTags{0};
    qint32 totalExpungedTags{0};
    qint32 totalLinkedNotebooks{0};
    qint32 totalExpungedLinkedNotebooks{0};
    qint32 totalNotes{0};
    qint32 totalExpungedNotes{0};
    qint32 totalResources{0};
};

void checkSyncChunksDataCounters(
    const SyncChunksItemCounts & syncChunksItemCounts,
    const ISyncChunksDataCounters & syncChunksDataCounters)
{
    EXPECT_EQ(
        syncChunksDataCounters.totalSavedSearches(),
        syncChunksItemCounts.totalSavedSearches);

    EXPECT_EQ(
        syncChunksDataCounters.totalExpungedSavedSearches(),
        syncChunksItemCounts.totalExpungedSavedSearches);

    EXPECT_EQ(
        syncChunksDataCounters.totalNotebooks(),
        syncChunksItemCounts.totalNotebooks);

    EXPECT_EQ(
        syncChunksDataCounters.totalExpungedNotebooks(),
        syncChunksItemCounts.totalExpungedNotebooks);

    EXPECT_EQ(
        syncChunksDataCounters.totalTags(), syncChunksItemCounts.totalTags);

    EXPECT_EQ(
        syncChunksDataCounters.totalExpungedTags(),
        syncChunksItemCounts.totalExpungedTags);

    EXPECT_EQ(
        syncChunksDataCounters.totalLinkedNotebooks(),
        syncChunksItemCounts.totalLinkedNotebooks);

    EXPECT_EQ(
        syncChunksDataCounters.totalExpungedLinkedNotebooks(),
        syncChunksItemCounts.totalExpungedLinkedNotebooks);
}

void checkDownloadNotesStatus(
    const SyncChunksItemCounts & syncChunksItemCounts,
    const IDownloadNotesStatus & downloadNotesStatus)
{
    EXPECT_EQ(
        downloadNotesStatus.totalNewNotes() +
            downloadNotesStatus.totalUpdatedNotes(),
        syncChunksItemCounts.totalNotes);

    EXPECT_EQ(
        downloadNotesStatus.totalExpungedNotes(),
        syncChunksItemCounts.totalExpungedNotes);

    EXPECT_EQ(
        downloadNotesStatus.notesWhichFailedToDownload().size() +
            downloadNotesStatus.notesWhichFailedToProcess().size() +
            downloadNotesStatus.processedNoteGuidsAndUsns().size() +
            downloadNotesStatus.cancelledNoteGuidsAndUsns().size(),
        syncChunksItemCounts.totalNotes);

    EXPECT_EQ(
        downloadNotesStatus.expungedNoteGuids().size() +
            downloadNotesStatus.noteGuidsWhichFailedToExpunge().size(),
        syncChunksItemCounts.totalExpungedNotes);
}

void checkDownloadResourcesStatus(
    const SyncChunksItemCounts & syncChunksItemCounts,
    const IDownloadResourcesStatus & downloadResourcesStatus)
{
    EXPECT_EQ(
        downloadResourcesStatus.totalNewResources() +
            downloadResourcesStatus.totalUpdatedResources(),
        syncChunksItemCounts.totalResources);

    EXPECT_EQ(
        downloadResourcesStatus.resourcesWhichFailedToDownload().size() +
            downloadResourcesStatus.resourcesWhichFailedToProcess().size() +
            downloadResourcesStatus.processedResourceGuidsAndUsns().size() +
            downloadResourcesStatus.cancelledResourceGuidsAndUsns().size(),
        syncChunksItemCounts.totalResources);
}

void checkSyncChunksDataCountersUpdate(
    const ISyncChunksDataCounters & previousSyncChunksDataCounters,
    const ISyncChunksDataCounters & currentSyncChunksDataCounters)
{
    EXPECT_GE(
        currentSyncChunksDataCounters.totalSavedSearches(),
        previousSyncChunksDataCounters.totalSavedSearches());

    EXPECT_GE(
        currentSyncChunksDataCounters.totalExpungedSavedSearches(),
        previousSyncChunksDataCounters.totalExpungedSavedSearches());

    EXPECT_GE(
        currentSyncChunksDataCounters.addedSavedSearches(),
        previousSyncChunksDataCounters.addedSavedSearches());

    EXPECT_GE(
        currentSyncChunksDataCounters.updatedSavedSearches(),
        previousSyncChunksDataCounters.updatedSavedSearches());

    EXPECT_GE(
        currentSyncChunksDataCounters.expungedSavedSearches(),
        previousSyncChunksDataCounters.expungedSavedSearches());

    EXPECT_GE(
        currentSyncChunksDataCounters.totalTags(),
        previousSyncChunksDataCounters.totalTags());

    EXPECT_GE(
        currentSyncChunksDataCounters.totalExpungedTags(),
        previousSyncChunksDataCounters.totalExpungedTags());

    EXPECT_GE(
        currentSyncChunksDataCounters.addedTags(),
        previousSyncChunksDataCounters.addedTags());

    EXPECT_GE(
        currentSyncChunksDataCounters.updatedTags(),
        previousSyncChunksDataCounters.updatedTags());

    EXPECT_GE(
        currentSyncChunksDataCounters.expungedTags(),
        previousSyncChunksDataCounters.expungedTags());

    EXPECT_GE(
        currentSyncChunksDataCounters.totalLinkedNotebooks(),
        previousSyncChunksDataCounters.totalLinkedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.totalExpungedLinkedNotebooks(),
        previousSyncChunksDataCounters.totalExpungedLinkedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.addedLinkedNotebooks(),
        previousSyncChunksDataCounters.addedLinkedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.updatedLinkedNotebooks(),
        previousSyncChunksDataCounters.updatedLinkedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.expungedLinkedNotebooks(),
        previousSyncChunksDataCounters.expungedLinkedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.totalNotebooks(),
        previousSyncChunksDataCounters.totalNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.totalExpungedNotebooks(),
        previousSyncChunksDataCounters.totalExpungedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.addedNotebooks(),
        previousSyncChunksDataCounters.addedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.updatedNotebooks(),
        previousSyncChunksDataCounters.updatedNotebooks());

    EXPECT_GE(
        currentSyncChunksDataCounters.expungedNotebooks(),
        previousSyncChunksDataCounters.expungedNotebooks());
}

[[nodiscard]] QList<qevercloud::LinkedNotebook> collectLinkedNotebooks(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    QList<qevercloud::LinkedNotebook> result;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.linkedNotebooks()) {
            result << *syncChunk.linkedNotebooks();
        }
    }
    return result;
}

[[nodiscard]] IFullSyncStaleDataExpunger::PreservedGuids collectPreservedGuids(
    const QList<qevercloud::SyncChunk> & syncChunks)
{
    IFullSyncStaleDataExpunger::PreservedGuids preservedGuids;

    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (syncChunk.notebooks()) {
            for (const auto & notebook: std::as_const(*syncChunk.notebooks())) {
                preservedGuids.notebookGuids << notebook.guid().value();
            }
        }

        if (syncChunk.notes()) {
            for (const auto & note: std::as_const(*syncChunk.notes())) {
                preservedGuids.noteGuids << note.guid().value();
            }
        }

        if (syncChunk.tags()) {
            for (const auto & tag: std::as_const(*syncChunk.tags())) {
                preservedGuids.tagGuids << tag.guid().value();
            }
        }

        if (syncChunk.searches()) {
            for (const auto & savedSearch: std::as_const(*syncChunk.searches()))
            {
                preservedGuids.savedSearchGuids << savedSearch.guid().value();
            }
        }
    }

    return preservedGuids;
}

void emulateSyncChunksNotesProcessing(
    const QList<qevercloud::SyncChunk> & syncChunks,
    const std::shared_ptr<IDurableNotesProcessor::ICallback> & callback,
    DownloadNotesStatus & downloadNotesStatus)
{
    quint64 noteIndex = 0UL;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (!syncChunk.notes()) {
            continue;
        }

        for (const auto & note: std::as_const(*syncChunk.notes())) {
            if (noteIndex % 4 == 0) {
                const auto noteGuid = note.guid().value();
                const auto noteUsn = note.updateSequenceNum().value();

                if (callback) {
                    callback->onProcessedNote(noteGuid, noteUsn);
                }

                downloadNotesStatus.m_processedNoteGuidsAndUsns[noteGuid] =
                    noteUsn;
            }
            else if (noteIndex % 4 == 1) {
                auto exc =
                    std::make_shared<RuntimeError>(ErrorString{"some error"});

                if (callback) {
                    callback->onNoteFailedToDownload(note, *exc);
                }

                downloadNotesStatus.m_notesWhichFailedToDownload
                    << DownloadNotesStatus::NoteWithException{
                           note, std::move(exc)};
            }
            else if (noteIndex % 4 == 2) {
                auto exc =
                    std::make_shared<RuntimeError>(ErrorString{"some error"});

                if (callback) {
                    callback->onNoteFailedToProcess(note, *exc);
                }

                downloadNotesStatus.m_notesWhichFailedToProcess
                    << DownloadNotesStatus::NoteWithException{
                           note, std::move(exc)};
            }
            else if (noteIndex % 4 == 3) {
                if (callback) {
                    callback->onNoteProcessingCancelled(note);
                }

                const auto noteGuid = note.guid().value();
                const auto noteUsn = note.updateSequenceNum().value();

                downloadNotesStatus.m_cancelledNoteGuidsAndUsns[noteGuid] =
                    noteUsn;
            }
            else {
                EXPECT_FALSE(true) << "Unreachable code";
            }

            ++noteIndex;
        }
    }

    downloadNotesStatus.m_totalNewNotes = noteIndex / 2;
    downloadNotesStatus.m_totalUpdatedNotes =
        noteIndex - downloadNotesStatus.m_totalNewNotes;

    noteIndex = 0;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (!syncChunk.expungedNotes()) {
            continue;
        }

        for (const auto & expungedNoteGuid:
             std::as_const(*syncChunk.expungedNotes())) {
            if (noteIndex % 2 == 0) {
                if (callback) {
                    callback->onExpungedNote(expungedNoteGuid);
                }

                downloadNotesStatus.m_expungedNoteGuids << expungedNoteGuid;
            }
            else if (noteIndex % 2 == 1) {
                auto exc =
                    std::make_shared<RuntimeError>(ErrorString{"some error"});

                if (callback) {
                    callback->onFailedToExpungeNote(expungedNoteGuid, *exc);
                }

                downloadNotesStatus.m_noteGuidsWhichFailedToExpunge
                    << DownloadNotesStatus::GuidWithException{
                           expungedNoteGuid, std::move(exc)};
            }
            else {
                EXPECT_FALSE(true) << "Unreachable code";
            }

            ++noteIndex;
        }
    }

    downloadNotesStatus.m_totalExpungedNotes = noteIndex;
}

void emulateSyncChunksResourcesProcessing(
    const QList<qevercloud::SyncChunk> & syncChunks,
    const std::shared_ptr<IDurableResourcesProcessor::ICallback> & callback,
    DownloadResourcesStatus & downloadResourcesStatus)
{
    quint64 resourceIndex = 0UL;
    for (const auto & syncChunk: std::as_const(syncChunks)) {
        if (!syncChunk.resources()) {
            continue;
        }

        for (const auto & resource: std::as_const(*syncChunk.resources())) {
            if (resourceIndex % 4 == 0) {
                const auto resourceGuid = resource.guid().value();
                const auto resourceUsn = resource.updateSequenceNum().value();

                if (callback) {
                    callback->onProcessedResource(resourceGuid, resourceUsn);
                }

                downloadResourcesStatus
                    .m_processedResourceGuidsAndUsns[resourceGuid] =
                    resourceUsn;
            }
            else if (resourceIndex % 4 == 1) {
                auto exc =
                    std::make_shared<RuntimeError>(ErrorString{"some error"});

                if (callback) {
                    callback->onResourceFailedToDownload(resource, *exc);
                }

                downloadResourcesStatus.m_resourcesWhichFailedToDownload
                    << DownloadResourcesStatus::ResourceWithException{
                           resource, std::move(exc)};
            }
            else if (resourceIndex % 4 == 2) {
                auto exc =
                    std::make_shared<RuntimeError>(ErrorString{"some error"});

                if (callback) {
                    callback->onResourceFailedToProcess(resource, *exc);
                }

                downloadResourcesStatus.m_resourcesWhichFailedToProcess
                    << DownloadResourcesStatus::ResourceWithException{
                           resource, std::move(exc)};
            }
            else if (resourceIndex % 4 == 3) {
                if (callback) {
                    callback->onResourceProcessingCancelled(resource);
                }

                const auto resourceGuid = resource.guid().value();
                const auto resourceUsn = resource.updateSequenceNum().value();

                downloadResourcesStatus
                    .m_cancelledResourceGuidsAndUsns[resourceGuid] =
                    resourceUsn;
            }
            else {
                EXPECT_FALSE(true) << "Unreachable code";
            }

            ++resourceIndex;
        }
    }

    downloadResourcesStatus.m_totalNewResources = resourceIndex / 2;

    downloadResourcesStatus.m_totalUpdatedResources =
        resourceIndex - downloadResourcesStatus.m_totalNewResources;
}

} // namespace

class DownloaderTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const std::shared_ptr<mocks::MockISyncStateStorage> m_mockSyncStateStorage =
        std::make_shared<StrictMock<mocks::MockISyncStateStorage>>();

    const std::shared_ptr<mocks::MockISyncChunksProvider>
        m_mockSyncChunksProvider =
            std::make_shared<StrictMock<mocks::MockISyncChunksProvider>>();

    const std::shared_ptr<mocks::MockILinkedNotebooksProcessor>
        m_mockLinkedNotebooksProcessor = std::make_shared<
            StrictMock<mocks::MockILinkedNotebooksProcessor>>();

    const std::shared_ptr<mocks::MockINotebooksProcessor>
        m_mockNotebooksProcessor =
            std::make_shared<StrictMock<mocks::MockINotebooksProcessor>>();

    const std::shared_ptr<mocks::MockIDurableNotesProcessor>
        m_mockNotesProcessor =
            std::make_shared<StrictMock<mocks::MockIDurableNotesProcessor>>();

    const std::shared_ptr<mocks::MockIDurableResourcesProcessor>
        m_mockResourcesProcessor = std::make_shared<
            StrictMock<mocks::MockIDurableResourcesProcessor>>();

    const std::shared_ptr<mocks::MockISavedSearchesProcessor>
        m_mockSavedSearchesProcessor =
            std::make_shared<StrictMock<mocks::MockISavedSearchesProcessor>>();

    const std::shared_ptr<mocks::MockITagsProcessor> m_mockTagsProcessor =
        std::make_shared<StrictMock<mocks::MockITagsProcessor>>();

    const std::shared_ptr<mocks::MockIFullSyncStaleDataExpunger>
        m_mockFullSyncStaleDataExpunger = std::make_shared<
            StrictMock<mocks::MockIFullSyncStaleDataExpunger>>();

    const std::shared_ptr<mocks::qevercloud::MockINoteStore> m_mockNoteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    const std::shared_ptr<mocks::MockINoteStoreProvider>
        m_mockNoteStoreProvider =
            std::make_shared<StrictMock<mocks::MockINoteStoreProvider>>();

    const std::shared_ptr<mocks::MockILinkedNotebookTagsCleaner>
        m_mockLinkedNotebookTagsCleaner = std::make_shared<
            StrictMock<mocks::MockILinkedNotebookTagsCleaner>>();

    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const qevercloud::IRequestContextPtr m_ctx =
        qevercloud::newRequestContext();

    const qevercloud::IRetryPolicyPtr m_retryPolicy =
        qevercloud::newRetryPolicy();

    const utility::cancelers::ManualCancelerPtr m_manualCanceler =
        std::make_shared<utility::cancelers::ManualCanceler>();
};

TEST_F(DownloaderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner,
            m_mockLocalStorage, m_ctx, m_retryPolicy));
}

TEST_F(DownloaderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            Account{}, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner,
            m_mockLocalStorage, m_ctx, m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNonEvernoteAccount)
{
    Account account{QStringLiteral("Full Name"), Account::Type::Local};

    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            std::move(account), m_mockAuthenticationInfoProvider,
            m_mockSyncStateStorage, m_mockSyncChunksProvider,
            m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, nullptr, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner,
            m_mockLocalStorage, m_ctx, m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncStateStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, nullptr,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner,
            m_mockLocalStorage, m_ctx, m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSyncChunksProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            nullptr, m_mockLinkedNotebooksProcessor, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLinkedNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, nullptr, m_mockNotebooksProcessor,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotebooksProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor, nullptr,
            m_mockNotesProcessor, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNotesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, nullptr, m_mockResourcesProcessor,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullResourcesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor, nullptr,
            m_mockSavedSearchesProcessor, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullSavedSearchesProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, nullptr, m_mockTagsProcessor,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullTagsProcessor)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor, nullptr,
            m_mockFullSyncStaleDataExpunger, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullFullSyncStaleDataExpunger)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, nullptr, m_mockNoteStoreProvider,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullNoteStoreProvider)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger, nullptr,
            m_mockLinkedNotebookTagsCleaner, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLinkedNotebookTagsCleaner)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, nullptr, m_mockLocalStorage, m_ctx,
            m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullLocalStorage)
{
    EXPECT_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner, nullptr,
            m_ctx, m_retryPolicy),
        InvalidArgument);
}

TEST_F(DownloaderTest, CtorNullRequestContext)
{
    EXPECT_NO_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner,
            m_mockLocalStorage, nullptr, m_retryPolicy));
}

TEST_F(DownloaderTest, CtorNullRetryPolicy)
{
    EXPECT_NO_THROW(
        const auto downloader = std::make_shared<Downloader>(
            m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
            m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
            m_mockNotebooksProcessor, m_mockNotesProcessor,
            m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
            m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
            m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner,
            m_mockLocalStorage, m_ctx, nullptr));
}

enum class SyncMode
{
    FullFirst,
    FullNonFirst,
    Incremental
};

struct DownloaderSyncChunksTestData
{
    SyncChunksFlags m_syncChunksFlags;
    SyncMode m_syncMode;
};

constexpr std::array gSyncChunksTestData{
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks, SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithTags, SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithTags, SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithTags, SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches |
            SyncChunksFlag::WithTags,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches |
            SyncChunksFlag::WithTags,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithSavedSearches |
            SyncChunksFlag::WithTags,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithSavedSearches,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithSavedSearches,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithSavedSearches,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithTags |
            SyncChunksFlag::WithSavedSearches,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithTags |
            SyncChunksFlag::WithSavedSearches,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithTags |
            SyncChunksFlag::WithSavedSearches,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags,
        SyncMode::Incremental},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags |
            SyncChunksFlag::WithLinkedNotebooks,
        SyncMode::FullFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags |
            SyncChunksFlag::WithLinkedNotebooks,
        SyncMode::FullNonFirst},
    DownloaderSyncChunksTestData{
        SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
            SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
            SyncChunksFlag::WithSavedSearches | SyncChunksFlag::WithTags |
            SyncChunksFlag::WithLinkedNotebooks,
        SyncMode::Incremental},
};

class DownloaderSyncChunksTest :
    public DownloaderTest,
    public testing::WithParamInterface<DownloaderSyncChunksTestData>
{};

INSTANTIATE_TEST_SUITE_P(
    DownloaderSyncChunksTestInstance, DownloaderSyncChunksTest,
    testing::ValuesIn(gSyncChunksTestData));

TEST_P(DownloaderSyncChunksTest, Download)
{
    const auto downloader = std::make_shared<Downloader>(
        m_account, m_mockAuthenticationInfoProvider, m_mockSyncStateStorage,
        m_mockSyncChunksProvider, m_mockLinkedNotebooksProcessor,
        m_mockNotebooksProcessor, m_mockNotesProcessor,
        m_mockResourcesProcessor, m_mockSavedSearchesProcessor,
        m_mockTagsProcessor, m_mockFullSyncStaleDataExpunger,
        m_mockNoteStoreProvider, m_mockLinkedNotebookTagsCleaner,
        m_mockLocalStorage, m_ctx, m_retryPolicy);

    const auto now = QDateTime::currentMSecsSinceEpoch();

    const auto authenticationInfo = std::make_shared<AuthenticationInfo>();
    authenticationInfo->m_userId = m_account.id();
    authenticationInfo->m_authToken = QStringLiteral("authToken");
    authenticationInfo->m_authTokenExpirationTime = now + 100000000;
    authenticationInfo->m_authenticationTime = now;
    authenticationInfo->m_shardId = QStringLiteral("shardId");
    authenticationInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");
    authenticationInfo->m_webApiUrlPrefix = QStringLiteral("webApiUrlPrefix");

    const auto testData = GetParam();

    const qint32 userOwnAfterUsn = [&] {
        switch (testData.m_syncMode) {
        case SyncMode::FullFirst:
            return 0;
        case SyncMode::FullNonFirst:
            [[fallthrough]];
        case SyncMode::Incremental:
            return 42;
        }

        UNREACHABLE;
    }();

    const qint32 linkedNotebookAfterUsn = [&] {
        switch (testData.m_syncMode) {
        case SyncMode::FullFirst:
            return 0;
        case SyncMode::FullNonFirst:
            [[fallthrough]];
        case SyncMode::Incremental:
            return 15;
        }

        UNREACHABLE;
    }();

    const auto syncChunks =
        generateSyncChunks(testData.m_syncChunksFlags, userOwnAfterUsn);
    ASSERT_FALSE(syncChunks.isEmpty());

    const auto linkedNotebooks = collectLinkedNotebooks(syncChunks);

    const SyncChunksItemCounts syncChunksItemCounts{syncChunks};

    std::shared_ptr<mocks::MockIDownloaderICallback> mockDownloaderCallback =
        std::make_shared<StrictMock<mocks::MockIDownloaderICallback>>();

    EXPECT_CALL(*m_mockNoteStoreProvider, userOwnNoteStore)
        .WillRepeatedly(
            [noteStoreWeak = std::weak_ptr{
                 m_mockNoteStore}]() -> QFuture<qevercloud::INoteStorePtr> {
                return threading::makeReadyFuture<qevercloud::INoteStorePtr>(
                    noteStoreWeak.lock());
            });
    {
        InSequence s;

        EXPECT_CALL(*m_mockSyncStateStorage, getSyncState(m_account))
            .WillOnce([&]([[maybe_unused]] const Account & account) {
                auto syncState = std::make_shared<SyncState>();
                syncState->m_userDataUpdateCount = userOwnAfterUsn;
                if (testData.m_syncMode == SyncMode::FullNonFirst) {
                    syncState->m_userDataLastSyncTime = now;
                }
                for (const auto & linkedNotebook:
                     std::as_const(linkedNotebooks)) {
                    Q_ASSERT(linkedNotebook.guid());
                    const auto & guid = *linkedNotebook.guid();
                    syncState->m_linkedNotebookUpdateCounts[guid] =
                        linkedNotebookAfterUsn;

                    if (testData.m_syncMode == SyncMode::FullNonFirst) {
                        syncState->m_linkedNotebookLastSyncTimes[guid] = now;
                    }
                }
                return syncState;
            });

        EXPECT_CALL(
            *m_mockAuthenticationInfoProvider,
            authenticateAccount(
                m_account, IAuthenticationInfoProvider::Mode::Cache))
            .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
                authenticationInfo)));

        EXPECT_CALL(*m_mockNoteStore, getSyncStateAsync)
            .WillOnce([&](const qevercloud::IRequestContextPtr & ctx) {
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(),
                        authenticationInfo->authToken());
                }
                return threading::makeReadyFuture<qevercloud::SyncState>(
                    qevercloud::SyncStateBuilder{}
                        .setUpdateCount(
                            syncChunks.last().chunkHighUSN().value())
                        .setFullSyncBefore(
                            testData.m_syncMode == SyncMode::FullNonFirst
                                ? (now + 100500)
                                : 0)
                        .build());
            });

        EXPECT_CALL(*m_mockSyncChunksProvider, fetchSyncChunks)
            .WillOnce([&](const qint32 afterUsn, const qint32 updateCount,
                          [[maybe_unused]] const SynchronizationMode syncMode,
                          const qevercloud::IRequestContextPtr & ctx,
                          const utility::cancelers::ICancelerPtr & canceler,
                          const ISyncChunksProvider::ICallbackWeakPtr &
                              callbackWeak) {
                if (testData.m_syncMode == SyncMode::FullNonFirst) {
                    EXPECT_EQ(afterUsn, 0);
                    EXPECT_NE(afterUsn, userOwnAfterUsn);
                }
                else {
                    EXPECT_EQ(afterUsn, userOwnAfterUsn);
                }

                EXPECT_EQ(
                    updateCount, syncChunks.last().chunkHighUSN().value());

                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(),
                        authenticationInfo->authToken());
                }
                EXPECT_TRUE(canceler);

                EXPECT_FALSE(callbackWeak.expired());
                if (const auto callback = callbackWeak.lock()) {
                    const qint32 lastPreviousUsn = afterUsn;
                    const qint32 highestServerUsn =
                        syncChunks.last().chunkHighUSN().value();
                    for (const auto & syncChunk: std::as_const(syncChunks)) {
                        callback->onUserOwnSyncChunksDownloadProgress(
                            syncChunk.chunkHighUSN().value(), highestServerUsn,
                            lastPreviousUsn);
                    }
                }

                return threading::makeReadyFuture<QList<qevercloud::SyncChunk>>(
                    syncChunks);
            });

        const qint32 lastPreviousUsn =
            (testData.m_syncMode == SyncMode::FullNonFirst ? 0
                                                           : userOwnAfterUsn);

        const qint32 highestServerUsn =
            syncChunks.last().chunkHighUSN().value();
        for (const auto & syncChunk: std::as_const(syncChunks)) {
            EXPECT_CALL(
                *mockDownloaderCallback,
                onSyncChunksDownloadProgress(
                    syncChunk.chunkHighUSN().value(), highestServerUsn,
                    lastPreviousUsn))
                .Times(1);
        }
    }

    SyncChunksDataCountersPtr lastSyncChunksDataCounters;
    EXPECT_CALL(*mockDownloaderCallback, onSyncChunksDataProcessingProgress)
        .WillRepeatedly([&](const SyncChunksDataCountersPtr & counters) {
            ASSERT_TRUE(counters);
            if (!lastSyncChunksDataCounters) {
                lastSyncChunksDataCounters = counters;
                return;
            }

            checkSyncChunksDataCountersUpdate(
                *lastSyncChunksDataCounters, *counters);

            lastSyncChunksDataCounters = counters;
        });

    quint32 lastDownloadedNotes = 0U;
    quint32 lastTotalNotesToDownload = 0U;

    if (syncChunksItemCounts.totalNotes != 0) {
        EXPECT_CALL(*mockDownloaderCallback, onNotesDownloadProgress)
            .Times(AtMost(syncChunksItemCounts.totalNotes))
            .WillRepeatedly([&](const quint32 notesDownloaded,
                                const quint32 totalNotesToDownload) {
                EXPECT_GT(notesDownloaded, lastDownloadedNotes);

                EXPECT_TRUE(
                    lastTotalNotesToDownload == 0U ||
                    lastTotalNotesToDownload == totalNotesToDownload);

                lastDownloadedNotes = notesDownloaded;
                lastTotalNotesToDownload = totalNotesToDownload;
            });
    }

    quint32 lastDownloadedResources = 0U;
    quint32 lastTotalResourcesToDownload = 0U;

    if (syncChunksItemCounts.totalResources != 0) {
        EXPECT_CALL(*mockDownloaderCallback, onResourcesDownloadProgress)
            .Times(AtMost(syncChunksItemCounts.totalResources))
            .WillRepeatedly([&](const quint32 resourcesDownloaded,
                                const quint32 totalResourcesToDownload) {
                EXPECT_GT(resourcesDownloaded, lastDownloadedResources);

                EXPECT_TRUE(
                    lastTotalResourcesToDownload == 0U ||
                    lastTotalResourcesToDownload == totalResourcesToDownload);

                lastDownloadedResources = resourcesDownloaded;
                lastTotalResourcesToDownload = totalResourcesToDownload;
            });
    }

    EXPECT_CALL(*mockDownloaderCallback, onSyncChunksDownloaded);

    {
        InSequence s;

        if (testData.m_syncMode == SyncMode::FullNonFirst) {
            EXPECT_CALL(
                *m_mockFullSyncStaleDataExpunger,
                expungeStaleData(_, _, Eq(std::nullopt)))
                .WillOnce(
                    [&](const IFullSyncStaleDataExpunger::PreservedGuids &
                            preservedGuids,
                        [[maybe_unused]] const utility::cancelers::
                            ICancelerPtr & canceler,
                        [[maybe_unused]] const std::optional<qevercloud::Guid> &
                            linkedNotebookGuid) {
                        const auto expectedPreservedGuids =
                            collectPreservedGuids(syncChunks);
                        EXPECT_EQ(preservedGuids, expectedPreservedGuids);
                        return threading::makeReadyFuture();
                    });
        }

        EXPECT_CALL(*m_mockNotebooksProcessor, processNotebooks(syncChunks, _))
            .WillOnce([&]([[maybe_unused]] const QList<qevercloud::SyncChunk> &
                              chunks,
                          const INotebooksProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);
                if (callback) {
                    const qint32 totalNotebooks =
                        syncChunksItemCounts.totalNotebooks;

                    const qint32 totalExpungedNotebooks =
                        syncChunksItemCounts.totalExpungedNotebooks;

                    const qint32 addedNotebooks = totalNotebooks / 2;
                    const qint32 updatedNotebooks =
                        totalNotebooks - addedNotebooks;

                    callback->onNotebooksProcessingProgress(
                        totalNotebooks, totalExpungedNotebooks, addedNotebooks,
                        updatedNotebooks, totalExpungedNotebooks);
                }

                return threading::makeReadyFuture();
            });

        EXPECT_CALL(*m_mockTagsProcessor, processTags(syncChunks, _))
            .WillOnce(
                [&]([[maybe_unused]] const QList<qevercloud::SyncChunk> &
                        chunks,
                    const ITagsProcessor::ICallbackWeakPtr & callbackWeak) {
                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);
                    if (callback) {
                        const qint32 totalTags = syncChunksItemCounts.totalTags;

                        const qint32 totalExpungedTags =
                            syncChunksItemCounts.totalExpungedTags;

                        const qint32 addedTags = totalTags / 2;
                        const qint32 updatedTags = totalTags - addedTags;

                        callback->onTagsProcessingProgress(
                            totalTags, totalExpungedTags, addedTags,
                            updatedTags, totalExpungedTags);
                    }

                    return threading::makeReadyFuture();
                });

        EXPECT_CALL(
            *m_mockSavedSearchesProcessor, processSavedSearches(syncChunks, _))
            .WillOnce([&]([[maybe_unused]] const QList<qevercloud::SyncChunk> &
                              chunks,
                          const ISavedSearchesProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);
                if (callback) {
                    const qint32 totalSavedSearches =
                        syncChunksItemCounts.totalSavedSearches;

                    const qint32 totalExpungedSavedSearches =
                        syncChunksItemCounts.totalExpungedSavedSearches;

                    const qint32 addedSavedSearches = totalSavedSearches / 2;
                    const qint32 updatedSavedSearches =
                        totalSavedSearches - addedSavedSearches;

                    callback->onSavedSearchesProcessingProgress(
                        totalSavedSearches, totalExpungedSavedSearches,
                        addedSavedSearches, updatedSavedSearches,
                        totalExpungedSavedSearches);
                }

                return threading::makeReadyFuture();
            });

        EXPECT_CALL(
            *m_mockLinkedNotebooksProcessor,
            processLinkedNotebooks(syncChunks, _))
            .WillOnce([&]([[maybe_unused]] const QList<qevercloud::SyncChunk> &
                              chunks,
                          const ILinkedNotebooksProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);
                if (callback) {
                    const qint32 totalLinkedNotebooks =
                        syncChunksItemCounts.totalLinkedNotebooks;

                    const qint32 totalExpungedLinkedNotebooks =
                        syncChunksItemCounts.totalExpungedLinkedNotebooks;

                    callback->onLinkedNotebooksProcessingProgress(
                        totalLinkedNotebooks, totalExpungedLinkedNotebooks,
                        totalLinkedNotebooks, totalExpungedLinkedNotebooks);
                }

                return threading::makeReadyFuture();
            });

        EXPECT_CALL(*m_mockNotesProcessor, processNotes(syncChunks, _, _, _))
            .WillOnce(
                [&](const QList<qevercloud::SyncChunk> & chunks,
                    const utility::cancelers::ICancelerPtr & canceler,
                    [[maybe_unused]] const std::optional<qevercloud::Guid> &
                        linkedNotebookGuid,
                    const IDurableNotesProcessor::ICallbackWeakPtr &
                        callbackWeak) {
                    EXPECT_TRUE(canceler);

                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);

                    auto downloadNotesStatus =
                        std::make_shared<DownloadNotesStatus>();

                    emulateSyncChunksNotesProcessing(
                        chunks, callback, *downloadNotesStatus);

                    return threading::makeReadyFuture<DownloadNotesStatusPtr>(
                        std::move(downloadNotesStatus));
                });

        EXPECT_CALL(
            *m_mockResourcesProcessor, processResources(syncChunks, _, _, _))
            .WillOnce([&](const QList<qevercloud::SyncChunk> & chunks,
                          const utility::cancelers::ICancelerPtr & canceler,
                          [[maybe_unused]] const std::optional<
                              qevercloud::Guid> & linkedNotebookGuid,
                          const IDurableResourcesProcessor::ICallbackWeakPtr &
                              callbackWeak) {
                EXPECT_TRUE(canceler);

                const auto callback = callbackWeak.lock();
                EXPECT_TRUE(callback);

                auto downloadResourcesStatus =
                    std::make_shared<DownloadResourcesStatus>();

                emulateSyncChunksResourcesProcessing(
                    chunks, callback, *downloadResourcesStatus);

                return threading::makeReadyFuture<DownloadResourcesStatusPtr>(
                    std::move(downloadResourcesStatus));
            });
    }

    EXPECT_CALL(*m_mockLocalStorage, listLinkedNotebooks)
        .WillOnce(Return(
            threading::makeReadyFuture<QList<qevercloud::LinkedNotebook>>(
                linkedNotebooks)));

    if (!linkedNotebooks.isEmpty()) {
        EXPECT_CALL(
            *mockDownloaderCallback,
            onStartLinkedNotebooksDataDownloading(linkedNotebooks));

        EXPECT_CALL(
            *m_mockLinkedNotebookTagsCleaner, clearStaleLinkedNotebookTags)
            .WillOnce(Return(threading::makeReadyFuture()));
    }

    struct LinkedNotebookData
    {
        explicit LinkedNotebookData(
            const QList<qevercloud::SyncChunk> & syncChunks) :
            syncChunksItemCounts{syncChunks}
        {}

        SyncChunksItemCounts syncChunksItemCounts;

        quint32 lastDownloadedNotes = 0UL;
        quint32 lastTotalNotesToDownload = 0UL;

        quint32 lastDownloadedResources = 0UL;
        quint32 lastTotalResourcesToDownload = 0UL;
    };

    QHash<qevercloud::Guid, LinkedNotebookData> linkedNotebooksData;
    linkedNotebooksData.reserve(linkedNotebooks.size());

    for (const auto & linkedNotebook: std::as_const(linkedNotebooks)) {
        const auto linkedNotebookSyncChunks = generateSyncChunks(
            SyncChunksFlags{} | SyncChunksFlag::WithNotebooks |
                SyncChunksFlag::WithNotes | SyncChunksFlag::WithResources |
                SyncChunksFlag::WithTags,
            linkedNotebookAfterUsn);
        ASSERT_FALSE(linkedNotebookSyncChunks.isEmpty());

        const qint32 chunksHighUsn =
            linkedNotebookSyncChunks.constLast().chunkHighUSN().value();

        EXPECT_CALL(
            *m_mockNoteStore,
            getLinkedNotebookSyncStateAsync(linkedNotebook, _))
            .WillOnce([&, chunksHighUsn](
                          const qevercloud::LinkedNotebook & linkedNotebook,
                          const qevercloud::IRequestContextPtr & ctx) {
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(),
                        authenticationInfo->authToken());
                }

                EXPECT_TRUE(linkedNotebooks.contains(linkedNotebook));

                return threading::makeReadyFuture<qevercloud::SyncState>(
                    qevercloud::SyncStateBuilder{}
                        .setUpdateCount(chunksHighUsn)
                        .setFullSyncBefore(
                            testData.m_syncMode == SyncMode::FullNonFirst
                                ? (now + 100500)
                                : 0)
                        .build());
            });

        const auto linkedNotebookAuthenticationInfo =
            std::make_shared<AuthenticationInfo>();
        linkedNotebookAuthenticationInfo->m_authToken =
            QString::fromUtf8("%1 authToken")
                .arg(linkedNotebook.username().value());

        EXPECT_CALL(
            *m_mockAuthenticationInfoProvider,
            authenticateToLinkedNotebook(
                m_account, linkedNotebook,
                IAuthenticationInfoProvider::Mode::Cache))
            .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
                linkedNotebookAuthenticationInfo)));

        EXPECT_CALL(
            *m_mockSyncChunksProvider,
            fetchLinkedNotebookSyncChunks(
                linkedNotebook, _, _, _, _, _, _))
            .WillOnce([&, linkedNotebookSyncChunks](
                          const qevercloud::LinkedNotebook & ln,
                          [[maybe_unused]] qint32 afterUsn,
                          [[maybe_unused]] qint32 updateCount,
                          [[maybe_unused]] const SynchronizationMode syncMode,
                          const qevercloud::IRequestContextPtr & ctx,
                          const utility::cancelers::ICancelerPtr & canceler,
                          const ISyncChunksProvider::ICallbackWeakPtr &
                              callbackWeak) {
                EXPECT_EQ(ln, linkedNotebook);

                if (testData.m_syncMode == SyncMode::FullNonFirst) {
                    EXPECT_EQ(afterUsn, 0);
                    EXPECT_NE(afterUsn, linkedNotebookAfterUsn);
                }
                else {
                    EXPECT_EQ(afterUsn, linkedNotebookAfterUsn);
                }

                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(),
                        authenticationInfo->authToken());
                }
                EXPECT_TRUE(canceler);

                EXPECT_FALSE(callbackWeak.expired());
                if (const auto callback = callbackWeak.lock()) {
                    const qint32 lastPreviousUsn = linkedNotebookAfterUsn;
                    const qint32 highestServerUsn =
                        linkedNotebookSyncChunks.last().chunkHighUSN().value();
                    for (const auto & syncChunk:
                         std::as_const(linkedNotebookSyncChunks)) {
                        callback->onLinkedNotebookSyncChunksDownloadProgress(
                            syncChunk.chunkHighUSN().value(), highestServerUsn,
                            lastPreviousUsn, ln);
                    }
                }

                return threading::makeReadyFuture<QList<qevercloud::SyncChunk>>(
                    linkedNotebookSyncChunks);
            });

        const qint32 lastPreviousUsn = linkedNotebookAfterUsn;
        const qint32 highestServerUsn =
            linkedNotebookSyncChunks.last().chunkHighUSN().value();
        for (const auto & syncChunk: std::as_const(linkedNotebookSyncChunks)) {
            EXPECT_CALL(
                *mockDownloaderCallback,
                onLinkedNotebookSyncChunksDownloadProgress(
                    syncChunk.chunkHighUSN().value(), highestServerUsn,
                    lastPreviousUsn, linkedNotebook))
                .Times(1);
        }

        EXPECT_CALL(
            *mockDownloaderCallback,
            onLinkedNotebookSyncChunksDownloaded(
                linkedNotebook, linkedNotebookSyncChunks));

        SyncChunksDataCountersPtr lastSyncChunksDataCounters;
        EXPECT_CALL(
            *mockDownloaderCallback,
            onLinkedNotebookSyncChunksDataProcessingProgress(
                Ne(nullptr), linkedNotebook))
            .WillRepeatedly(
                [lastSyncChunksDataCounters = lastSyncChunksDataCounters](
                    const SyncChunksDataCountersPtr & counters,
                    [[maybe_unused]] const qevercloud::LinkedNotebook & ln) mutable {
                    ASSERT_TRUE(counters);
                    if (!lastSyncChunksDataCounters) {
                        lastSyncChunksDataCounters = counters;
                        return;
                    }

                    checkSyncChunksDataCountersUpdate(
                        *lastSyncChunksDataCounters, *counters);

                    lastSyncChunksDataCounters = counters;
                });

        const auto & linkedNotebookGuid = linkedNotebook.guid().value();

        const auto it = linkedNotebooksData.insert(
            linkedNotebookGuid, LinkedNotebookData{linkedNotebookSyncChunks});
        ASSERT_NE(it, linkedNotebooksData.end());

        auto & linkedNotebookData = it.value();
        const auto & linkedNotebookSyncChunksItemCounts =
            linkedNotebookData.syncChunksItemCounts;

        if (linkedNotebookSyncChunksItemCounts.totalNotes != 0) {
            EXPECT_CALL(
                *mockDownloaderCallback,
                onLinkedNotebookNotesDownloadProgress(_, _, linkedNotebook))
                .Times(AtMost(linkedNotebookSyncChunksItemCounts.totalNotes))
                .WillRepeatedly(
                    [&](const quint32 notesDownloaded, // NOLINT
                        const quint32 totalNotesToDownload,
                        [[maybe_unused]] const qevercloud::LinkedNotebook &
                            ln) {
                        EXPECT_GT(
                            notesDownloaded,
                            linkedNotebookData.lastDownloadedNotes);

                        EXPECT_TRUE(
                            linkedNotebookData.lastTotalNotesToDownload == 0 ||
                            linkedNotebookData.lastTotalNotesToDownload ==
                                totalNotesToDownload);

                        linkedNotebookData.lastDownloadedNotes =
                            notesDownloaded;

                        linkedNotebookData.lastTotalNotesToDownload =
                            totalNotesToDownload;
                    });
        }

        if (linkedNotebookSyncChunksItemCounts.totalResources != 0) {
            EXPECT_CALL(
                *mockDownloaderCallback,
                onLinkedNotebookResourcesDownloadProgress(_, _, linkedNotebook))
                .Times(
                    AtMost(linkedNotebookSyncChunksItemCounts.totalResources))
                .WillRepeatedly(
                    [&](const quint32 resourcesDownloaded, // NOLINT
                        const quint32 totalResourcesToDownload,
                        [[maybe_unused]] const qevercloud::LinkedNotebook &
                            ln) {
                        EXPECT_GT(
                            resourcesDownloaded,
                            linkedNotebookData.lastDownloadedResources);

                        EXPECT_TRUE(
                            linkedNotebookData.lastTotalResourcesToDownload ==
                                0 ||
                            linkedNotebookData.lastTotalResourcesToDownload ==
                                totalResourcesToDownload);

                        linkedNotebookData.lastDownloadedResources =
                            resourcesDownloaded;

                        linkedNotebookData.lastTotalResourcesToDownload =
                            totalResourcesToDownload;
                    });
        }

        {
            InSequence s;

            if (testData.m_syncMode == SyncMode::FullNonFirst) {
                EXPECT_CALL(
                    *m_mockFullSyncStaleDataExpunger,
                    expungeStaleData(_, _, Eq(linkedNotebookGuid)))
                    .WillOnce(
                        [&, linkedNotebookSyncChunks](const IFullSyncStaleDataExpunger::PreservedGuids &
                                preservedGuids,
                            [[maybe_unused]] const utility::cancelers::
                                ICancelerPtr & canceler,
                            [[maybe_unused]] const std::optional<qevercloud::Guid> &
                                linkedNotebookGuid) {
                            const auto expectedPreservedGuids =
                                collectPreservedGuids(linkedNotebookSyncChunks);
                            EXPECT_EQ(preservedGuids, expectedPreservedGuids);
                            return threading::makeReadyFuture();
                        });
            }

            EXPECT_CALL(
                *m_mockNotebooksProcessor,
                processNotebooks(linkedNotebookSyncChunks, _))
                .WillOnce([&]([[maybe_unused]] const QList<
                                  qevercloud::SyncChunk> & chunks,
                              const INotebooksProcessor::ICallbackWeakPtr &
                                  callbackWeak) {
                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);
                    if (callback) {
                        const qint32 totalNotebooks =
                            linkedNotebookSyncChunksItemCounts.totalNotebooks;

                        const qint32 totalExpungedNotebooks =
                            linkedNotebookSyncChunksItemCounts
                                .totalExpungedNotebooks;

                        const qint32 addedNotebooks = totalNotebooks / 2;
                        const qint32 updatedNotebooks =
                            totalNotebooks - addedNotebooks;

                        callback->onNotebooksProcessingProgress(
                            totalNotebooks, totalExpungedNotebooks,
                            addedNotebooks, updatedNotebooks,
                            totalExpungedNotebooks);
                    }

                    return threading::makeReadyFuture();
                });

            EXPECT_CALL(
                *m_mockTagsProcessor, processTags(linkedNotebookSyncChunks, _))
                .WillOnce(
                    [&]([[maybe_unused]] const QList<qevercloud::SyncChunk> &
                            chunks,
                        const ITagsProcessor::ICallbackWeakPtr & callbackWeak) {
                        const auto callback = callbackWeak.lock();
                        EXPECT_TRUE(callback);
                        if (callback) {
                            const qint32 totalTags =
                                linkedNotebookSyncChunksItemCounts.totalTags;

                            const qint32 totalExpungedTags =
                                linkedNotebookSyncChunksItemCounts
                                    .totalExpungedTags;

                            const qint32 addedTags = totalTags / 2;
                            const qint32 updatedTags = totalTags - addedTags;

                            callback->onTagsProcessingProgress(
                                totalTags, totalExpungedTags, addedTags,
                                updatedTags, totalExpungedTags);
                        }

                        return threading::makeReadyFuture();
                    });

            EXPECT_CALL(
                *m_mockNotesProcessor,
                processNotes(linkedNotebookSyncChunks, _, _, _))
                .WillOnce([&](const QList<qevercloud::SyncChunk> & chunks,
                              const utility::cancelers::ICancelerPtr & canceler,
                              [[maybe_unused]] const std::optional<
                                  qevercloud::Guid> & linkedNotebookGuid,
                              const IDurableNotesProcessor::ICallbackWeakPtr &
                                  callbackWeak) {
                    EXPECT_TRUE(canceler);

                    const auto callback = callbackWeak.lock();
                    EXPECT_TRUE(callback);

                    auto downloadNotesStatus =
                        std::make_shared<DownloadNotesStatus>();

                    emulateSyncChunksNotesProcessing(
                        chunks, callback, *downloadNotesStatus);

                    return threading::makeReadyFuture<DownloadNotesStatusPtr>(
                        std::move(downloadNotesStatus));
                });

            EXPECT_CALL(
                *m_mockResourcesProcessor,
                processResources(linkedNotebookSyncChunks, _, _, _))
                .WillOnce(
                    [&](const QList<qevercloud::SyncChunk> & chunks,
                        const utility::cancelers::ICancelerPtr & canceler,
                        [[maybe_unused]] const std::optional<qevercloud::Guid> &
                            linkedNotebookGuid,
                        const IDurableResourcesProcessor::ICallbackWeakPtr &
                            callbackWeak) {
                        EXPECT_TRUE(canceler);

                        const auto callback = callbackWeak.lock();
                        EXPECT_TRUE(callback);

                        auto downloadResourcesStatus =
                            std::make_shared<DownloadResourcesStatus>();

                        emulateSyncChunksResourcesProcessing(
                            chunks, callback, *downloadResourcesStatus);

                        return threading::makeReadyFuture<
                            DownloadResourcesStatusPtr>(
                            std::move(downloadResourcesStatus));
                    });
        }
    }

    auto result =
        downloader->download(m_manualCanceler, mockDownloaderCallback);

    waitForFuture(result);

    // Checking the state of the last reported sync chunks data counters
    ASSERT_TRUE(lastSyncChunksDataCounters);

    checkSyncChunksDataCounters(
        syncChunksItemCounts, *lastSyncChunksDataCounters);

    // Checking the returned status
    ASSERT_EQ(result.resultCount(), 1);
    const auto status = result.result();

    // Check user own data status
    ASSERT_TRUE(status.userOwnResult.syncChunksDataCounters);
    checkSyncChunksDataCounters(
        syncChunksItemCounts, *status.userOwnResult.syncChunksDataCounters);

    // Notes
    EXPECT_EQ(lastTotalNotesToDownload, syncChunksItemCounts.totalNotes);

    ASSERT_TRUE(status.userOwnResult.downloadNotesStatus);
    checkDownloadNotesStatus(
        syncChunksItemCounts, *status.userOwnResult.downloadNotesStatus);

    // Resources
    EXPECT_EQ(
        lastTotalResourcesToDownload, syncChunksItemCounts.totalResources);

    ASSERT_TRUE(status.userOwnResult.downloadResourcesStatus);
    checkDownloadResourcesStatus(
        syncChunksItemCounts, *status.userOwnResult.downloadResourcesStatus);

    // Check data from linked notebooks status
    EXPECT_EQ(status.linkedNotebookResults.size(), linkedNotebooks.size());

    for (const auto & linkedNotebook: std::as_const(linkedNotebooks)) {
        const auto & linkedNotebookGuid = linkedNotebook.guid().value();

        const auto it =
            status.linkedNotebookResults.constFind(linkedNotebookGuid);
        ASSERT_NE(it, status.linkedNotebookResults.constEnd());

        const auto & result = it.value();
        ASSERT_TRUE(result.syncChunksDataCounters);

        const auto cit = linkedNotebooksData.constFind(linkedNotebookGuid);
        ASSERT_NE(cit, linkedNotebooksData.constEnd());

        const auto & linkedNotebookData = cit.value();
        checkSyncChunksDataCounters(
            linkedNotebookData.syncChunksItemCounts,
            *result.syncChunksDataCounters);

        // Notes
        EXPECT_EQ(
            linkedNotebookData.lastTotalNotesToDownload,
            linkedNotebookData.syncChunksItemCounts.totalNotes);

        ASSERT_TRUE(result.downloadNotesStatus);
        checkDownloadNotesStatus(
            linkedNotebookData.syncChunksItemCounts,
            *result.downloadNotesStatus);

        // Resources
        EXPECT_EQ(
            linkedNotebookData.lastTotalResourcesToDownload,
            linkedNotebookData.syncChunksItemCounts.totalResources);

        ASSERT_TRUE(result.downloadResourcesStatus);
        checkDownloadResourcesStatus(
            linkedNotebookData.syncChunksItemCounts,
            *result.downloadResourcesStatus);
    }
}

} // namespace quentier::synchronization::tests
