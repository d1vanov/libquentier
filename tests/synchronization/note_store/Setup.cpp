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
#include "../NoteStoreServer.h"

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

#include <utility>

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

void setupNoteStoreServer(
    const DataItemTypes dataItemTypes, const GeneratorOptions generatorOptions,
    const ItemSources itemSources, NoteStoreServer & noteStoreServer)
{
    constexpr int itemCount = 10;

    if (dataItemTypes.testFlag(DataItemType::SavedSearch) &&
        itemSources.testFlag(ItemSource::UserOwnAccount))
    {
        int savedSearchIndex = 1;

        const auto putSavedSearches = [&](const QString & nameSuffix) {
            for (int i = 0; i < itemCount; ++i) {
                auto savedSearch =
                    generateSavedSearch(savedSearchIndex++, nameSuffix);
                noteStoreServer.putSavedSearch(std::move(savedSearch));
            }
        };

        if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
            putSavedSearches(*gBaseItems);
        }

        if (generatorOptions.testFlag(GeneratorOption::IncludeModifiedItems)) {
            putSavedSearches(*gModifiedItems);
        }

        if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
            putSavedSearches(*gNewItems);
        }
    }

    if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
        int linkedNotebookIndex = 1;
        for (int i = 0; i < itemCount; ++i) {
            auto linkedNotebook = generateLinkedNotebook(linkedNotebookIndex++);
            noteStoreServer.putLinkedNotebook(linkedNotebook);
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Tag)) {
        int tagIndex = 1;

        const auto putTags =
            [&](const QString & nameSuffix,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid =
                    std::nullopt) {
                for (int i = 0; i < itemCount; ++i) {
                    auto tag =
                        generateTag(tagIndex++, nameSuffix, linkedNotebookGuid);

                    auto childTag =
                        generateTag(tagIndex++, nameSuffix, linkedNotebookGuid);

                    childTag.setParentGuid(tag.guid());
                    noteStoreServer.putTag(std::move(tag));
                    noteStoreServer.putTag(std::move(childTag));
                }
            };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putTags(*gBaseItems);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putTags(*gModifiedItems);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putTags(*gNewItems);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            const auto linkedNotebooksByGuid =
                noteStoreServer.linkedNotebooks();

            for (const auto it:
                 qevercloud::toRange(qAsConst(linkedNotebooksByGuid))) {
                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeBaseItems)) {
                    putTags(*gBaseItems, it.key());
                }

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeModifiedItems)) {
                    putTags(*gModifiedItems, it.key());
                }

                if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems))
                {
                    putTags(*gNewItems, it.key());
                }
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Notebook) ||
        dataItemTypes.testFlag(DataItemType::Note))
    {
        int notebookIndex = 1;

        const auto putNotebooks =
            [&](const QString & nameSuffix,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid =
                    std::nullopt) {
                for (int i = 0; i < itemCount; ++i) {
                    auto notebook = generateNotebook(
                        notebookIndex++, nameSuffix, linkedNotebookGuid);

                    noteStoreServer.putNotebook(std::move(notebook));
                }
            };

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putNotebooks(*gBaseItems);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putNotebooks(*gModifiedItems);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putNotebooks(*gNewItems);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            const auto linkedNotebooksByGuid =
                noteStoreServer.linkedNotebooks();

            for (const auto it:
                 qevercloud::toRange(qAsConst(linkedNotebooksByGuid))) {
                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeBaseItems)) {
                    putNotebooks(*gNewItems, it.key());
                }

                if (generatorOptions.testFlag(
                        GeneratorOption::IncludeModifiedItems)) {
                    putNotebooks(*gModifiedItems, it.key());
                }

                if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems))
                {
                    putNotebooks(*gNewItems, it.key());
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

            const auto tagsByGuid = noteStoreServer.tags();
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
            [&](const QString & nameSuffix,
                const QList<qevercloud::Guid> & notebookGuids) {
                auto notebookIt = notebookGuids.constBegin();
                for (int i = 0; i < itemCount; ++i) {
                    QList<qevercloud::Resource> resources;
                    if (i % 2 == 0) {
                        constexpr int resourceCountPerNote = 3;
                        resources.reserve(resourceCountPerNote);
                        for (int j = 0; j < resourceCountPerNote; ++j) {
                            resources << generateResource(j, nameSuffix);
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

                    noteStoreServer.putNote(std::move(note));
                }
            };

        auto [userOwnNotebookGuids, linkedNotebookGuids] = [&] {
            QList<qevercloud::Guid> userOwnNotebookGuids;
            QList<qevercloud::Guid> linkedNotebookGuids;
            const auto notebooksByGuid = noteStoreServer.notebooks();
            for (const auto it: qevercloud::toRange(qAsConst(notebooksByGuid)))
            {
                const auto & notebook = it.value();
                if (notebook.linkedNotebookGuid()) {
                    linkedNotebookGuids << notebook.guid().value();
                }
                else {
                    userOwnNotebookGuids << notebook.guid().value();
                }
            }

            return std::make_pair(
                std::move(userOwnNotebookGuids),
                std::move(linkedNotebookGuids));
        }();

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putNotes(*gBaseItems, userOwnNotebookGuids);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putNotes(*gModifiedItems, userOwnNotebookGuids);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putNotes(*gNewItems, userOwnNotebookGuids);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putNotes(*gBaseItems, linkedNotebookGuids);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putNotes(*gModifiedItems, linkedNotebookGuids);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putNotes(*gNewItems, linkedNotebookGuids);
            }
        }
    }

    if (dataItemTypes.testFlag(DataItemType::Resource)) {
        int resourceIndex = 1;
        const auto notesByGuid = noteStoreServer.notes();

        const QList<qevercloud::Guid> noteGuids = [&] {
            return notesByGuid.keys();
        }();

        Q_ASSERT(!noteGuids.isEmpty());

        int noteGuidsListIndex = 0;
        const auto putResources =
            [&](const QString & nameSuffix,
                const QList<qevercloud::Guid> & noteGuids) {
                for (int i = 0; i < itemCount; ++i) {
                    auto noteGuid = noteGuids[noteGuidsListIndex++];
                    if (noteGuidsListIndex >= noteGuids.size()) {
                        noteGuidsListIndex = 0;
                    }

                    auto resource = generateResource(resourceIndex++, nameSuffix);
                    resource.setNoteGuid(std::move(noteGuid));

                    noteStoreServer.putResource(std::move(resource));
                }
            };

        auto [userOwnNoteGuids, linkedNotebookNoteGuids] = [&] {
            QList<qevercloud::Guid> userOwnNoteGuids;
            QList<qevercloud::Guid> linkedNotebookNoteGuids;
            const auto notebooksByGuids = noteStoreServer.notebooks();
            for (const auto it: qevercloud::toRange(qAsConst(notesByGuid))) {
                const auto & note = it.value();
                Q_ASSERT(note.guid());
                Q_ASSERT(note.notebookGuid());

                const auto notebookIt =
                    notebooksByGuids.constFind(*note.notebookGuid());
                Q_ASSERT(notebookIt != notebooksByGuids.constEnd());
                if (notebookIt->linkedNotebookGuid()) {
                    linkedNotebookNoteGuids << *note.guid();
                }
                else {
                    userOwnNoteGuids << *note.guid();
                }
            }
            return std::make_pair(
                std::move(userOwnNoteGuids),
                std::move(linkedNotebookNoteGuids));
        }();

        if (itemSources.testFlag(ItemSource::UserOwnAccount)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putResources(*gBaseItems, userOwnNoteGuids);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putResources(*gModifiedItems, userOwnNoteGuids);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putResources(*gNewItems, userOwnNoteGuids);
            }
        }

        if (itemSources.testFlag(ItemSource::LinkedNotebook)) {
            if (generatorOptions.testFlag(GeneratorOption::IncludeBaseItems)) {
                putResources(*gBaseItems, linkedNotebookNoteGuids);
            }

            if (generatorOptions.testFlag(
                    GeneratorOption::IncludeModifiedItems)) {
                putResources(*gModifiedItems, linkedNotebookNoteGuids);
            }

            if (generatorOptions.testFlag(GeneratorOption::IncludeNewItems)) {
                putResources(*gNewItems, linkedNotebookNoteGuids);
            }
        }
    }
}

} // namespace quentier::synchronization::tests::note_store
