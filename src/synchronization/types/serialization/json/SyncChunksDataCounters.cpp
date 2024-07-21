/*
 * Copyright 2024 Dmitry Ivanov
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

#include <quentier/synchronization/types/serialization/json/SyncChunksDataCounters.h>

#include <synchronization/types/SyncChunksDataCounters.h>

#include "../Utils.h"

#include <QJsonArray>

#include <string_view>

namespace quentier::synchronization {

using namespace std::string_view_literals;

namespace {

constexpr auto gTotalSavedSearchesKey = "savedSearches"sv;
constexpr auto gTotalExpungedSavedSearchesKey = "totalExpungedSavedSearches"sv;
constexpr auto gAddedSavedSearchesKey = "addedSavedSearches"sv;
constexpr auto gUpdatedSavedSearchesKey = "updatedSavedSearches"sv;
constexpr auto gExpungedSavedSearchesKey = "expungedSavedSearches"sv;
constexpr auto gTotalTagsKey = "totalTags"sv;
constexpr auto gTotalExpungedTagsKey = "totalExpungedTags"sv;
constexpr auto gAddedTagsKey = "addedTags"sv;
constexpr auto gUpdatedTagsKey = "updatedTags"sv;
constexpr auto gExpungedTagsKey = "expungedTags"sv;
constexpr auto gTotalLinkedNotebooksKey = "totalLinkedNotebooks"sv;
constexpr auto gTotalExpungedLinkedNotebooksKey =
    "totalExpungedLinkedNotebooks"sv;

constexpr auto gAddedLinkedNotebooksKey = "addedLinkedNotebooks"sv;
constexpr auto gUpdatedLinkedNotebooksKey = "updatedLinkedNotebooks"sv;
constexpr auto gExpungedLinkedNotebooksKey = "expungedLinkedNotebooks"sv;
constexpr auto gTotalNotebooksKey = "totalNotebooks"sv;
constexpr auto gTotalExpungedNotebooksKey = "totalExpungedNotebooks"sv;
constexpr auto gAddedNotebooksKey = "addedNotebooks"sv;
constexpr auto gUpdatedNotebooksKey = "updatedNotebooks"sv;
constexpr auto gExpungedNotebooksKey = "expungedNotebooks"sv;

[[nodiscard]] QString toStr(const std::string_view key)
{
    return synchronization::toString(key);
}

[[nodiscard]] bool readCounter(
    const std::string_view key, const QJsonObject & json,
    quint64 & counterValue)
{
    const auto it = json.constFind(toStr(key));
    if (it == json.constEnd() || !it->isString()) {
        return false;
    }

    bool conversionResult = false;
    counterValue = it->toString().toULongLong(&conversionResult);
    return conversionResult;
}

} // namespace

QJsonObject serializeSyncChunksDataCountersToJson(
    const ISyncChunksDataCounters & counters)
{
    QJsonObject object;

    // Saved searches
    object[toStr(gTotalSavedSearchesKey)] =
        QString::number(counters.totalSavedSearches());

    object[toStr(gTotalExpungedSavedSearchesKey)] =
        QString::number(counters.totalExpungedSavedSearches());

    object[toStr(gAddedSavedSearchesKey)] =
        QString::number(counters.addedSavedSearches());

    object[toStr(gUpdatedSavedSearchesKey)] =
        QString::number(counters.updatedSavedSearches());

    object[toStr(gExpungedSavedSearchesKey)] =
        QString::number(counters.expungedSavedSearches());

    // Tags
    object[toStr(gTotalTagsKey)] = QString::number(counters.totalTags());
    object[toStr(gTotalExpungedTagsKey)] =
        QString::number(counters.totalExpungedTags());

    object[toStr(gAddedTagsKey)] = QString::number(counters.addedTags());
    object[toStr(gUpdatedTagsKey)] = QString::number(counters.updatedTags());
    object[toStr(gExpungedTagsKey)] = QString::number(counters.expungedTags());

    // Linked notebooks
    object[toStr(gTotalLinkedNotebooksKey)] =
        QString::number(counters.totalLinkedNotebooks());

    object[toStr(gTotalExpungedLinkedNotebooksKey)] =
        QString::number(counters.totalExpungedLinkedNotebooks());

    object[toStr(gAddedLinkedNotebooksKey)] =
        QString::number(counters.addedLinkedNotebooks());

    object[toStr(gUpdatedLinkedNotebooksKey)] =
        QString::number(counters.updatedLinkedNotebooks());

    object[toStr(gExpungedLinkedNotebooksKey)] =
        QString::number(counters.expungedLinkedNotebooks());

    // Notebooks
    object[toStr(gTotalNotebooksKey)] =
        QString::number(counters.totalNotebooks());

    object[toStr(gTotalExpungedNotebooksKey)] =
        QString::number(counters.totalExpungedNotebooks());

    object[toStr(gAddedNotebooksKey)] =
        QString::number(counters.addedNotebooks());

    object[toStr(gUpdatedNotebooksKey)] =
        QString::number(counters.updatedNotebooks());

    object[toStr(gExpungedNotebooksKey)] =
        QString::number(counters.expungedNotebooks());

    return object;
}

ISyncChunksDataCountersPtr deserializeSyncChunksDataCountersFromJson(
    const QJsonObject & json)
{
    auto counters = std::make_shared<SyncChunksDataCounters>();

    // Saved searches
    if (!readCounter(
            gTotalSavedSearchesKey, json, counters->m_totalSavedSearches))
    {
        return nullptr;
    }

    if (!readCounter(
            gTotalExpungedSavedSearchesKey, json,
            counters->m_totalExpungedSavedSearches))
    {
        return nullptr;
    }

    if (!readCounter(
            gAddedSavedSearchesKey, json, counters->m_addedSavedSearches))
    {
        return nullptr;
    }

    if (!readCounter(
            gUpdatedSavedSearchesKey, json, counters->m_updatedSavedSearches))
    {
        return nullptr;
    }

    if (!readCounter(
            gExpungedSavedSearchesKey, json, counters->m_expungedSavedSearches))
    {
        return nullptr;
    }

    // Tags
    if (!readCounter(gTotalTagsKey, json, counters->m_totalTags)) {
        return nullptr;
    }

    if (!readCounter(
            gTotalExpungedTagsKey, json, counters->m_totalExpungedTags))
    {
        return nullptr;
    }

    if (!readCounter(gAddedTagsKey, json, counters->m_addedTags)) {
        return nullptr;
    }

    if (!readCounter(gUpdatedTagsKey, json, counters->m_updatedTags)) {
        return nullptr;
    }

    if (!readCounter(gExpungedTagsKey, json, counters->m_expungedTags)) {
        return nullptr;
    }

    // Linked notebooks
    if (!readCounter(
            gTotalLinkedNotebooksKey, json, counters->m_totalLinkedNotebooks))
    {
        return nullptr;
    }

    if (!readCounter(
            gTotalExpungedLinkedNotebooksKey, json,
            counters->m_totalExpungedLinkedNotebooks))
    {
        return nullptr;
    }

    if (!readCounter(
            gAddedLinkedNotebooksKey, json, counters->m_addedLinkedNotebooks))
    {
        return nullptr;
    }

    if (!readCounter(
            gUpdatedLinkedNotebooksKey, json,
            counters->m_updatedLinkedNotebooks))
    {
        return nullptr;
    }

    if (!readCounter(
            gExpungedLinkedNotebooksKey, json,
            counters->m_expungedLinkedNotebooks))
    {
        return nullptr;
    }

    // Notebooks
    if (!readCounter(gTotalNotebooksKey, json, counters->m_totalNotebooks)) {
        return nullptr;
    }

    if (!readCounter(
            gTotalExpungedNotebooksKey, json,
            counters->m_totalExpungedNotebooks))
    {
        return nullptr;
    }

    if (!readCounter(gAddedNotebooksKey, json, counters->m_addedNotebooks)) {
        return nullptr;
    }

    if (!readCounter(gUpdatedNotebooksKey, json, counters->m_updatedNotebooks))
    {
        return nullptr;
    }

    if (!readCounter(
            gExpungedNotebooksKey, json, counters->m_expungedNotebooks))
    {
        return nullptr;
    }

    return counters;
}

} // namespace quentier::synchronization
