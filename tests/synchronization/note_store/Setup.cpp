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
#include <QGlobalStatic>

#include <algorithm>

namespace quentier::synchronization::tests::note_store {

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
    const DataItemTypes dataItemTypes, const GeneratorOptions generatorOptions,
    const ItemSources itemSources, TestData & testData)
{
    constexpr int itemCount = 10;

    qint32 userOwnUsn = 1;
    QHash<qevercloud::Guid, qint32> linkedNotebookUsns;

    if (dataItemTypes.testFlag(DataItemType::SavedSearch) &&
        itemSources.testFlag(ItemSource::UserOwnAccount))
    {
        int savedSearchIndex = 1;

        const auto putSavedSearches =
            [&](const QString & nameSuffix, qint32 & updateSequenceNum,
                QList<qevercloud::SavedSearch> & savedSearches) {
                for (int i = 0; i < itemCount; ++i) {
                    auto savedSearch =
                        generateSavedSearch(savedSearchIndex++, nameSuffix);
                    savedSearch.setUpdateSequenceNum(updateSequenceNum++);
                    savedSearches << savedSearch;
                }
            };

        if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
            putSavedSearches(
                *gBaseItems, userOwnUsn, testData.m_baseSavedSearches);
        }

        if (generatorOptions.testFlag(GeneratorOption::IncludeModifiedItems)) {
            putSavedSearches(
                *gModifiedItems, userOwnUsn, testData.m_modifiedSavedSearches);
        }

        if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
            putSavedSearches(
                *gNewItems, userOwnUsn, testData.m_newSavedSearches);
        }
    }

    if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
        int linkedNotebookIndex = 1;

        const auto putLinkedNotebooks =
            [&](QList<qevercloud::LinkedNotebook> & linkedNotebooks) {
                for (int i = 0; i < itemCount; ++i) {
                    auto linkedNotebook =
                        generateLinkedNotebook(linkedNotebookIndex++);
                    linkedNotebook.setUpdateSequenceNum(userOwnUsn++);
                    linkedNotebooks << linkedNotebook;
                    linkedNotebookUsns[*linkedNotebook.guid()] = 1;
                }
            };

        if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
            putLinkedNotebooks(testData.m_baseLinkedNotebooks);
        }

        if (generatorOptions.testFlag(GeneratorOption::IncludeModifiedItems)) {
            putLinkedNotebooks(testData.m_modifiedLinkedNotebooks);
        }

        if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
            putLinkedNotebooks(testData.m_newLinkedNotebooks);
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Tag)) {
        int tagIndex = 1;

        const auto putTags =
            [&](const QString & nameSuffix, qint32 & updateSequenceNum,
                QList<qevercloud::Tag> & tags,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid =
                    std::nullopt) {
                for (int i = 0; i < itemCount; ++i) {
                    auto tag =
                        generateTag(tagIndex++, nameSuffix, linkedNotebookGuid);
                    tag.setUpdateSequenceNum(updateSequenceNum++);
                    tags << tag;

                    if (i % 2 == 0) {
                        auto childTag = generateTag(
                            tagIndex++, nameSuffix, linkedNotebookGuid);
                        childTag.setUpdateSequenceNum(updateSequenceNum++);
                        childTag.setParentGuid(tag.guid());
                        tags << childTag;
                    }
                }
            };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putTags(*gBaseItems, userOwnUsn, testData.m_userOwnBaseTags);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putTags(
                    *gModifiedItems, userOwnUsn,
                    testData.m_userOwnModifiedTags);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putTags(*gNewItems, userOwnUsn, testData.m_userOwnNewTags);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            for (auto it = linkedNotebookUsns.begin(),
                      end = linkedNotebookUsns.end();
                 it != end; ++it)
            {
                qint32 & usn = it.value();

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeBaseItems)) {
                    putTags(
                        *gBaseItems, usn, testData.m_linkedNotebookBaseTags,
                        it.key());
                }

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeModifiedItems)) {
                    putTags(
                        *gModifiedItems, usn,
                        testData.m_linkedNotebookModifiedTags, it.key());
                }

                if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems))
                {
                    putTags(
                        *gNewItems, usn, testData.m_linkedNotebookNewTags,
                        it.key());
                }
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Notebook) ||
        dataItemTypes.testFlag(DataItemType::Note))
    {
        int notebookIndex = 1;

        const auto putNotebooks =
            [&](const QString & nameSuffix, qint32 & updateSequenceNum,
                QList<qevercloud::Notebook> & notebooks,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid =
                    std::nullopt) {
                for (int i = 0; i < itemCount; ++i) {
                    auto notebook = generateNotebook(
                        notebookIndex++, nameSuffix, linkedNotebookGuid);
                    notebook.setUpdateSequenceNum(updateSequenceNum++);
                    notebooks << notebook;
                }
            };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putNotebooks(
                    *gBaseItems, userOwnUsn, testData.m_userOwnBaseNotebooks);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putNotebooks(
                    *gModifiedItems, userOwnUsn,
                    testData.m_userOwnModifiedNotebooks);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putNotebooks(
                    *gNewItems, userOwnUsn, testData.m_userOwnNewNotebooks);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            for (auto it = linkedNotebookUsns.begin(),
                      end = linkedNotebookUsns.end();
                 it != end; ++it)
            {
                qint32 & usn = it.value();

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeBaseItems)) {
                    putNotebooks(
                        *gNewItems, usn, testData.m_linkedNotebookBaseNotebooks,
                        it.key());
                }

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeModifiedItems)) {
                    putNotebooks(
                        *gModifiedItems, usn,
                        testData.m_linkedNotebookModifiedNotebooks, it.key());
                }

                if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems))
                {
                    putNotebooks(
                        *gNewItems, usn, testData.m_linkedNotebookNewNotebooks,
                        it.key());
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
            [&](const QString & nameSuffix, qint32 & updateSequenceNum,
                QList<qevercloud::Note> & notes,
                const QList<qevercloud::Guid> & notebookGuids) {
                auto notebookIt = notebookGuids.constBegin();
                for (int i = 0; i < itemCount; ++i) {
                    QList<qevercloud::Resource> resources;
                    if (i % 2 == 0) {
                        constexpr int resourceCountPerNote = 3;
                        resources.reserve(resourceCountPerNote);
                        for (int j = 0; j < resourceCountPerNote; ++j) {
                            auto resource = generateResource(j, nameSuffix);
                            resource.setUpdateSequenceNum(updateSequenceNum++);
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

                    note.setUpdateSequenceNum(updateSequenceNum++);
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

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putNotes(
                    *gBaseItems, userOwnUsn, testData.m_userOwnBaseNotes,
                    userOwnNotebookGuids);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putNotes(
                    *gModifiedItems, userOwnUsn,
                    testData.m_userOwnModifiedNotes, userOwnNotebookGuids);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putNotes(
                    *gNewItems, userOwnUsn, testData.m_userOwnNewNotes,
                    userOwnNotebookGuids);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            for (auto it = linkedNotebookUsns.begin(),
                      end = linkedNotebookUsns.end();
                 it != end; ++it)
            {
                qint32 & usn = it.value();

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeBaseItems)) {
                    putNotes(
                        *gBaseItems, usn, testData.m_linkedNotebookBaseNotes,
                        QList{it.key()});
                }

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeModifiedItems)) {
                    putNotes(
                        *gModifiedItems, usn,
                        testData.m_linkedNotebookModifiedNotes,
                        QList{it.key()});
                }

                if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems))
                {
                    putNotes(
                        *gNewItems, usn, testData.m_linkedNotebookNewNotes,
                        QList{it.key()});
                }
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Resource)) {
        int resourceIndex = 1;

        int noteGuidsListIndex = 0;
        const auto putResources =
            [&](const QString & nameSuffix, qint32 & updateSequenceNum,
                QList<qevercloud::Resource> & resources,
                const QList<qevercloud::Guid> & noteGuids) {
                for (int i = 0; i < itemCount; ++i) {
                    auto noteGuid = noteGuids[noteGuidsListIndex++];
                    if (noteGuidsListIndex >= noteGuids.size()) {
                        noteGuidsListIndex = 0;
                    }

                    auto resource =
                        generateResource(resourceIndex++, nameSuffix);
                    resource.setUpdateSequenceNum(updateSequenceNum++);
                    resource.setNoteGuid(std::move(noteGuid));
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
            generatorOptions.testFlag(GeneratorOption::IncludeModifiedItems))
        {
            putResources(
                *gBaseItems, userOwnUsn, testData.m_userOwnModifiedResources,
                userOwnNoteGuids);
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook) &&
            generatorOptions.testFlag(GeneratorOption::IncludeModifiedItems))
        {
            for (auto it = linkedNotebookUsns.begin(),
                      end = linkedNotebookUsns.end();
                 it != end; ++it)
            {
                qint32 & usn = it.value();

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
                        if (notebookIt->linkedNotebookGuid() == it.key()) {
                            result << *note.guid();
                        }
                    }

                    return result;
                }();

                putResources(
                    *gModifiedItems, usn,
                    testData.m_linkedNotebookModifiedResources, noteGuids);
            }
        }
    }
}

} // namespace quentier::synchronization::tests::note_store
