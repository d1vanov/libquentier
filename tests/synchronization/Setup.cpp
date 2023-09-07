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

#include "Setup.h"
#include "NoteStoreServer.h"

#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/synchronization/types/ISyncStateBuilder.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/DataBuilder.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/SavedSearchBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QGlobalStatic>
#include <QtTest>

#include <algorithm>
#include <optional>
#include <type_traits>

namespace quentier::synchronization::tests {

namespace {

Q_GLOBAL_STATIC_WITH_ARGS(QString, gBaseItems, (QString::fromUtf8("base")));

Q_GLOBAL_STATIC_WITH_ARGS(
    QString, gModifiedItems, (QString::fromUtf8("modified")));

Q_GLOBAL_STATIC_WITH_ARGS(QString, gNewItems, (QString::fromUtf8("new")));

[[nodiscard]] QString composeName(
    const int index, const QString & typeName, const QString & nameSuffix)
{
    auto name = QString::fromUtf8("%1 #%2").arg(typeName, index);
    if (!nameSuffix.isEmpty()) {
        name += QString::fromUtf8(" (%1)").arg(nameSuffix);
    }
    return name;
}

[[nodiscard]] qevercloud::SavedSearch generateSavedSearch(
    const int index, const QString & nameSuffix = {})
{
    return qevercloud::SavedSearchBuilder{}
        .setGuid(UidGenerator::Generate())
        .setLocalOnly(false)
        .setLocallyModified(false)
        .setLocallyFavorited(false)
        .setName(composeName(index, QStringLiteral("Saved search"), nameSuffix))
        .setFormat(qevercloud::QueryFormat::SEXP)
        .setQuery(QString::fromUtf8("Saved search query %1").arg(index))
        .build();
}

[[nodiscard]] qevercloud::Tag generateTag(
    const int index, const QString & nameSuffix = {},
    std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt)
{
    return qevercloud::TagBuilder{}
        .setGuid(UidGenerator::Generate())
        .setLinkedNotebookGuid(std::move(linkedNotebookGuid))
        .setLocalOnly(false)
        .setLocallyModified(false)
        .setLocallyFavorited(false)
        .setName(composeName(index, QStringLiteral("Tag"), nameSuffix))
        .build();
}

[[nodiscard]] qevercloud::Notebook generateNotebook(
    const int index, const QString & nameSuffix = {},
    std::optional<qevercloud::Guid> linkedNotebookGuid = std::nullopt)
{
    return qevercloud::NotebookBuilder{}
        .setGuid(UidGenerator::Generate())
        .setLinkedNotebookGuid(std::move(linkedNotebookGuid))
        .setLocalOnly(false)
        .setLocallyModified(false)
        .setLocallyFavorited(false)
        .setName(composeName(index, QStringLiteral("Notebook"), nameSuffix))
        .build();
}

[[nodiscard]] qevercloud::Note generateNote(
    const int index, qevercloud::Guid notebookGuid,
    const QString & nameSuffix = {}, QList<qevercloud::Resource> resources = {},
    QList<qevercloud::Guid> tagGuids = {})
{
    auto note =
        qevercloud::NoteBuilder{}
            .setGuid(UidGenerator::Generate())
            .setNotebookGuid(std::move(notebookGuid))
            .setLocalOnly(false)
            .setLocallyModified(false)
            .setLocallyFavorited(false)
            .setActive(true)
            .setTitle(composeName(index, QStringLiteral("Note"), nameSuffix))
            .build();

    if (!resources.isEmpty()) {
        for (auto & resource: resources) {
            resource.setNoteGuid(note.guid());
        }
        note.setResources(std::move(resources));
    }

    if (!tagGuids.isEmpty()) {
        note.setTagGuids(std::move(tagGuids));
    }

    return note;
}

[[nodiscard]] qevercloud::Resource generateResource(
    const int index, const QString & nameSuffix = {})
{
    QByteArray resourceData =
        composeName(index, QStringLiteral("Resource"), nameSuffix).toUtf8();

    auto resourceHash =
        QCryptographicHash::hash(resourceData, QCryptographicHash::Md5);

    const auto resourceSize = resourceData.size();

    return qevercloud::ResourceBuilder{}
        .setGuid(UidGenerator::Generate())
        .setActive(true)
        .setHeight(32)
        .setWidth(24)
        .setLocalOnly(false)
        .setLocallyModified(false)
        .setLocallyFavorited(false)
        .setMime(QStringLiteral("application/octet-stream"))
        .setData(qevercloud::DataBuilder{}
                     .setBody(std::move(resourceData))
                     .setBodyHash(std::move(resourceHash))
                     .setSize(resourceSize)
                     .build())
        .build();
}

[[nodiscard]] qevercloud::LinkedNotebook generateLinkedNotebook(const int index)
{
    return qevercloud::LinkedNotebookBuilder{}
        .setGuid(UidGenerator::Generate())
        .setLocalOnly(false)
        .setLocallyModified(false)
        .setLocallyFavorited(false)
        .setNoteStoreUrl(QStringLiteral("Fake note store url"))
        .setShardId(QStringLiteral("Fake shard id"))
        .setWebApiUrlPrefix(QStringLiteral("Fake web api url prefix"))
        .setUsername(QString::fromUtf8("Username #%1").arg(index))
        .setShareName(QStringLiteral("Share name #%1").arg(index))
        .setUri(QString::fromUtf8("Uri #%1").arg(index))
        .build();
}

} // namespace

void setupTestData(
    const DataItemTypes dataItemTypes, const ItemGroups itemGroups,
    const ItemSources itemSources, const DataItemTypes expungedDataItemTypes,
    const ItemSources expungedItemSources, TestData & testData)
{
    constexpr int itemCount = 10;

    if (dataItemTypes.testFlag(DataItemType::SavedSearch) &&
        itemSources.testFlag(ItemSource::UserOwnAccount))
    {
        int savedSearchIndex = 1;

        const auto putSavedSearches =
            [&](const QString & nameSuffix,
                QList<qevercloud::SavedSearch> & savedSearches) {
                for (int i = 0; i < itemCount; ++i) {
                    auto savedSearch =
                        generateSavedSearch(savedSearchIndex++, nameSuffix);
                    savedSearches << savedSearch;
                }
            };

        if (itemGroups.testFlag(ItemGroup::Base)) {
            putSavedSearches(*gBaseItems, testData.m_baseSavedSearches);
        }

        if (itemGroups.testFlag(ItemGroup::Modified)) {
            putSavedSearches(*gModifiedItems, testData.m_modifiedSavedSearches);
        }

        if (itemGroups.testFlag(ItemGroup::New)) {
            putSavedSearches(*gNewItems, testData.m_newSavedSearches);
        }
    }

    QList<qevercloud::Guid> linkedNotebookGuids;
    if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
        int linkedNotebookIndex = 1;

        const auto putLinkedNotebooks =
            [&](QList<qevercloud::LinkedNotebook> & linkedNotebooks) {
                for (int i = 0; i < itemCount; ++i) {
                    auto linkedNotebook =
                        generateLinkedNotebook(linkedNotebookIndex++);
                    linkedNotebooks << linkedNotebook;
                    linkedNotebookGuids << *linkedNotebook.guid();
                }
            };

        if (itemGroups.testFlag(ItemGroup::Base)) {
            putLinkedNotebooks(testData.m_baseLinkedNotebooks);
        }

        if (itemGroups.testFlag(ItemGroup::Modified)) {
            putLinkedNotebooks(testData.m_modifiedLinkedNotebooks);
        }

        if (itemGroups.testFlag(ItemGroup::New)) {
            putLinkedNotebooks(testData.m_newLinkedNotebooks);
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Tag)) {
        int tagIndex = 1;

        const auto putTags =
            [&](const QString & nameSuffix, QList<qevercloud::Tag> & tags,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid =
                    std::nullopt) {
                for (int i = 0; i < itemCount; ++i) {
                    auto tag =
                        generateTag(tagIndex++, nameSuffix, linkedNotebookGuid);
                    tags << tag;

                    if (i % 2 == 0) {
                        auto childTag = generateTag(
                            tagIndex++, nameSuffix, linkedNotebookGuid);
                        childTag.setParentGuid(tag.guid());
                        childTag.setParentTagLocalId(tag.localId());
                        tags << childTag;
                    }
                }
            };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putTags(*gBaseItems, testData.m_userOwnBaseTags);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putTags(*gModifiedItems, testData.m_userOwnModifiedTags);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putTags(*gNewItems, testData.m_userOwnNewTags);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids))
            {
                if (itemGroups.testFlag(ItemGroup::Base)) {
                    putTags(
                        *gBaseItems, testData.m_linkedNotebookBaseTags,
                        linkedNotebookGuid);
                }

                if (itemGroups.testFlag(ItemGroup::Modified)) {
                    putTags(
                        *gModifiedItems, testData.m_linkedNotebookModifiedTags,
                        linkedNotebookGuid);
                }

                if (itemGroups.testFlag(ItemGroup::New)) {
                    putTags(
                        *gNewItems, testData.m_linkedNotebookNewTags,
                        linkedNotebookGuid);
                }
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Notebook) ||
        dataItemTypes.testFlag(DataItemType::Note) ||
        dataItemTypes.testFlag(DataItemType::Resource))
    {
        int notebookIndex = 1;

        const auto putNotebooks =
            [&](const QString & nameSuffix,
                QList<qevercloud::Notebook> & notebooks,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid =
                    std::nullopt) {
                for (int i = 0; i < itemCount; ++i) {
                    auto notebook = generateNotebook(
                        notebookIndex++, nameSuffix, linkedNotebookGuid);
                    notebooks << notebook;
                }
            };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putNotebooks(*gBaseItems, testData.m_userOwnBaseNotebooks);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putNotebooks(
                    *gModifiedItems, testData.m_userOwnModifiedNotebooks);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putNotebooks(*gNewItems, testData.m_userOwnNewNotebooks);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids))
            {
                if (itemGroups.testFlag(ItemGroup::Base)) {
                    putNotebooks(
                        *gNewItems, testData.m_linkedNotebookBaseNotebooks,
                        linkedNotebookGuid);
                }

                if (itemGroups.testFlag(ItemGroup::Modified)) {
                    putNotebooks(
                        *gModifiedItems,
                        testData.m_linkedNotebookModifiedNotebooks,
                        linkedNotebookGuid);
                }

                if (itemGroups.testFlag(ItemGroup::New)) {
                    putNotebooks(
                        *gNewItems, testData.m_linkedNotebookNewNotebooks,
                        linkedNotebookGuid);
                }
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Note) ||
        dataItemTypes.testFlag(DataItemType::Resource))
    {
        int noteIndex = 1;

        const QList<QList<qevercloud::Guid>> tagGuidsLists = [&] {
            QList<QList<qevercloud::Guid>> result;

            const auto tagsByGuid = [&] {
                QHash<qevercloud::Guid, qevercloud::Tag> result;

                const auto allTags = QList<qevercloud::Tag>{}
                    << testData.m_userOwnBaseTags
                    << testData.m_userOwnModifiedTags
                    << testData.m_userOwnNewTags
                    << testData.m_linkedNotebookBaseTags
                    << testData.m_linkedNotebookModifiedTags
                    << testData.m_linkedNotebookNewTags;

                for (const auto & tag: qAsConst(allTags)) {
                    result[*tag.guid()] = tag;
                }

                return result;
            }();

            const auto tagCount = tagsByGuid.size();

            const int tagBatchCount = 3;
            const auto tagCountPerBatch = tagCount / tagBatchCount;
            auto it = tagsByGuid.constBegin();
            for (int i = 0; i < tagBatchCount; ++i) {
                QList<qevercloud::Guid> tagGuids;
                tagGuids.reserve(tagCountPerBatch);
                for (int j = 0; j < tagCountPerBatch; ++j) {
                    tagGuids << it.key();

                    ++it;
                    if (it == tagsByGuid.constEnd()) {
                        it = tagsByGuid.constBegin();
                    }
                }

                result << tagGuids;
            }

            return result;
        }();

        Q_ASSERT(!tagGuidsLists.isEmpty());

        int tagGuidsListIndex = 0;
        const auto putNotes =
            [&](const QString & nameSuffix, QList<qevercloud::Note> & notes,
                const QList<qevercloud::Guid> & notebookGuids) {
                auto notebookIt = notebookGuids.constBegin();
                for (int i = 0; i < itemCount; ++i) {
                    QList<qevercloud::Resource> resources;
                    if (i % 2 == 0) {
                        constexpr int resourceCountPerNote = 3;
                        resources.reserve(resourceCountPerNote);
                        for (int j = 0; j < resourceCountPerNote; ++j) {
                            auto resource = generateResource(j, nameSuffix);
                            resources << resource;
                        }
                    }

                    QList<qevercloud::Guid> tagGuids;
                    if (i % 3 == 0) {
                        tagGuids = tagGuidsLists[tagGuidsListIndex++];
                        if (tagGuidsListIndex >= tagGuidsLists.size()) {
                            tagGuidsListIndex = 0;
                        }
                    }

                    auto note = generateNote(
                        noteIndex++, *notebookIt, nameSuffix,
                        std::move(resources), std::move(tagGuids));
                    notes << note;
                }
            };

        const QList<qevercloud::Guid> userOwnNotebookGuids = [&] {
            QList<qevercloud::Guid> result;
            const auto allNotebooks = QList<qevercloud::Notebook>{}
                << testData.m_userOwnBaseNotebooks
                << testData.m_userOwnModifiedNotebooks
                << testData.m_userOwnNewNotebooks;
            result.reserve(allNotebooks.size());
            for (const auto & notebook: qAsConst(allNotebooks)) {
                result << *notebook.guid();
            }
            return result;
        }();

        const QList<qevercloud::Guid> linkedNotebookNotebookGuids = [&] {
            QList<qevercloud::Guid> result;
            const auto allNotebooks = QList<qevercloud::Notebook>{}
                << testData.m_linkedNotebookBaseNotebooks
                << testData.m_linkedNotebookModifiedNotebooks
                << testData.m_linkedNotebookNewNotebooks;
            result.reserve(allNotebooks.size());
            for (const auto & notebook: qAsConst(allNotebooks)) {
                result << *notebook.guid();
            }
            return result;
        }();

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putNotes(
                    *gBaseItems, testData.m_userOwnBaseNotes,
                    userOwnNotebookGuids);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putNotes(
                    *gModifiedItems, testData.m_userOwnModifiedNotes,
                    userOwnNotebookGuids);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putNotes(
                    *gNewItems, testData.m_userOwnNewNotes,
                    userOwnNotebookGuids);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putNotes(
                    *gBaseItems, testData.m_linkedNotebookBaseNotes,
                    linkedNotebookNotebookGuids);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putNotes(
                    *gModifiedItems, testData.m_linkedNotebookModifiedNotes,
                    linkedNotebookNotebookGuids);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putNotes(
                    *gNewItems, testData.m_linkedNotebookNewNotes,
                    linkedNotebookNotebookGuids);
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Resource)) {
        int resourceIndex = 1;

        int noteGuidsListIndex = 0;
        const auto putResources =
            [&](const QString & nameSuffix,
                QList<qevercloud::Resource> & resources,
                const QList<qevercloud::Guid> & noteGuids) {
                for (int i = 0; i < itemCount; ++i) {
                    auto noteGuid = noteGuids[noteGuidsListIndex++];
                    if (noteGuidsListIndex >= noteGuids.size()) {
                        noteGuidsListIndex = 0;
                    }

                    auto resource =
                        generateResource(resourceIndex++, nameSuffix);
                    resource.setNoteGuid(std::move(noteGuid));
                    resources << resource;
                }
            };

        const QList<qevercloud::Guid> userOwnNoteGuids = [&] {
            QList<qevercloud::Guid> result;
            const auto allNotes = QList<qevercloud::Note>{}
                << testData.m_userOwnBaseNotes
                << testData.m_userOwnModifiedNotes
                << testData.m_userOwnNewNotes;
            result.reserve(allNotes.size());
            for (const auto & note: qAsConst(allNotes)) {
                result << *note.guid();
            }
            return result;
        }();

        if (itemSources.testFlag(ItemSource::UserOwnAccount) &&
            itemGroups.testFlag(ItemGroup::Modified))
        {
            putResources(
                *gBaseItems, testData.m_userOwnModifiedResources,
                userOwnNoteGuids);
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook) &&
            itemGroups.testFlag(ItemGroup::Modified))
        {
            for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids))
            {
                const QList<qevercloud::Guid> noteGuids = [&] {
                    QList<qevercloud::Guid> result;

                    const auto allNotes = QList<qevercloud::Note>{}
                        << testData.m_linkedNotebookBaseNotes
                        << testData.m_linkedNotebookModifiedNotes
                        << testData.m_linkedNotebookNewNotes;

                    const auto allNotebooks = QList<qevercloud::Notebook>{}
                        << testData.m_linkedNotebookBaseNotebooks
                        << testData.m_linkedNotebookModifiedNotebooks
                        << testData.m_linkedNotebookNewNotebooks;

                    for (const auto & note: qAsConst(allNotes)) {
                        Q_ASSERT(note.guid());
                        Q_ASSERT(note.notebookGuid());

                        const auto notebookIt = std::find_if(
                            allNotebooks.constBegin(), allNotebooks.constEnd(),
                            [&note](const qevercloud::Notebook & notebook) {
                                return note.notebookGuid() == notebook.guid();
                            });
                        Q_ASSERT(notebookIt != allNotebooks.constEnd());
                        if (notebookIt->linkedNotebookGuid() ==
                            linkedNotebookGuid) {
                            result << *note.guid();
                        }
                    }

                    return result;
                }();

                putResources(
                    *gModifiedItems, testData.m_linkedNotebookModifiedResources,
                    noteGuids);
            }
        }
    }

    const auto generateExpungedGuids =
        [](QSet<qevercloud::Guid> & expungedGuids) {
            expungedGuids.reserve(itemCount);
            for (int i = 0; i < itemCount; ++i) {
                expungedGuids.insert(UidGenerator::Generate());
            }
        };

    if (expungedDataItemTypes.testFlag(DataItemType::SavedSearch) &&
        expungedItemSources.testFlag(ItemSource::UserOwnAccount))
    {
        generateExpungedGuids(testData.m_expungedUserOwnSavedSearchGuids);
    }

    if (expungedDataItemTypes.testFlag(DataItemType::Tag)) {
        if (expungedItemSources.testFlag(ItemSource::UserOwnAccount)) {
            generateExpungedGuids(testData.m_expungedUserOwnTagGuids);
        }

        if (expungedItemSources.testFlag(ItemSource::LinkedNotebook)) {
            testData.m_expungedLinkedNotebookTagGuids.reserve(
                linkedNotebookGuids.size());

            for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids))
            {
                auto & expungedTagGuids =
                    testData
                        .m_expungedLinkedNotebookTagGuids[linkedNotebookGuid];

                generateExpungedGuids(expungedTagGuids);
            }
        }
    }

    if (expungedDataItemTypes.testFlag(DataItemType::Notebook)) {
        if (expungedItemSources.testFlag(ItemSource::UserOwnAccount)) {
            generateExpungedGuids(testData.m_expungedUserOwnNotebookGuids);
        }

        if (expungedItemSources.testFlag(ItemSource::LinkedNotebook)) {
            testData.m_expungedLinkedNotebookNotebookGuids.reserve(
                linkedNotebookGuids.size());

            for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids))
            {
                auto & expungedNotebookGuids =
                    testData.m_expungedLinkedNotebookNotebookGuids
                        [linkedNotebookGuid];

                generateExpungedGuids(expungedNotebookGuids);
            }
        }
    }

    if (expungedDataItemTypes.testFlag(DataItemType::Note)) {
        if (expungedItemSources.testFlag(ItemSource::UserOwnAccount)) {
            generateExpungedGuids(testData.m_expungedUserOwnNoteGuids);
        }

        if (expungedItemSources.testFlag(ItemSource::LinkedNotebook)) {
            testData.m_expungedLinkedNotebookNoteGuids.reserve(
                linkedNotebookGuids.size());

            for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids))
            {
                auto & expungedNoteGuids =
                    testData
                        .m_expungedLinkedNotebookNoteGuids[linkedNotebookGuid];

                generateExpungedGuids(expungedNoteGuids);
            }
        }
    }
}

void setupNoteStoreServer(
    TestData & testData, NoteStoreServer & noteStoreServer)
{
    const auto putSavedSearches =
        [&](QList<qevercloud::SavedSearch> & savedSearches) {
            for (auto & savedSearch: savedSearches) {
                auto itemData = noteStoreServer.putSavedSearch(savedSearch);
                savedSearch.setUpdateSequenceNum(itemData.usn);

                if (itemData.name) {
                    savedSearch.setName(*itemData.name);
                }

                if (itemData.guid) {
                    savedSearch.setGuid(*itemData.guid);
                }
            }
        };

    putSavedSearches(testData.m_baseSavedSearches);
    putSavedSearches(testData.m_modifiedSavedSearches);
    putSavedSearches(testData.m_newSavedSearches);

    const auto putLinkedNotebooks =
        [&](QList<qevercloud::LinkedNotebook> & linkedNotebooks) {
            for (auto & linkedNotebook: linkedNotebooks) {
                auto itemData =
                    noteStoreServer.putLinkedNotebook(linkedNotebook);
                linkedNotebook.setUpdateSequenceNum(itemData.usn);

                if (itemData.guid) {
                    linkedNotebook.setGuid(*itemData.guid);
                }
            }
        };

    putLinkedNotebooks(testData.m_baseLinkedNotebooks);
    putLinkedNotebooks(testData.m_modifiedLinkedNotebooks);
    putLinkedNotebooks(testData.m_newLinkedNotebooks);

    const auto putNotebooks = [&](QList<qevercloud::Notebook> & notebooks) {
        for (auto & notebook: notebooks) {
            auto itemData = noteStoreServer.putNotebook(notebook);
            notebook.setUpdateSequenceNum(itemData.usn);

            if (itemData.name) {
                notebook.setName(*itemData.name);
            }

            if (itemData.guid) {
                notebook.setGuid(*itemData.guid);
            }
        }
    };

    putNotebooks(testData.m_userOwnBaseNotebooks);
    putNotebooks(testData.m_userOwnModifiedNotebooks);
    putNotebooks(testData.m_userOwnNewNotebooks);
    putNotebooks(testData.m_linkedNotebookBaseNotebooks);
    putNotebooks(testData.m_linkedNotebookModifiedNotebooks);
    putNotebooks(testData.m_linkedNotebookNewNotebooks);

    const auto putTags = [&](QList<qevercloud::Tag> & tags) {
        for (auto & tag: tags) {
            auto itemData = noteStoreServer.putTag(tag);
            tag.setUpdateSequenceNum(itemData.usn);

            if (itemData.name) {
                tag.setName(*itemData.name);
            }

            if (itemData.guid) {
                tag.setGuid(*itemData.guid);
            }
        }
    };

    putTags(testData.m_userOwnBaseTags);
    putTags(testData.m_userOwnModifiedTags);
    putTags(testData.m_userOwnNewTags);
    putTags(testData.m_linkedNotebookBaseTags);
    putTags(testData.m_linkedNotebookModifiedTags);
    putTags(testData.m_linkedNotebookNewTags);

    const auto putNotes = [&](QList<qevercloud::Note> & notes) {
        for (auto & note: notes) {
            auto itemData = noteStoreServer.putNote(note);
            note.setUpdateSequenceNum(itemData.usn);

            if (itemData.guid) {
                note.setGuid(*itemData.guid);
            }
        }
    };

    putNotes(testData.m_userOwnBaseNotes);
    putNotes(testData.m_userOwnModifiedNotes);
    putNotes(testData.m_userOwnNewNotes);
    putNotes(testData.m_linkedNotebookBaseNotes);
    putNotes(testData.m_linkedNotebookModifiedNotes);
    putNotes(testData.m_linkedNotebookNewNotes);

    const auto putResources = [&](QList<qevercloud::Resource> & resources) {
        for (auto & resource: resources) {
            auto itemData = noteStoreServer.putResource(resource);
            resource.setUpdateSequenceNum(itemData.usn);

            if (itemData.guid) {
                resource.setGuid(*itemData.guid);
            }
        }
    };

    putResources(testData.m_userOwnModifiedResources);
    putResources(testData.m_linkedNotebookModifiedResources);

    for (const auto & guid:
         qAsConst(testData.m_expungedUserOwnSavedSearchGuids)) {
        noteStoreServer.putExpungedSavedSearchGuid(guid);
    }

    for (const auto & guid: qAsConst(testData.m_expungedUserOwnTagGuids)) {
        noteStoreServer.putExpungedUserOwnTagGuid(guid);
    }

    for (const auto & guid: qAsConst(testData.m_expungedUserOwnNotebookGuids)) {
        noteStoreServer.putExpungedUserOwnNotebookGuid(guid);
    }

    for (const auto & guid: qAsConst(testData.m_expungedUserOwnNoteGuids)) {
        noteStoreServer.putExpungedUserOwnNoteGuid(guid);
    }

    for (const auto it: qevercloud::toRange(
             qAsConst(testData.m_expungedLinkedNotebookTagGuids)))
    {
        const auto & linkedNotebookGuid = it.key();
        for (const auto & tagGuid: qAsConst(it.value())) {
            noteStoreServer.putExpungedLinkedNotebookTagGuid(
                linkedNotebookGuid, tagGuid);
        }
    }

    for (const auto it: qevercloud::toRange(
             qAsConst(testData.m_expungedLinkedNotebookNotebookGuids)))
    {
        const auto & linkedNotebookGuid = it.key();
        for (const auto & notebookGuid: qAsConst(it.value())) {
            noteStoreServer.putExpungedLinkedNotebookNotebookGuid(
                linkedNotebookGuid, notebookGuid);
        }
    }

    for (const auto it: qevercloud::toRange(
             qAsConst(testData.m_expungedLinkedNotebookNoteGuids)))
    {
        const auto & linkedNotebookGuid = it.key();
        for (const auto & noteGuid: qAsConst(it.value())) {
            noteStoreServer.putExpungedLinkedNotebookNoteGuid(
                linkedNotebookGuid, noteGuid);
        }
    }
}

void setupLocalStorage(
    const TestData & testData, const DataItemTypes dataItemTypes,
    const ItemGroups itemGroups, const ItemSources itemSources,
    local_storage::ILocalStorage & localStorage)
{
    if (dataItemTypes.testFlag(DataItemType::SavedSearch) &&
        itemSources.testFlag(ItemSource::UserOwnAccount))
    {
        const auto putSavedSearches = [&](const QList<qevercloud::SavedSearch> &
                                              savedSearches,
                                          const ItemGroup itemGroup) {
            for (const auto & savedSearch: qAsConst(savedSearches)) {
                switch (itemGroup) {
                case ItemGroup::Base:
                    localStorage.putSavedSearch(savedSearch).waitForFinished();
                    break;
                case ItemGroup::Modified:
                {
                    auto search = savedSearch;
                    search.setLocallyModified(true);
                    localStorage.putSavedSearch(std::move(search))
                        .waitForFinished();
                } break;
                case ItemGroup::New:
                {
                    auto search = savedSearch;
                    search.setGuid(std::nullopt);
                    search.setUpdateSequenceNum(std::nullopt);
                    search.setLocallyModified(true);
                    localStorage.putSavedSearch(std::move(search))
                        .waitForFinished();
                } break;
                }
            }
        };

        if (itemGroups.testFlag(ItemGroup::Base)) {
            putSavedSearches(testData.m_baseSavedSearches, ItemGroup::Base);
        }

        if (itemGroups.testFlag(ItemGroup::Modified)) {
            putSavedSearches(
                testData.m_modifiedSavedSearches, ItemGroup::Modified);
        }

        if (itemGroups.testFlag(ItemGroup::New)) {
            putSavedSearches(testData.m_newSavedSearches, ItemGroup::New);
        }
    }

    if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
        const auto putLinkedNotebooks =
            [&](const QList<qevercloud::LinkedNotebook> & linkedNotebooks,
                const ItemGroup itemGroup) {
                for (const auto & linkedNotebook: qAsConst(linkedNotebooks)) {
                    switch (itemGroup) {
                    case ItemGroup::Base:
                        localStorage.putLinkedNotebook(linkedNotebook)
                            .waitForFinished();
                        break;
                    case ItemGroup::Modified:
                    {
                        auto n = linkedNotebook;
                        n.setLocallyModified(true);
                        localStorage.putLinkedNotebook(std::move(n))
                            .waitForFinished();
                    } break;
                    case ItemGroup::New:
                      // It makes no sense to put new linked notebooks to local
                      // storage, they can only be created on the server
                      break;
                    }
                }
            };

        if (itemGroups.testFlag(ItemGroup::Base)) {
            putLinkedNotebooks(testData.m_baseLinkedNotebooks, ItemGroup::Base);
        }

        if (itemGroups.testFlag(ItemGroup::Modified)) {
            putLinkedNotebooks(
                testData.m_modifiedLinkedNotebooks, ItemGroup::Modified);
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Tag)) {
        const auto putTags = [&](const QList<qevercloud::Tag> & tags,
                                 const ItemGroup itemGroup) {
            for (const auto & tag: qAsConst(tags)) {
                switch (itemGroup) {
                case ItemGroup::Base:
                    localStorage.putTag(tag).waitForFinished();
                    break;
                case ItemGroup::Modified:
                {
                    auto t = tag;
                    t.setLocallyModified(true);
                    localStorage.putTag(std::move(t)).waitForFinished();
                } break;
                case ItemGroup::New:
                {
                    auto t = tag;
                    t.setGuid(std::nullopt);
                    t.setParentGuid(std::nullopt);
                    t.setUpdateSequenceNum(std::nullopt);
                    t.setLocallyModified(true);
                    localStorage.putTag(std::move(t)).waitForFinished();
                } break;
                }
            }
        };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putTags(testData.m_userOwnBaseTags, ItemGroup::Base);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putTags(testData.m_userOwnModifiedTags, ItemGroup::Modified);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putTags(testData.m_userOwnNewTags, ItemGroup::New);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putTags(testData.m_linkedNotebookBaseTags, ItemGroup::Base);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putTags(
                    testData.m_linkedNotebookModifiedTags, ItemGroup::Modified);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putTags(testData.m_linkedNotebookNewTags, ItemGroup::New);
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Notebook)) {
        const auto putNotebooks = [&](const QList<qevercloud::Notebook> &
                                          notebooks,
                                      const ItemGroup itemGroup) {
            for (const auto & notebook: qAsConst(notebooks)) {
                switch (itemGroup) {
                case ItemGroup::Base:
                    localStorage.putNotebook(notebook).waitForFinished();
                    break;
                case ItemGroup::Modified:
                {
                    auto n = notebook;
                    n.setLocallyModified(true);
                    localStorage.putNotebook(std::move(n)).waitForFinished();
                } break;
                case ItemGroup::New:
                {
                    auto n = notebook;
                    n.setGuid(std::nullopt);
                    n.setUpdateSequenceNum(std::nullopt);
                    n.setLocallyModified(true);
                    localStorage.putNotebook(std::move(n)).waitForFinished();
                } break;
                }
            }
        };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putNotebooks(testData.m_userOwnBaseNotebooks, ItemGroup::Base);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putNotebooks(
                    testData.m_userOwnModifiedNotebooks, ItemGroup::Modified);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putNotebooks(testData.m_userOwnNewNotebooks, ItemGroup::New);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putNotebooks(
                    testData.m_linkedNotebookBaseNotebooks, ItemGroup::Base);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putNotebooks(
                    testData.m_linkedNotebookModifiedNotebooks,
                    ItemGroup::Modified);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putNotebooks(
                    testData.m_linkedNotebookNewNotebooks, ItemGroup::New);
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Note)) {
        const auto putNotes = [&](const QList<qevercloud::Note> & notes,
                                  const ItemGroup itemGroup) {
            for (const auto & note: qAsConst(notes)) {
                switch (itemGroup) {
                case ItemGroup::Base:
                    localStorage.putNote(note).waitForFinished();
                    break;
                case ItemGroup::Modified:
                {
                    auto n = note;
                    n.setLocallyModified(true);
                    localStorage.putNote(std::move(n)).waitForFinished();
                } break;
                case ItemGroup::New:
                {
                    auto n = note;
                    n.setGuid(std::nullopt);
                    n.setUpdateSequenceNum(std::nullopt);
                    n.setLocallyModified(true);
                    localStorage.putNote(std::move(n)).waitForFinished();
                } break;
                }
            }
        };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putNotes(testData.m_userOwnBaseNotes, ItemGroup::Base);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putNotes(testData.m_userOwnModifiedNotes, ItemGroup::Modified);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putNotes(testData.m_userOwnNewNotes, ItemGroup::New);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                putNotes(testData.m_linkedNotebookBaseNotes, ItemGroup::Base);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                putNotes(
                    testData.m_linkedNotebookModifiedNotes,
                    ItemGroup::Modified);
            }

            if (itemGroups.testFlag(ItemGroup::New)) {
                putNotes(testData.m_linkedNotebookNewNotes, ItemGroup::New);
            }
        }
    }

    int expungedSavedSearchIndex = 1;
    for (const auto & guid:
         qAsConst(testData.m_expungedUserOwnSavedSearchGuids)) {
        localStorage
            .putSavedSearch(
                qevercloud::SavedSearchBuilder{}
                    .setGuid(guid)
                    .setName(QString::fromUtf8("Expunged saved search #")
                                 .arg(expungedSavedSearchIndex++))
                    .setUpdateSequenceNum(42)
                    .build())
            .waitForFinished();
    }

    int expungedNotebookIndex = 1;
    for (const auto & guid: qAsConst(testData.m_expungedUserOwnNotebookGuids)) {
        localStorage
            .putNotebook(qevercloud::NotebookBuilder{}
                             .setGuid(guid)
                             .setName(QString::fromUtf8("Expunged notebook #")
                                          .arg(expungedNotebookIndex++))
                             .setUpdateSequenceNum(42)
                             .build())
            .waitForFinished();
    }

    for (const auto it: qevercloud::toRange(
             qAsConst(testData.m_expungedLinkedNotebookNotebookGuids)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & guids = it.value();
        for (const auto & guid: qAsConst(guids)) {
            localStorage
                .putNotebook(
                    qevercloud::NotebookBuilder{}
                        .setGuid(guid)
                        .setLinkedNotebookGuid(linkedNotebookGuid)
                        .setName(QString::fromUtf8(
                                     "Expunged linked notebook's notebook #")
                                     .arg(expungedNotebookIndex++))
                        .setUpdateSequenceNum(42)
                        .build())
                .waitForFinished();
        }
    }

    int expungedTagIndex = 1;
    for (const auto & guid: qAsConst(testData.m_expungedUserOwnTagGuids)) {
        localStorage
            .putTag(qevercloud::TagBuilder{}
                        .setGuid(guid)
                        .setName(QString::fromUtf8("Expunged tag #")
                                     .arg(expungedTagIndex++))
                        .setUpdateSequenceNum(42)
                        .build())
            .waitForFinished();
    }

    for (const auto it: qevercloud::toRange(
             qAsConst(testData.m_expungedLinkedNotebookTagGuids)))
    {
        const auto & linkedNotebookGuid = it.key();
        const auto & guids = it.value();
        for (const auto & guid: qAsConst(guids)) {
            localStorage
                .putTag(qevercloud::TagBuilder{}
                            .setGuid(guid)
                            .setLinkedNotebookGuid(linkedNotebookGuid)
                            .setName(QString::fromUtf8(
                                         "Expunged linked notebook's tag #")
                                         .arg(expungedTagIndex++))
                            .setUpdateSequenceNum(42)
                            .build())
                .waitForFinished();
        }
    }

    int expungedNoteIndex = 1;
    if (!testData.m_expungedUserOwnNoteGuids.isEmpty()) {
        const auto listNotebooksOptions = [] {
            local_storage::ILocalStorage::ListNotebooksOptions options;
            options.m_affiliation =
                local_storage::ILocalStorage::Affiliation::User;
            return options;
        }();

        auto notebooksFuture = localStorage.listNotebooks(listNotebooksOptions);
        notebooksFuture.waitForFinished();
        QVERIFY(notebooksFuture.resultCount() == 1);

        const auto notebooks = notebooksFuture.result();
        QVERIFY(!notebooks.isEmpty());

        const auto & notebook = notebooks[0];

        for (const auto & guid: qAsConst(testData.m_expungedUserOwnNoteGuids)) {
            localStorage
                .putNote(qevercloud::NoteBuilder{}
                             .setGuid(guid)
                             .setTitle(QString::fromUtf8("Expunged note #")
                                           .arg(expungedNoteIndex++))
                             .setUpdateSequenceNum(42)
                             .setNotebookGuid(notebook.guid())
                             .setNotebookLocalId(notebook.localId())
                             .build())
                .waitForFinished();
        }
    }

    if (!testData.m_expungedLinkedNotebookNoteGuids.isEmpty()) {
        const auto listNotebooksOptions = [] {
            local_storage::ILocalStorage::ListNotebooksOptions options;
            options.m_affiliation =
                local_storage::ILocalStorage::Affiliation::AnyLinkedNotebook;
            return options;
        }();

        auto notebooksFuture = localStorage.listNotebooks(listNotebooksOptions);
        notebooksFuture.waitForFinished();
        QVERIFY(notebooksFuture.resultCount() == 1);

        const auto notebooks = notebooksFuture.result();

        for (const auto it: qevercloud::toRange(
                 qAsConst(testData.m_expungedLinkedNotebookTagGuids)))
        {
            const auto & linkedNotebookGuid = it.key();
            const auto & guids = it.value();
            for (const auto & guid: qAsConst(guids)) {
                const auto notebookIt = std::find_if(
                    notebooks.constBegin(), notebooks.constEnd(),
                    [&](const qevercloud::Notebook & notebook) {
                        return notebook.linkedNotebookGuid() ==
                            linkedNotebookGuid;
                    });
                QVERIFY(notebookIt != notebooks.constEnd());
                const auto & notebook = *notebookIt;

                localStorage
                    .putNote(
                        qevercloud::NoteBuilder{}
                            .setGuid(guid)
                            .setTitle(QString::fromUtf8(
                                          "Expunged linked notebook's note #")
                                          .arg(expungedNoteIndex++))
                            .setUpdateSequenceNum(42)
                            .setNotebookGuid(notebook.guid())
                            .setNotebookLocalId(notebook.localId())
                            .build())
                    .waitForFinished();
            }
        }
    }
}

void setupSyncState(
    const TestData & testData, const Account & testAccount,
    const DataItemTypes dataItemTypes, const ItemGroups itemGroups,
    const ItemSources itemSources, ISyncStateStorage & syncStateStorage,
    std::optional<qint32> lastUpdateTimestamp)
{
    qint32 userOwnUpdateCount = 0;
    QHash<qevercloud::Guid, qint32> linkedNotebookUpdateCounts;

    if (!lastUpdateTimestamp) {
        lastUpdateTimestamp = QDateTime::currentMSecsSinceEpoch();
    }

    const auto processItems = [&](const auto & items) {
        for (const auto & item: qAsConst(items)) {
            Q_ASSERT(item.updateSequenceNum());

            std::optional<qevercloud::Guid> linkedNotebookGuid;
            if constexpr (std::is_same_v<
                              std::decay_t<decltype(item)>,
                              qevercloud::Resource>)
            {
                Q_ASSERT(item.noteGuid());

                const QList<const QList<qevercloud::Note> *> noteLists{
                    &testData.m_userOwnBaseNotes,
                    &testData.m_userOwnModifiedNotes,
                    &testData.m_userOwnNewNotes};

                const QList<const QList<qevercloud::Notebook> *> notebookLists{
                    &testData.m_userOwnBaseNotebooks,
                    &testData.m_userOwnModifiedNotebooks,
                    &testData.m_userOwnNewNotebooks};

                for (const auto & noteList: qAsConst(noteLists)) {
                    const auto noteIt = std::find_if(
                        noteList->constBegin(), noteList->constEnd(),
                        [&](const qevercloud::Note & note) {
                            return note.guid() == item.noteGuid();
                        });
                    if (noteIt == noteList->constEnd()) {
                        continue;
                    }

                    for (const auto & notebookList: qAsConst(notebookLists)) {
                        const auto it = std::find_if(
                            notebookList->constBegin(),
                            notebookList->constEnd(),
                            [&](const qevercloud::Notebook & notebook) {
                                return notebook.guid() ==
                                    noteIt->notebookGuid();
                            });
                        if (it == notebookList->constEnd()) {
                            continue;
                        }

                        linkedNotebookGuid = it->linkedNotebookGuid();
                        break;
                    }

                    break;
                }
            }
            else if constexpr (std::is_same_v<
                              std::decay_t<decltype(item)>,
                              qevercloud::Note>)
            {
                Q_ASSERT(item.notebookGuid());

                const QList<const QList<qevercloud::Notebook> *> lists{
                    &testData.m_userOwnBaseNotebooks,
                    &testData.m_userOwnModifiedNotebooks,
                    &testData.m_userOwnNewNotebooks};

                for (const auto & list: qAsConst(lists)) {
                    const auto it = std::find_if(
                        list->constBegin(),
                        list->constEnd(),
                        [&](const qevercloud::Notebook & notebook) {
                            return notebook.guid() == item.notebookGuid();
                        });
                    if (it == list->constEnd()) {
                        continue;
                    }

                    linkedNotebookGuid = it->linkedNotebookGuid();
                    break;
                }
            }
            else if constexpr (
                !std::is_same_v<
                    std::decay_t<decltype(item)>, qevercloud::SavedSearch> &&
                !std::is_same_v<
                    std::decay_t<decltype(item)>, qevercloud::LinkedNotebook>)
            {
                linkedNotebookGuid = item.linkedNotebookGuid();
            }

            if (linkedNotebookGuid) {
                auto it = linkedNotebookUpdateCounts.find(*linkedNotebookGuid);
                if (it == linkedNotebookUpdateCounts.end()) {
                    linkedNotebookUpdateCounts[*linkedNotebookGuid] =
                        *item.updateSequenceNum();
                }
                else if (it.value() < *item.updateSequenceNum()) {
                    it.value() = *item.updateSequenceNum();
                }
            }
            else if (userOwnUpdateCount < *item.updateSequenceNum()) {
                userOwnUpdateCount = *item.updateSequenceNum();
            }
        }
    };

    if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
        if (dataItemTypes.testFlag(DataItemType::SavedSearch)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                processItems(testData.m_baseSavedSearches);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                processItems(testData.m_modifiedSavedSearches);
            }
        }

        if (dataItemTypes.testFlag(DataItemType::Notebook) ||
            dataItemTypes.testFlag(DataItemType::Note) ||
            (dataItemTypes.testFlag(DataItemType::Resource) &&
             itemGroups.testFlag(ItemGroup::Modified)))
        {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                processItems(testData.m_userOwnBaseNotebooks);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                processItems(testData.m_userOwnModifiedNotebooks);
            }
        }

        if (dataItemTypes.testFlag(DataItemType::Tag)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                processItems(testData.m_userOwnBaseTags);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                processItems(testData.m_userOwnModifiedTags);
            }
        }

        if (dataItemTypes.testFlag(DataItemType::Note) ||
            (dataItemTypes.testFlag(DataItemType::Resource) &&
             itemGroups.testFlag(ItemGroup::Modified))) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                processItems(testData.m_userOwnBaseNotes);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                processItems(testData.m_userOwnModifiedNotes);
            }
        }

        if (dataItemTypes.testFlag(DataItemType::Resource) &&
            itemGroups.testFlag(ItemGroup::Modified))
        {
            processItems(testData.m_userOwnModifiedResources);
        }
    }

    if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
        if (itemGroups.testFlag(ItemGroup::Base)) {
            processItems(testData.m_baseLinkedNotebooks);
        }

        if (itemGroups.testFlag(ItemGroup::Modified)) {
            processItems(testData.m_modifiedLinkedNotebooks);
        }

        if (dataItemTypes.testFlag(DataItemType::Notebook) ||
            dataItemTypes.testFlag(DataItemType::Note) ||
            (dataItemTypes.testFlag(DataItemType::Resource) &&
             itemGroups.testFlag(ItemGroup::Modified)))
        {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                processItems(testData.m_linkedNotebookBaseNotebooks);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                processItems(testData.m_linkedNotebookModifiedNotebooks);
            }
        }

        if (dataItemTypes.testFlag(DataItemType::Tag)) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                processItems(testData.m_linkedNotebookBaseTags);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                processItems(testData.m_linkedNotebookModifiedTags);
            }
        }

        if (dataItemTypes.testFlag(DataItemType::Note) ||
            (dataItemTypes.testFlag(DataItemType::Resource) &&
             itemGroups.testFlag(ItemGroup::Modified))) {
            if (itemGroups.testFlag(ItemGroup::Base)) {
                processItems(testData.m_linkedNotebookBaseNotes);
            }

            if (itemGroups.testFlag(ItemGroup::Modified)) {
                processItems(testData.m_linkedNotebookModifiedNotes);
            }
        }

        if (dataItemTypes.testFlag(DataItemType::Resource) &&
            itemGroups.testFlag(ItemGroup::Modified))
        {
            processItems(testData.m_linkedNotebookModifiedResources);
        }
    }

    QHash<qevercloud::Guid, qevercloud::Timestamp> linkedNotebookLastSyncTimes;
    linkedNotebookLastSyncTimes.reserve(linkedNotebookUpdateCounts.size());
    for (const auto it:
         qevercloud::toRange(qAsConst(linkedNotebookUpdateCounts)))
    {
        linkedNotebookLastSyncTimes[it.key()] = *lastUpdateTimestamp;
    }

    auto syncStateBuilder = createSyncStateBuilder();
    auto syncState = syncStateBuilder
        ->setUserDataUpdateCount(userOwnUpdateCount)
        .setUserDataLastSyncTime(*lastUpdateTimestamp)
        .setLinkedNotebookUpdateCounts(std::move(linkedNotebookUpdateCounts))
        .setLinkedNotebookLastSyncTimes(std::move(linkedNotebookLastSyncTimes))
        .build();

    syncStateStorage.setSyncState(testAccount, std::move(syncState));
}

} // namespace quentier::synchronization::tests