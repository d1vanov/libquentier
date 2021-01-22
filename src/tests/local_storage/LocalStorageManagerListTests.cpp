/*
 * Copyright 2019-2021 Dmitry Ivanov
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

#include "LocalStorageManagerListTests.h"

#include <quentier/local_storage/LocalStorageManager.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/CommonUtils.h>
#include <quentier/types/NoteUtils.h>
#include <quentier/utility/UidGenerator.h>

#include <QtTest/QtTest>

namespace quentier {
namespace test {

void TestListSavedSearches()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nSearches = 5;
    QList<qevercloud::SavedSearch> searches;
    searches.reserve(nSearches);
    for (int i = 0; i < nSearches; ++i) {
        searches << qevercloud::SavedSearch();
        auto & search = searches.back();

        if (i > 1) {
            search.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        search.setUpdateSequenceNum(i);

        search.setName(QStringLiteral("SavedSearch #") + QString::number(i));

        search.setQuery(
            QStringLiteral("Fake saved search query #") + QString::number(i));

        search.setFormat(qevercloud::QueryFormat::USER);
        search.setScope(qevercloud::SavedSearchScope{});
        search.mutableScope()->setIncludeAccount(true);
        search.mutableScope()->setIncludeBusinessLinkedNotebooks(true);
        search.mutableScope()->setIncludePersonalLinkedNotebooks(true);

        if (i > 2) {
            search.setLocallyModified(true);
        }
        else {
            search.setLocallyModified(false);
        }

        if (i < 3) {
            search.setLocalOnly(true);
        }
        else {
            search.setLocalOnly(false);
        }

        if ((i == 0) || (i == 4)) {
            search.setLocallyFavorited(true);
        }
        else {
            search.setLocallyFavorited(false);
        }

        bool res = localStorageManager.addSavedSearch(search, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all saved searches

    errorMessage.clear();

    QList<qevercloud::SavedSearch> foundSearches =
        localStorageManager.listAllSavedSearches(errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    int numFoundSearches = foundSearches.size();
    if (numFoundSearches != nSearches) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of saved searches "
                           "in the result of LocalStorageManager::"
                           "ListAllSavedSearches (") +
            QString::number(numFoundSearches) +
            QStringLiteral(") does not match the original number "
                           "of added saved searches (") +
            QString::number(nSearches) + QStringLiteral(")")));
    }

    for (int i = 0; i < numFoundSearches; ++i) {
        const auto & foundSearch = foundSearches.at(i);
        if (!searches.contains(foundSearch)) {
            QFAIL(
                "One of saved searches from the result of "
                "LocalStorageManager::ListAllSavedSearches "
                "was not found in the list of original searches");
        }
    }

#define CHECK_LIST_SAVED_SEARCHES(flag, name, true_cond, false_cond)           \
    errorMessage.clear();                                                      \
    foundSearches = localStorageManager.listSavedSearches(flag, errorMessage); \
    QVERIFY2(                                                                  \
        errorMessage.isEmpty(),                                                \
        qPrintable(errorMessage.nonLocalizedString()));                        \
    for (int i = 0; i < nSearches; ++i) {                                      \
        const auto & search = searches.at(i);                                  \
        bool res = foundSearches.contains(search);                             \
        if ((true_cond) && !res) {                                             \
            QNWARNING(                                                         \
                "tests:local_storage", "Not found saved search: " << search);  \
            QFAIL("One of " name                                               \
                  " SavedSearches was not found by "                           \
                  "LocalStorageManager::ListSavedSearches");                   \
        }                                                                      \
        else if ((false_cond) && res) {                                        \
            QNWARNING(                                                         \
                "tests:local_storage",                                         \
                "Found irrelevant saved search: " << search);                  \
            QFAIL("LocalStorageManager::ListSavedSearches with flag " name     \
                  " returned incorrect saved search");                         \
        }                                                                      \
    }

    using ListObjectsOption = LocalStorageManager::ListObjectsOption;

    // 2) Test method listing only dirty saved searches
    CHECK_LIST_SAVED_SEARCHES(
        ListObjectsOption::ListDirty, "dirty", i > 2, i <= 2);

    // 3) Test method listing only local saved searches
    CHECK_LIST_SAVED_SEARCHES(
        ListObjectsOption::ListLocal, "local", i < 3, i >= 3);

    // 4) Test method listing only saved searches without guid
    CHECK_LIST_SAVED_SEARCHES(
        ListObjectsOption::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

    // 5) Test method listing only favorited saved searches
    CHECK_LIST_SAVED_SEARCHES(
        ListObjectsOption::ListFavoritedElements, "favorited",
        (i == 0) || (i == 4), (i != 0) && (i != 4));

    // 6) Test method listing dirty favorited saved searches with guid
    CHECK_LIST_SAVED_SEARCHES(
        ListObjectsOption::ListDirty | ListObjectsOption::ListElementsWithGuid |
            ListObjectsOption::ListFavoritedElements,
        "dirty, favorited, having guid", i == 4, i != 4);

    // 7) Test method listing local favorited saved searches
    CHECK_LIST_SAVED_SEARCHES(
        ListObjectsOption::ListLocal | ListObjectsOption::ListFavoritedElements,
        "local, favorited", i == 0, i != 0);

    // 8) Test method listing saved searches with guid set also specifying
    // limit, offset and order
    size_t limit = 2;
    size_t offset = 1;
    auto order =
        LocalStorageManager::ListSavedSearchesOrder::ByUpdateSequenceNumber;

    errorMessage.clear();

    foundSearches = localStorageManager.listSavedSearches(
        ListObjectsOption::ListElementsWithGuid, errorMessage, limit, offset,
        order);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    if (foundSearches.size() != static_cast<int>(limit)) {
        QFAIL(qPrintable(
            QStringLiteral("Unexpected number of found saved searches "
                           "not corresponding to the specified limit: "
                           "limit = ") +
            QString::number(limit) +
            QStringLiteral(", number of searches found is ") +
            QString::number(foundSearches.size())));
    }

    const auto & firstSearch = foundSearches[0];
    const auto & secondSearch = foundSearches[1];

    if (!firstSearch.updateSequenceNum() ||
        !secondSearch.updateSequenceNum())
    {
        QFAIL(qPrintable(
            QStringLiteral("One of found saved searches doesn't "
                           "have the update sequence number "
                           "which is unexpected: first search: ") +
            firstSearch.toString() + QStringLiteral("\nSecond search: ") +
            secondSearch.toString()));
    }

    if (firstSearch.updateSequenceNum().value() != 3) {
        QFAIL(qPrintable(
            QStringLiteral("First saved search was expected to "
                           "have update sequence number of 3, "
                           "instead it is ") +
            QString::number(firstSearch.updateSequenceNum().value())));
    }

    if (secondSearch.updateSequenceNum().value() != 4) {
        QFAIL(qPrintable(
            QStringLiteral("Second saved search was expected "
                           "to have update sequence number of 4, "
                           "instead it is ") +
            QString::number(secondSearch.updateSequenceNum().value())));
    }
}

void TestListLinkedNotebooks()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nLinkedNotebooks = 5;
    QList<qevercloud::LinkedNotebook> linkedNotebooks;
    linkedNotebooks.reserve(nLinkedNotebooks);
    for (int i = 0; i < nLinkedNotebooks; ++i) {
        linkedNotebooks << qevercloud::LinkedNotebook();
        auto & linkedNotebook = linkedNotebooks.back();

        linkedNotebook.setGuid(
            QStringLiteral("00000000-0000-0000-c000-00000000000") +
            QString::number(i + 1));

        linkedNotebook.setUpdateSequenceNum(i);

        linkedNotebook.setShareName(
            QStringLiteral("Linked notebook share name #") +
            QString::number(i));

        linkedNotebook.setUsername(
            QStringLiteral("Linked notebook username #") + QString::number(i));

        linkedNotebook.setShardId(
            QStringLiteral("Linked notebook shard id #") + QString::number(i));

        linkedNotebook.setSharedNotebookGlobalId(
            QStringLiteral("Linked notebook shared notebook global id #") +
            QString::number(i));

        linkedNotebook.setUri(
            QStringLiteral("Linked notebook uri #") + QString::number(i));

        linkedNotebook.setNoteStoreUrl(
            QStringLiteral("Linked notebook note store url #") +
            QString::number(i));

        linkedNotebook.setWebApiUrlPrefix(
            QStringLiteral("Linked notebook web api url prefix #") +
            QString::number(i));

        linkedNotebook.setStack(
            QStringLiteral("Linked notebook stack #") + QString::number(i));

        linkedNotebook.setBusinessId(1);

        if (i > 2) {
            linkedNotebook.setLocallyModified(true);
        }
        else {
            linkedNotebook.setLocallyModified(false);
        }

        bool res =
            localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);

        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all linked notebooks

    errorMessage.clear();

    QList<qevercloud::LinkedNotebook> foundLinkedNotebooks =
        localStorageManager.listAllLinkedNotebooks(errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    int numFoundLinkedNotebooks = foundLinkedNotebooks.size();
    if (numFoundLinkedNotebooks != nLinkedNotebooks) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of linked notebooks "
                           "in the result of LocalStorageManager::"
                           "ListAllLinkedNotebooks (") +
            QString::number(numFoundLinkedNotebooks) +
            QStringLiteral(") does not match the original number "
                           "of added linked notebooks (") +
            QString::number(nLinkedNotebooks) + QStringLiteral(")")));
    }

    for (int i = 0; i < numFoundLinkedNotebooks; ++i) {
        const auto & foundLinkedNotebook = foundLinkedNotebooks.at(i);
        if (!linkedNotebooks.contains(foundLinkedNotebook)) {
            QFAIL(
                "One of linked notebooks from the result of "
                "LocalStorageManager::ListAllLinkedNotebooks "
                "was not found in the list of original linked notebooks");
        }
    }

    // 2) Test method listing only dirty linked notebooks
    errorMessage.clear();

    foundLinkedNotebooks = localStorageManager.listLinkedNotebooks(
        LocalStorageManager::ListObjectsOption::ListDirty, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    for (int i = 0; i < nLinkedNotebooks; ++i) {
        const auto & linkedNotebook = linkedNotebooks.at(i);
        bool res = foundLinkedNotebooks.contains(linkedNotebook);
        if ((i > 2) && !res) {
            QNWARNING(
                "tests:local_storage",
                "Not found linked notebook: " << linkedNotebook);
            QFAIL(
                "One of dirty linked notebooks was not found by "
                "LocalStorageManager::ListLinkedNotebooks");
        }
        else if ((i <= 2) && res) {
            QNWARNING(
                "tests:local_storage",
                "Found irrelevant linked notebook: " << linkedNotebook);
            QFAIL(
                "LocalStorageManager::ListLinkedNotebooks with flag "
                "ListDirty returned incorrect linked notebook");
        }
    }
}

void TestListTags()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nTags = 5;
    QVector<qevercloud::Tag> tags;
    tags.reserve(nTags);
    for (int i = 0; i < nTags; ++i) {
        tags.push_back(qevercloud::Tag());
        auto & tag = tags.back();

        if (i > 1) {
            tag.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        tag.setUpdateSequenceNum(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 2) {
            tag.setParentGuid(tags.at(i - 1).guid());
        }

        if (i > 2) {
            tag.setLocallyModified(true);
        }
        else {
            tag.setLocallyModified(false);
        }

        if (i < 3) {
            tag.setLocalOnly(true);
        }
        else {
            tag.setLocalOnly(false);
        }

        if ((i == 0) || (i == 4)) {
            tag.setLocallyFavorited(true);
        }
        else {
            tag.setLocallyFavorited(false);
        }

        bool res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all tags

    errorMessage.clear();
    QList<qevercloud::Tag> foundTags =
        localStorageManager.listAllTags(errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    int numFoundTags = foundTags.size();
    if (numFoundTags != nTags) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of tags in the result "
                           "of LocalStorageManager::ListAllTags (") +
            QString::number(numFoundTags) +
            QStringLiteral(") does not match the original number "
                           "of added tags (") +
            QString::number(nTags) + QStringLiteral(")")));
    }

    for (int i = 0; i < numFoundTags; ++i) {
        const auto & foundTag = foundTags.at(i);
        if (!tags.contains(foundTag)) {
            QFAIL(
                "One of tags from the result of "
                "LocalStorageManager::ListAllTags was not found in the list "
                "of original tags");
        }
    }

#define CHECK_LIST_TAGS(flag, name, true_cond, false_cond)                     \
    errorMessage.clear();                                                      \
    foundTags = localStorageManager.listTags(flag, errorMessage);              \
    QVERIFY2(                                                                  \
        errorMessage.isEmpty(),                                                \
        qPrintable(errorMessage.nonLocalizedString()));                        \
    for (int i = 0; i < nTags; ++i) {                                          \
        const auto & tag = tags.at(i);                                         \
        bool res = foundTags.contains(tag);                                    \
        if ((true_cond) && !res) {                                             \
            QNWARNING("tests:local_storage", "Not found tag: " << tag);        \
            QFAIL("One of " name                                               \
                  " Tags was not found by "                                    \
                  "LocalStorageManager::ListTags");                            \
        }                                                                      \
        else if ((false_cond) && res) {                                        \
            QNWARNING("tests:local_storage", "Found irrelevant tag: " << tag); \
            QFAIL("LocalStorageManager::ListTags with flag " name              \
                  " returned incorrect tag");                                  \
        }                                                                      \
    }

    using ListObjectsOption = LocalStorageManager::ListObjectsOption;

    // 2) Test method listing only dirty tags
    CHECK_LIST_TAGS(ListObjectsOption::ListDirty, "dirty", i > 2, i <= 2);

    // 3) Test method listing only local tags
    CHECK_LIST_TAGS(ListObjectsOption::ListLocal, "local", i < 3, i >= 3);

    // 4) Test method listing only tags without guid
    CHECK_LIST_TAGS(
        ListObjectsOption::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

    // 5) Test method listing only favorited tags
    CHECK_LIST_TAGS(
        ListObjectsOption::ListFavoritedElements, "favorited",
        (i == 0) || (i == 4), (i != 0) && (i != 4));

    // 6) Test method listing dirty favorited tags with guid
    CHECK_LIST_TAGS(
        ListObjectsOption::ListDirty | ListObjectsOption::ListElementsWithGuid |
            ListObjectsOption::ListFavoritedElements,
        "dirty, favorited, having guid", i == 4, i != 4);

    // 7) Test method listing local favorited tags
    CHECK_LIST_TAGS(
        ListObjectsOption::ListLocal | ListObjectsOption::ListFavoritedElements,
        "local, favorited", i == 0, i != 0);

#undef CHECK_LIST_TAGS
}

void TestListTagsWithNoteLocalUids()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nTags = 5;
    QVector<qevercloud::Tag> tags;
    tags.reserve(nTags);
    for (int i = 0; i < nTags; ++i) {
        tags.push_back(qevercloud::Tag());
        auto & tag = tags.back();

        if (i > 1) {
            tag.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        tag.setUpdateSequenceNum(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 2) {
            tag.setParentGuid(tags.at(i - 1).guid());
        }

        if (i > 2) {
            tag.setLocallyModified(true);
        }
        else {
            tag.setLocallyModified(false);
        }

        if (i < 3) {
            tag.setLocalOnly(true);
        }
        else {
            tag.setLocalOnly(false);
        }

        if ((i == 0) || (i == 4)) {
            tag.setLocallyFavorited(true);
        }
        else {
            tag.setLocallyFavorited(false);
        }

        bool res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // Now add some notebooks and notes using the just created tags
    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);

    errorMessage.clear();
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    QMap<QString, QStringList> noteLocalIdsByTagLocalId;

    int numNotes = 5;
    QList<qevercloud::Note> notes;
    notes.reserve(numNotes);
    for (int i = 0; i < numNotes; ++i) {
        notes << qevercloud::Note();
        auto & note = notes.back();

        if (i > 1) {
            note.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        if (i > 2) {
            note.setLocallyModified(true);
        }
        else {
            note.setLocallyModified(false);
        }

        if (i < 3) {
            note.setLocalOnly(true);
        }
        else {
            note.setLocalOnly(false);
        }

        if ((i == 0) || (i == 4)) {
            note.setLocallyFavorited(true);
        }
        else {
            note.setLocallyFavorited(false);
        }

#define APPEND_TAG_TO_NOTE(tagNum)                                             \
    addNoteTagLocalId(tags[tagNum].localId(), note);                           \
    noteLocalIdsByTagLocalId[tags[tagNum].localId()] << note.localId()

        if (i == 0) {
            APPEND_TAG_TO_NOTE(1);
            APPEND_TAG_TO_NOTE(2);
            APPEND_TAG_TO_NOTE(3);
        }
        else if (i == 3) {
            APPEND_TAG_TO_NOTE(1);
            APPEND_TAG_TO_NOTE(4);
        }
        else if (i == 4) {
            APPEND_TAG_TO_NOTE(2);
        }

#undef APPEND_TAG_TO_NOTE

        note.setUpdateSequenceNum(i + 1);
        note.setTitle(QStringLiteral("Fake note title #") + QString::number(i));

        note.setContent(
            QStringLiteral("<en-note><h1>Hello, world #") + QString::number(i) +
            QStringLiteral("</h1></en-note>"));

        note.setCreated(i + 1);
        note.setUpdated(i + 1);
        note.setActive(true);
        note.setNotebookGuid(notebook.guid());
        note.setParentLocalId(notebook.localId());

        res = localStorageManager.addNote(note, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    QList<std::pair<qevercloud::Tag, QStringList>> foundTagsWithNoteLocalIds;

#define CHECK_LIST_TAGS(flag, name, true_cond, false_cond)                     \
    errorMessage.clear();                                                      \
    foundTagsWithNoteLocalIds =                                                \
        localStorageManager.listTagsWithNoteLocalIds(flag, errorMessage);      \
    QVERIFY2(                                                                  \
        errorMessage.isEmpty(),                                                \
        qPrintable(errorMessage.nonLocalizedString()));                        \
    for (int i = 0; i < nTags; ++i) {                                          \
        const auto & tag = tags.at(i);                                         \
        int tagIndex = 0;                                                      \
        bool res = false;                                                      \
        for (int size = foundTagsWithNoteLocalIds.size(); tagIndex < size;     \
             ++tagIndex) {                                                     \
            if (foundTagsWithNoteLocalIds[tagIndex].first == tag) {            \
                res = true;                                                    \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        if ((true_cond) && !res) {                                             \
            QNWARNING("tests:local_storage", "Not found tag: " << tag);        \
            QFAIL("One of " name                                               \
                  " Tags was not found by LocalStorageManager::ListTags");     \
        }                                                                      \
        else if ((false_cond) && res) {                                        \
            QNWARNING("tests:local_storage", "Found irrelevant tag: " << tag); \
            QFAIL("LocalStorageManager::ListTags with flag " name              \
                  " returned incorrect tag");                                  \
        }                                                                      \
        else if (res) {                                                        \
            auto noteIt = noteLocalIdsByTagLocalId.find(tag.localId());        \
            if (noteIt == noteLocalIdsByTagLocalId.end() &&                    \
                !foundTagsWithNoteLocalIds[tagIndex].second.isEmpty())         \
            {                                                                  \
                QNWARNING(                                                     \
                    "tests:local_storage",                                     \
                    "Found irrelevant list of note local uids for a tag: "     \
                        << foundTagsWithNoteLocalIds[tagIndex].second.join(    \
                               QStringLiteral(", ")));                         \
                QFAIL("LocalStorageManager::ListTags with flag " name          \
                      " returned redundant note local uids");                  \
            }                                                                  \
            else if (noteIt != noteLocalIdsByTagLocalId.end()) {               \
                if (foundTagsWithNoteLocalIds[tagIndex].second.isEmpty()) {    \
                    QNWARNING(                                                 \
                        "tests:local_storage",                                 \
                        "Found empty list of note local uids for a tag for "   \
                            << "which they were expected: "                    \
                            << noteIt.value().join(QStringLiteral(", ")));     \
                    QFAIL("LocalStorageManager::ListTags with flag " name      \
                          " did not return proper note local uids");           \
                }                                                              \
                else if (                                                      \
                    foundTagsWithNoteLocalIds[tagIndex].second.size() !=       \
                    noteIt.value().size())                                     \
                {                                                              \
                    QNWARNING(                                                 \
                        "tests:local_storage",                                 \
                        "Found list of note local uids for a tag with "        \
                            << "incorrect list size: "                         \
                            << foundTagsWithNoteLocalIds[tagIndex]             \
                                   .second.join(QStringLiteral(", ")));        \
                    QFAIL("LocalStorageManager::ListTags with flag " name      \
                          " did not return proper number of note local uids"); \
                }                                                              \
                else {                                                         \
                    for (int j = 0; j <                                        \
                         foundTagsWithNoteLocalIds[tagIndex].second.size();    \
                         ++j) {                                                \
                        if (!noteIt.value().contains(                          \
                                foundTagsWithNoteLocalIds[tagIndex]            \
                                    .second[j])) {                             \
                            QNWARNING(                                         \
                                "tests:local_storage",                         \
                                "Found incorrect list of note local uids for " \
                                    << "a tag: "                               \
                                    << foundTagsWithNoteLocalIds[tagIndex]     \
                                           .second.join(                       \
                                               QStringLiteral(", ")));         \
                            QFAIL(                                             \
                                "LocalStorageManager::ListTags with "          \
                                "flag " name                                   \
                                " did not return correct set of note local "   \
                                "uids");                                       \
                        }                                                      \
                    }                                                          \
                }                                                              \
            }                                                                  \
        }                                                                      \
    }

    using ListObjectsOption = LocalStorageManager::ListObjectsOption;

    // 1) Test method listing all tags with note local uids
    CHECK_LIST_TAGS(ListObjectsOption::ListAll, "all", true, false);

    // 2) Test method listing only dirty tags
    CHECK_LIST_TAGS(ListObjectsOption::ListDirty, "dirty", i > 2, i <= 2);

    // 3) Test method listing only local tags
    CHECK_LIST_TAGS(ListObjectsOption::ListLocal, "local", i < 3, i >= 3);

    // 4) Test method listing only tags without guid
    CHECK_LIST_TAGS(
        ListObjectsOption::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

    // 5) Test method listing only favorited tags
    CHECK_LIST_TAGS(
        ListObjectsOption::ListFavoritedElements, "favorited",
        (i == 0) || (i == 4), (i != 0) && (i != 4));

    // 6) Test method listing dirty favorited tags with guid
    CHECK_LIST_TAGS(
        ListObjectsOption::ListDirty | ListObjectsOption::ListElementsWithGuid |
            ListObjectsOption::ListFavoritedElements,
        "dirty, favorited, having guid", i == 4, i != 4);

    // 7) Test method listing local favorited tags
    CHECK_LIST_TAGS(
        ListObjectsOption::ListLocal | ListObjectsOption::ListFavoritedElements,
        "local, favorited", i == 0, i != 0);
#undef CHECK_LIST_TAGS
}

void TestListAllSharedNotebooks()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000000"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);
    notebook.setDefaultNotebook(true);
    notebook.setPublished(false);
    notebook.setStack(QStringLiteral("Fake notebook stack"));


    notebook.setSharedNotebooks(QList<qevercloud::SharedNotebook>{});
    int numSharedNotebooks = 5;
    QList<qevercloud::SharedNotebook> sharedNotebooks;
    sharedNotebooks.reserve(numSharedNotebooks);
    for (int i = 0; i < numSharedNotebooks; ++i) {
        sharedNotebooks << qevercloud::SharedNotebook();
        auto & sharedNotebook = sharedNotebooks.back();

        sharedNotebook.setId(i);
        sharedNotebook.setUserId(i);
        sharedNotebook.setNotebookGuid(notebook.guid());

        sharedNotebook.setEmail(
            QStringLiteral("Fake shared notebook email #") +
            QString::number(i));

        sharedNotebook.setServiceCreated(i + 1);
        sharedNotebook.setServiceUpdated(i + 1);

        sharedNotebook.setGlobalId(
            QStringLiteral("Fake shared notebook global id #") +
            QString::number(i));

        sharedNotebook.setUsername(
            QStringLiteral("Fake shared notebook username #") +
            QString::number(i));

        sharedNotebook.setPrivilege(
            qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

        sharedNotebook.setRecipientSettings(
            qevercloud::SharedNotebookRecipientSettings{});

        sharedNotebook.mutableRecipientSettings()->setReminderNotifyEmail(true);

        sharedNotebook.mutableRecipientSettings()->setReminderNotifyInApp(
            false);

        *notebook.mutableSharedNotebooks() << sharedNotebook;
    }

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    QList<qevercloud::SharedNotebook> foundSharedNotebooks =
        localStorageManager.listAllSharedNotebooks(errorMessage);

    QVERIFY2(
        !foundSharedNotebooks.isEmpty(),
        qPrintable(errorMessage.nonLocalizedString()));

    int numFoundSharedNotebooks = foundSharedNotebooks.size();
    if (numFoundSharedNotebooks != numSharedNotebooks) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of shared notebooks "
                           "in the result of LocalStorageManager::"
                           "ListAllSharedNotebooks (") +
            QString::number(numFoundSharedNotebooks) +
            QStringLiteral(") does not match the original number "
                           "of added shared notebooks (") +
            QString::number(numSharedNotebooks) + QStringLiteral(")")));
    }

    for (int i = 0; i < numFoundSharedNotebooks; ++i) {
        const auto & foundSharedNotebook = foundSharedNotebooks.at(i);
        if (!sharedNotebooks.contains(foundSharedNotebook)) {
            QFAIL(
                "One of shared notebooks from the result of "
                "LocalStorageManager::ListAllSharedNotebooks "
                "was not found in the list of original shared notebooks");
        }
    }
}

void TestListAllTagsPerNote()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setParentLocalId(notebook.localId());

    res = localStorageManager.addNote(note, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    int numTags = 5;
    QList<qevercloud::Tag> tags;
    tags.reserve(numTags);
    for (int i = 0; i < numTags; ++i) {
        tags << qevercloud::Tag();
        auto & tag = tags.back();

        tag.setGuid(
            QStringLiteral("00000000-0000-0000-c000-00000000000") +
            QString::number(i + 1));

        tag.setUpdateSequenceNum(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 1) {
            tag.setLocallyModified(true);
        }
        else {
            tag.setLocallyModified(false);
        }

        res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        addNoteTagGuid(tag.guid().value(), note);
        addNoteTagLocalId(tag.localId(), note);

        LocalStorageManager::UpdateNoteOptions updateNoteOptions(
            LocalStorageManager::UpdateNoteOption::UpdateTags);

        res = localStorageManager.updateNote(
            note, updateNoteOptions, errorMessage);

        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    qevercloud::Tag tagNotLinkedWithNote;

    tagNotLinkedWithNote.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000045"));

    tagNotLinkedWithNote.setUpdateSequenceNum(9);
    tagNotLinkedWithNote.setName(QStringLiteral("Tag not linked with note"));

    res = localStorageManager.addTag(tagNotLinkedWithNote, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    // 1) Test method listing all tags per given note without any additional
    // conditions

    errorMessage.clear();

    QList<qevercloud::Tag> foundTags =
        localStorageManager.listAllTagsPerNote(note, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    int numFoundTags = foundTags.size();
    if (numFoundTags != numTags) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of tags in the result "
                           "of LocalStorageManager::ListAllTagsPerNote (") +
            QString::number(numFoundTags) +
            QStringLiteral(") does not match the original number "
                           "of added tags (") +
            QString::number(numTags) + QStringLiteral(")")));
    }

    for (int i = 0; i < numFoundTags; ++i) {
        const auto & foundTag = foundTags.at(i);
        if (!tags.contains(foundTag)) {
            QFAIL(
                "One of tags from the result of "
                "LocalStorageManager::ListAllTagsPerNote "
                "was not found in the list of original tags");
        }
    }

    if (foundTags.contains(tagNotLinkedWithNote)) {
        QFAIL(
            "Found tag not linked with testing note in the result of "
            "LocalStorageManager::ListAllTagsPerNote");
    }

    // 2) Test method listing all tags per note consideting only dirty ones +
    // with limit, offset, specific order and order direction

    errorMessage.clear();
    const size_t limit = 2;
    const size_t offset = 1;

    const LocalStorageManager::ListObjectsOptions flag =
        LocalStorageManager::ListObjectsOption::ListDirty;

    const LocalStorageManager::ListTagsOrder order =
        LocalStorageManager::ListTagsOrder::ByUpdateSequenceNumber;

    const LocalStorageManager::OrderDirection orderDirection =
        LocalStorageManager::OrderDirection::Descending;

    foundTags = localStorageManager.listAllTagsPerNote(
        note, errorMessage, flag, limit, offset, order, orderDirection);

    if (foundTags.size() != static_cast<int>(limit)) {
        QFAIL(qPrintable(
            QStringLiteral("Found unexpected amount of tags: "
                           "expected to find ") +
            QString::number(limit) + QStringLiteral(" tags, found ") +
            QString::number(foundTags.size()) + QStringLiteral(" tags")));
    }

    const auto & firstTag = foundTags[0];
    const auto & secondTag = foundTags[1];

    if (!firstTag.updateSequenceNum()) {
        QFAIL(
            "First of found tags doesn't have the update sequence number "
            "set");
    }

    if (!secondTag.updateSequenceNum()) {
        QFAIL(
            "Second of found tags doesn't have the update sequence number "
            "set");
    }

    if ((firstTag.updateSequenceNum().value() != 3) ||
        (secondTag.updateSequenceNum().value() != 2))
    {
        QFAIL(qPrintable(
            QStringLiteral("Unexpected order of found tags by "
                           "update sequence number: first tag: ") +
            firstTag.toString() + QStringLiteral("\nSecond tag: ") +
            secondTag.toString()));
    }
}

void TestListNotes()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Notebook secondNotebook;

    secondNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000048"));

    secondNotebook.setUpdateSequenceNum(1);
    secondNotebook.setName(QStringLiteral("Fake second notebook name"));
    secondNotebook.setServiceCreated(1);
    secondNotebook.setServiceUpdated(1);

    res = localStorageManager.addNotebook(secondNotebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Notebook notebookNotLinkedWithNotes;
    notebookNotLinkedWithNotes.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000049"));
    notebookNotLinkedWithNotes.setUpdateSequenceNum(1);
    notebookNotLinkedWithNotes.setName(
        QStringLiteral("Fake notebook not linked with notes name name"));
    notebookNotLinkedWithNotes.setServiceCreated(1);
    notebookNotLinkedWithNotes.setServiceUpdated(1);

    res = localStorageManager.addNotebook(
        notebookNotLinkedWithNotes, errorMessage);

    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Tag firstTestTag;
    firstTestTag.setName(QStringLiteral("My first test tag"));
    res = localStorageManager.addTag(firstTestTag, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Tag secondTestTag;
    secondTestTag.setName(QStringLiteral("My second test tag"));
    res = localStorageManager.addTag(secondTestTag, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    qevercloud::Tag thirdTestTag;
    thirdTestTag.setName(QStringLiteral("My third test tag"));
    res = localStorageManager.addTag(thirdTestTag, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    int numNotes = 5;
    QList<qevercloud::Note> notes;
    notes.reserve(numNotes);
    for (int i = 0; i < numNotes; ++i) {
        notes << qevercloud::Note();
        auto & note = notes.back();

        if (i > 1) {
            note.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        if (i > 2) {
            note.setLocallyModified(true);
        }
        else {
            note.setLocallyModified(false);
        }

        if (i < 3) {
            note.setLocalOnly(true);
        }
        else {
            note.setLocalOnly(false);
        }

        if ((i == 0) || (i == 4)) {
            note.setLocallyFavorited(true);
        }
        else {
            note.setLocallyFavorited(false);
        }

        if ((i == 1) || (i == 2) || (i == 4)) {
            addNoteTagLocalId(firstTestTag.localId(), note);
        }
        else if (i == 3) {
            addNoteTagLocalId(secondTestTag.localId(), note);
        }

        note.setUpdated(i + 1);
        note.setTitle(QStringLiteral("Fake note title #") + QString::number(i));

        note.setContent(
            QStringLiteral("<en-note><h1>Hello, world #") + QString::number(i) +
            QStringLiteral("</h1></en-note>"));

        note.setCreated(i + 1);
        note.setUpdated(i + 1);
        note.setActive(true);

        if (i != 3) {
            note.setNotebookGuid(notebook.guid());
            note.setParentLocalId(notebook.localId());
        }
        else {
            note.setNotebookGuid(secondNotebook.guid());
            note.setParentLocalId(secondNotebook.localId());
        }

        res = localStorageManager.addNote(note, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all notes per notebook

    errorMessage.clear();
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    LocalStorageManager::GetNoteOptions getNoteOptions;
#else
    LocalStorageManager::GetNoteOptions getNoteOptions = 0;
#endif

    QList<qevercloud::Note> foundNotes =
        localStorageManager.listNotesPerNotebook(
            notebook, getNoteOptions, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    int numFoundNotes = foundNotes.size();
    if (numFoundNotes != (numNotes - 1)) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of notes in the result "
                           "of LocalStorageManager::ListNotesPerNotebook (") +
            QString::number(numFoundNotes) +
            QStringLiteral(") does not match the original number "
                           "of added notes (") +
            QString::number(numNotes) + QStringLiteral(")")));
    }

    for (int i = 0; i < numNotes; ++i) {
        if (i == 3) {
            continue;
        }

        const auto & note = notes[i];
        if (!foundNotes.contains(note)) {
            QFAIL(
                "One of original notes was not found in the result of "
                "LocalStorageManager::ListNotesPerNotebook ");
        }
    }

    // 2) Ensure the method listing notes per notebook returns zero properly

    errorMessage.clear();

    foundNotes = localStorageManager.listNotesPerNotebook(
        notebookNotLinkedWithNotes, getNoteOptions, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    if (foundNotes.size() != 0) {
        QFAIL(qPrintable(
            QStringLiteral("Found non-zero number of notes in "
                           "the result of LocalStorageManager::"
                           "ListNotesPerNotebook "
                           "called with guid of notebook not "
                           "containing any notes (found ") +
            QString::number(foundNotes.size()) + QStringLiteral(" notes)")));
    }

    // 3) Test method listing notes per notebook considering only the notes
    // with guid + with limit, specific order and order direction

    errorMessage.clear();
    size_t limit = 2;
    size_t offset = 0;

    LocalStorageManager::ListNotesOrder order =
        LocalStorageManager::ListNotesOrder::ByUpdateSequenceNumber;

    LocalStorageManager::OrderDirection orderDirection =
        LocalStorageManager::OrderDirection::Descending;

    getNoteOptions = LocalStorageManager::GetNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    foundNotes = localStorageManager.listNotesPerNotebook(
        notebook, getNoteOptions, errorMessage,
        LocalStorageManager::ListObjectsOption::ListElementsWithGuid, limit,
        offset, order, orderDirection);

    if (foundNotes.size() != static_cast<int>(limit)) {
        QFAIL(qPrintable(
            QStringLiteral("Found unexpected amount of notes: "
                           "expected to find ") +
            QString::number(limit) + QStringLiteral(" notes, found ") +
            QString::number(foundNotes.size()) + QStringLiteral(" notes")));
    }

    const auto & firstNote = foundNotes[0];
    const auto & secondNote = foundNotes[1];

    if (!firstNote.updateSequenceNum()) {
        QFAIL(
            "First of found notes doesn't have the update sequence number "
            "set");
    }

    if (!secondNote.updateSequenceNum()) {
        QFAIL(
            "Second of found notes doesn't have the update sequence number "
            "set");
    }

    if ((firstNote.updateSequenceNum().value() != 5) ||
        (secondNote.updateSequenceNum().value() != 3))
    {
        QFAIL(qPrintable(
            QStringLiteral("Unexpected order of found notes by "
                           "update sequence number: first note: ") +
            firstNote.toString() + QStringLiteral("\nSecond note: ") +
            secondNote.toString()));
    }

    // 4) Test method listing notes per tag considering only the notes
    // with guid + with limit, specific order and order direction

    errorMessage.clear();
    limit = 2;
    offset = 0;

    foundNotes = localStorageManager.listNotesPerTag(
        firstTestTag, getNoteOptions, errorMessage,
        LocalStorageManager::ListObjectsOption::ListElementsWithGuid, limit,
        offset, order, orderDirection);

    if (foundNotes.size() != static_cast<int>(limit)) {
        QFAIL(qPrintable(
            QStringLiteral("Found unexpected amount of notes: "
                           "expected to find ") +
            QString::number(limit) + QStringLiteral(" notes, found ") +
            QString::number(foundNotes.size()) + QStringLiteral(" notes")));
    }

    const auto & firstNotePerTag = foundNotes[0];
    const auto & secondNotePerTag = foundNotes[1];

    if (!firstNotePerTag.updateSequenceNum()) {
        QFAIL(
            "First of found notes doesn't have the update sequence number "
            "set");
    }

    if (!secondNotePerTag.updateSequenceNum()) {
        QFAIL(
            "Second of found notes doesn't have the update sequence number "
            "set");
    }

    if (*firstNotePerTag.updateSequenceNum() <
        *secondNotePerTag.updateSequenceNum())
    {
        QFAIL(
            "Incorrect sorting of found notes, expected descending sorting "
            "by update sequence number");
    }

    if ((firstNotePerTag != notes[4]) && (secondNotePerTag != notes[2])) {
        QFAIL("Found unexpected notes per tag");
    }

    // 5) Test method listing all notes
    errorMessage.clear();

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    getNoteOptions = LocalStorageManager::GetNoteOptions();
#else
    getNoteOptions = LocalStorageManager::GetNoteOptions(0);
#endif

    foundNotes = localStorageManager.listNotes(
        LocalStorageManager::ListObjectsOption::ListAll, getNoteOptions,
        errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    numFoundNotes = foundNotes.size();
    if (numFoundNotes != numNotes) {
        QFAIL(qPrintable(
            QStringLiteral("Error number of notes in the result "
                           "of LocalStorageManager::ListNotes "
                           "with flag ListAll (") +
            QString::number(numFoundNotes) +
            QStringLiteral(") does not match the original number "
                           "of added notes (") +
            QString::number(numNotes) + QStringLiteral(")")));
    }

    for (int i = 0; i < numFoundNotes; ++i) {
        const auto & foundNote = foundNotes[i];
        if (!notes.contains(foundNote)) {
            QFAIL(
                "One of notes from the result of "
                "LocalStorageManager::ListNotes with flag ListAll was not "
                "found in the list of original notes");
        }
    }

#define CHECK_LIST_NOTES(flag, name, true_cond, false_cond)                    \
    errorMessage.clear();                                                      \
    foundNotes =                                                               \
        localStorageManager.listNotes(flag, getNoteOptions, errorMessage);     \
    QVERIFY2(                                                                  \
        errorMessage.isEmpty(),                                                \
        qPrintable(errorMessage.nonLocalizedString()));                        \
    for (int i = 0; i < numNotes; ++i) {                                       \
        const auto & note = notes[i];                                          \
        bool res = foundNotes.contains(note);                                  \
        if ((true_cond) && !res) {                                             \
            QNWARNING("tests:local_storage", "Not found note: " << note);      \
            QFAIL("One of " name                                               \
                  " notes was not found by "                                   \
                  "LocalStorageManager::ListNotes");                           \
        }                                                                      \
        else if ((false_cond) && res) {                                        \
            QNWARNING(                                                         \
                "tests:local_storage", "Found irrelevant note: " << note);     \
            QFAIL("LocalStorageManager::ListNotes with flag " name             \
                  " returned incorrect note");                                 \
        }                                                                      \
    }

    using ListObjectsOption = LocalStorageManager::ListObjectsOption;

    // 6) Test method listing only dirty notes
    CHECK_LIST_NOTES(ListObjectsOption::ListDirty, "dirty", i > 2, i <= 2);

    // 7) Test method listing only local notes
    CHECK_LIST_NOTES(ListObjectsOption::ListLocal, "local", i < 3, i >= 3);

    // 8) Test method listing only notes without guid
    CHECK_LIST_NOTES(
        ListObjectsOption::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

    // 9) Test method listing only favorited notes
    CHECK_LIST_NOTES(
        ListObjectsOption::ListFavoritedElements, "favorited",
        (i == 0) || (i == 4), (i != 0) && (i != 4));

    // 10) Test method listing dirty favorited notes with guid
    CHECK_LIST_NOTES(
        ListObjectsOption::ListDirty | ListObjectsOption::ListElementsWithGuid |
            ListObjectsOption::ListFavoritedElements,
        "dirty, favorited, having guid", i == 4, i != 4);

    // 11) Test method listing local favorited notes
    CHECK_LIST_NOTES(
        ListObjectsOption::ListLocal | ListObjectsOption::ListFavoritedElements,
        "local, favorited", i == 0, i != 0);

    // 12) Test method listing notes per notebook and tag local ids
    // using notebook local ids only as a filter
    QStringList notebookLocalIds = QStringList() << notebook.localId();
    QStringList tagLocalIds;

    foundNotes = localStorageManager.listNotesPerNotebooksAndTags(
        notebookLocalIds, tagLocalIds, getNoteOptions, errorMessage,
        LocalStorageManager::ListObjectsOption::ListAll);

    numFoundNotes = foundNotes.size();
    if (numFoundNotes != (numNotes - 1)) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of notes in the result "
                           "of LocalStorageManager::"
                           "ListNotesPerNotebooksAndTags (") +
            QString::number(numFoundNotes) +
            QStringLiteral(") does not match the original number "
                           "of added notes (") +
            QString::number(numNotes) + QStringLiteral(") ") +
            QStringLiteral("when only notebooks are present "
                           "within the filter")));
    }

    for (int i = 0; i < numNotes; ++i) {
        if (i == 3) {
            continue;
        }

        const auto & note = notes[i];
        if (!foundNotes.contains(note)) {
            QFAIL(
                "One of original notes was not found in the result of "
                "LocalStorageManager::ListNotesPerNotebooksAndTags "
                "when only notebooks are present within the filter");
        }
    }

    // 13) Test method listing notes per notebook and tag local uids
    // using tag local uids only as a filter
    notebookLocalIds.clear();
    tagLocalIds << firstTestTag.localId();

    foundNotes = localStorageManager.listNotesPerNotebooksAndTags(
        notebookLocalIds, tagLocalIds, getNoteOptions, errorMessage,
        LocalStorageManager::ListObjectsOption::ListAll);

    numFoundNotes = foundNotes.size();
    if (numFoundNotes != 3) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of notes in the result "
                           "of LocalStorageManager::"
                           "ListNotesPerNotebooksAndTags (") +
            QString::number(numFoundNotes) +
            QStringLiteral(") does not match the original number "
                           "of added notes per tag (") +
            QString::number(3) + QStringLiteral(") ") +
            QStringLiteral("when only tags are present within the filter")));
    }

    for (int i = 0; i < numNotes; ++i) {
        if ((i != 1) && (i != 2) && (i != 4)) {
            continue;
        }

        const auto & note = notes[i];
        if (!foundNotes.contains(note)) {
            QFAIL(
                "One of original notes was not found in the result of "
                "LocalStorageManager::ListNotesPerNotebooksAndTags "
                "when only tags are present within the filter");
        }
    }

    // 14) Test method listing notes per notebook and tag local ids
    // using notebook local ids and tag local ids as a filter
    notebookLocalIds << secondNotebook.localId();
    tagLocalIds.clear();
    tagLocalIds << secondTestTag.localId();

    foundNotes = localStorageManager.listNotesPerNotebooksAndTags(
        notebookLocalIds, tagLocalIds, getNoteOptions, errorMessage,
        LocalStorageManager::ListObjectsOption::ListAll);

    numFoundNotes = foundNotes.size();
    if (numFoundNotes != 1) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of notes in the result "
                           "of LocalStorageManager::"
                           "ListNotesPerNotebooksAndTags (") +
            QString::number(numFoundNotes) +
            QStringLiteral(") does not match the original number "
                           "of added notes per tag (") +
            QString::number(1) + QStringLiteral(") ") +
            QStringLiteral("when both notebooks and tags are present "
                           "within the filter")));
    }

    if (foundNotes[0] != notes[3]) {
        QFAIL(
            "The note found in the result of "
            "LocalStorageManager::ListNotesPerNotebooksAndTags "
            "when both notebooks and tags are present within the filter "
            "doesn't match the original note");
    }

    // 15) Test method listing notes by note local uids
    QStringList noteLocalIds;
    noteLocalIds.reserve(3);
    for (int i = 0, size = noteLocalIds.size(); i < size; ++i) {
        noteLocalIds << notes[i].localId();
    }

    errorMessage.clear();

    foundNotes = localStorageManager.listNotesByLocalIds(
        noteLocalIds, getNoteOptions, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        foundNotes.size() == noteLocalIds.size(),
        qPrintable(QString::fromUtf8("Unexpected number of notes found by "
                                     "method listing notes by local uids: "
                                     "expected %1 notes, got %2")
                       .arg(noteLocalIds.size(), foundNotes.size())));

    for (auto it = foundNotes.constBegin(), end = foundNotes.constEnd();
         it != end; ++it)
    {
        QVERIFY2(
            noteLocalIds.contains(it->localId()),
            "Detected note returned by method listing notes by local uids "
            "which local uid is not present within the original list of "
            "local uids");
    }

    // 16) Test method listing notes by note local ids when the list of note
    // local ids contains ids not corresponding to existing notes
    int originalNoteLocalIdsSize = noteLocalIds.size();
    noteLocalIds << UidGenerator::Generate();
    noteLocalIds << UidGenerator::Generate();

    errorMessage.clear();
    foundNotes = localStorageManager.listNotesByLocalIds(
        noteLocalIds, getNoteOptions, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        foundNotes.size() == originalNoteLocalIdsSize,
        qPrintable(QString::fromUtf8("Unexpected number of notes found by "
                                     "method listing notes by local ids: "
                                     "expected %1 notes, got %2")
                       .arg(originalNoteLocalIdsSize, foundNotes.size())));

    for (auto it = foundNotes.constBegin(), end = foundNotes.constEnd();
         it != end; ++it)
    {
        QVERIFY2(
            noteLocalIds.contains(it->localId()),
            "Detected note returned by method listing notes by local ids "
            "which local id is not present within the original list of "
            "local ids");
    }
}

void TestListNotebooks()
{
    Account account{QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int numNotebooks = 5;
    QList<qevercloud::Notebook> notebooks;
    notebooks.reserve(numNotebooks);
    for (int i = 0; i < numNotebooks; ++i) {
        notebooks << qevercloud::Notebook();
        auto & notebook = notebooks.back();

        if (i > 1) {
            notebook.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        notebook.setUpdateSequenceNum(i + 1);

        notebook.setName(
            QStringLiteral("Fake notebook name #") + QString::number(i + 1));

        notebook.setServiceCreated(i + 1);
        notebook.setServiceUpdated(i + 1);

        notebook.setDefaultNotebook(false);
        notebook.mutableLocalData()[QStringLiteral("isLastUsed")] = false;

        notebook.setPublishing(qevercloud::Publishing{});
        notebook.mutablePublishing()->setUri(
            QStringLiteral("Fake publishing uri #") + QString::number(i + 1));

        notebook.mutablePublishing()->setOrder(
            qevercloud::NoteSortOrder::CREATED);

        notebook.mutablePublishing()->setAscending(true);

        notebook.mutablePublishing()->setPublicDescription(
            QStringLiteral("Fake public description"));

        notebook.setPublished(true);
        notebook.setStack(QStringLiteral("Fake notebook stack"));

        notebook.setBusinessNotebook(qevercloud::BusinessNotebook{});
        notebook.mutableBusinessNotebook()->setNotebookDescription(
            QStringLiteral("Fake business notebook description"));

        notebook.mutableBusinessNotebook()->setPrivilege(
            qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

        notebook.mutableBusinessNotebook()->setRecommended(true);

        // NotebookRestrictions
        notebook.setRestrictions(qevercloud::NotebookRestrictions{});
        auto & notebookRestrictions = *notebook.mutableRestrictions();
        notebookRestrictions.setNoReadNotes(false);
        notebookRestrictions.setNoCreateNotes(false);
        notebookRestrictions.setNoUpdateNotes(false);
        notebookRestrictions.setNoExpungeNotes(true);
        notebookRestrictions.setNoShareNotes(false);
        notebookRestrictions.setNoEmailNotes(false);
        notebookRestrictions.setNoSendMessageToRecipients(false);
        notebookRestrictions.setNoUpdateNotebook(false);
        notebookRestrictions.setNoExpungeNotebook(true);
        notebookRestrictions.setNoSetDefaultNotebook(false);
        notebookRestrictions.setNoSetNotebookStack(false);
        notebookRestrictions.setNoPublishToPublic(false);
        notebookRestrictions.setNoPublishToBusinessLibrary(true);
        notebookRestrictions.setNoCreateTags(false);
        notebookRestrictions.setNoUpdateTags(false);
        notebookRestrictions.setNoExpungeTags(true);
        notebookRestrictions.setNoSetParentTag(false);
        notebookRestrictions.setNoCreateSharedNotebooks(false);

        notebookRestrictions.setUpdateWhichSharedNotebookRestrictions(
            qevercloud::SharedNotebookInstanceRestrictions::ASSIGNED);

        notebookRestrictions.setExpungeWhichSharedNotebookRestrictions(
            qevercloud::SharedNotebookInstanceRestrictions::NO_SHARED_NOTEBOOKS);

        if (i > 2) {
            notebook.setLocallyModified(true);
        }
        else {
            notebook.setLocallyModified(false);
        }

        if (i < 3) {
            notebook.setLocalOnly(true);
        }
        else {
            notebook.setLocalOnly(false);
        }

        if ((i == 0) || (i == 4)) {
            notebook.setLocallyFavorited(true);
        }
        else {
            notebook.setLocallyFavorited(false);
        }

        if (i > 1) {
            qevercloud::SharedNotebook sharedNotebook;
            sharedNotebook.setId(i + 1);
            sharedNotebook.setUserId(i + 1);
            sharedNotebook.setNotebookGuid(notebook.guid());

            sharedNotebook.setEmail(
                QStringLiteral("Fake shared notebook email #") +
                QString::number(i + 1));

            sharedNotebook.setServiceCreated(i + 1);
            sharedNotebook.setServiceUpdated(i + 1);

            sharedNotebook.setGlobalId(
                QStringLiteral("Fake shared notebook global id #") +
                QString::number(i + 1));

            sharedNotebook.setUsername(
                QStringLiteral("Fake shared notebook username #") +
                QString::number(i + 1));

            sharedNotebook.setPrivilege(
                qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

            sharedNotebook.setRecipientSettings(
                qevercloud::SharedNotebookRecipientSettings{});

            sharedNotebook.mutableRecipientSettings()->setReminderNotifyEmail(
                true);

            sharedNotebook.mutableRecipientSettings()->setReminderNotifyInApp(
                false);

            notebook.setSharedNotebooks(
                QList<qevercloud::SharedNotebook>() << sharedNotebook);
        }

        bool res = localStorageManager.addNotebook(notebook, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all notebooks

    QList<qevercloud::Notebook> foundNotebooks =
        localStorageManager.listAllNotebooks(errorMessage);

    QVERIFY2(
        !foundNotebooks.isEmpty(),
        qPrintable(errorMessage.nonLocalizedString()));

    int numFoundNotebooks = foundNotebooks.size();
    if (numFoundNotebooks != numNotebooks) {
        QFAIL(qPrintable(
            QStringLiteral("Error: number of notebooks in the result "
                           "of LocalStorageManager::ListAllNotebooks (") +
            QString::number(numFoundNotebooks) +
            QStringLiteral(") does not match the original number "
                           "of added notebooks (") +
            QString::number(numNotebooks) + QStringLiteral(")")));
    }

    for (int i = 0; i < numFoundNotebooks; ++i) {
        const auto & foundNotebook = foundNotebooks.at(i);
        if (!notebooks.contains(foundNotebook)) {
            QFAIL(
                "One of notebooks from the result of LocalStorageManager::"
                "ListAllNotebooks was not found in the list of original "
                "notebooks");
        }
    }

#define CHECK_LIST_NOTEBOOKS(flag, name, true_cond, false_cond)                \
    errorMessage.clear();                                                      \
    foundNotebooks = localStorageManager.listNotebooks(flag, errorMessage);    \
    QVERIFY2(                                                                  \
        errorMessage.isEmpty(),                                                \
        qPrintable(errorMessage.nonLocalizedString()));                        \
    for (int i = 0; i < numNotebooks; ++i) {                                   \
        const auto & notebook = notebooks.at(i);                               \
        bool res = foundNotebooks.contains(notebook);                          \
        if ((true_cond) && !res) {                                             \
            QNWARNING(                                                         \
                "tests:local_storage", "Not found notebook: " << notebook);    \
            QFAIL("One of " name                                               \
                  " notebooks was not found by "                               \
                  "LocalStorageManager::ListNotebooks");                       \
        }                                                                      \
        else if ((false_cond) && res) {                                        \
            QNWARNING(                                                         \
                "tests:local_storage",                                         \
                "Found irrelevant notebook: " << notebook);                    \
            QFAIL("LocalStorageManager::ListNotebooks with flag " name         \
                  " returned incorrect notebook");                             \
        }                                                                      \
    }

    using ListObjectsOption = LocalStorageManager::ListObjectsOption;

    // 2) Test method listing only dirty notebooks
    CHECK_LIST_NOTEBOOKS(ListObjectsOption::ListDirty, "dirty", i > 2, i <= 2);

    // 3) Test method listing only local notebooks
    CHECK_LIST_NOTEBOOKS(ListObjectsOption::ListLocal, "local", i < 3, i >= 3);

    // 4) Test method listing only notebooks without guid
    CHECK_LIST_NOTEBOOKS(
        ListObjectsOption::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

    // 5) Test method listing only favorited notebooks
    CHECK_LIST_NOTEBOOKS(
        ListObjectsOption::ListFavoritedElements, "favorited",
        (i == 0) || (i == 4), (i != 0) && (i != 4));

    // 6) Test method listing dirty favorited notebooks with guid
    CHECK_LIST_NOTEBOOKS(
        ListObjectsOption::ListDirty | ListObjectsOption::ListElementsWithGuid |
            ListObjectsOption::ListFavoritedElements,
        "dirty, favorited, having guid", i == 4, i != 4);

    // 7) Test method listing local favorited notebooks
    CHECK_LIST_NOTEBOOKS(
        ListObjectsOption::ListLocal | ListObjectsOption::ListFavoritedElements,
        "local, favorited", i == 0, i != 0);
}

void TestExpungeNotelessTagsFromLinkedNotebooks()
{
    const Account account{
        QStringLiteral("CoreTesterFakeUser"), Account::Type::Local};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    qevercloud::LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000001"));

    linkedNotebook.setUpdateSequenceNum(1);
    linkedNotebook.setShareName(QStringLiteral("Linked notebook share name"));
    linkedNotebook.setUsername(QStringLiteral("Linked notebook username"));
    linkedNotebook.setShardId(QStringLiteral("Linked notebook shard id"));

    linkedNotebook.setSharedNotebookGlobalId(
        QStringLiteral("Linked notebook shared notebook global id"));

    linkedNotebook.setUri(QStringLiteral("Linked notebook uri"));

    linkedNotebook.setNoteStoreUrl(
        QStringLiteral("Linked notebook note store url"));

    linkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Linked notebook web api url prefix"));

    linkedNotebook.setStack(QStringLiteral("Linked notebook stack"));
    linkedNotebook.setBusinessId(1);

    qevercloud::Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    setItemLinkedNotebookGuid(linkedNotebook.guid().value(), notebook);
    notebook.setUpdateSequenceNum(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setServiceCreated(1);
    notebook.setServiceUpdated(1);

    qevercloud::Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setParentLocalId(notebook.localId());

    ErrorString errorMessage;
    bool res =
        localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);

    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();
    res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();
    res = localStorageManager.addNote(note, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    int nTags = 5;
    QList<qevercloud::Tag> tags;
    tags.reserve(nTags);
    for (int i = 0; i < nTags; ++i) {
        tags.push_back(qevercloud::Tag());
        auto & tag = tags.back();

        tag.setGuid(
            QStringLiteral("00000000-0000-0000-c000-00000000000") +
            QString::number(i + 1));

        tag.setUpdateSequenceNum(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 2) {
            setItemLinkedNotebookGuid(linkedNotebook.guid().value(), tag);
        }

        errorMessage.clear();
        res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        errorMessage.clear();

        addNoteTagGuid(tag.guid().value(), note);
        addNoteTagLocalId(tag.localId(), note);

        LocalStorageManager::UpdateNoteOptions updateNoteOptions(
            LocalStorageManager::UpdateNoteOption::UpdateTags);

        res = localStorageManager.updateNote(
            note, updateNoteOptions, errorMessage);

        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    errorMessage.clear();
    res = localStorageManager.expungeNote(note, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    errorMessage.clear();

    res = localStorageManager.expungeNotelessTagsFromLinkedNotebooks(
        errorMessage);

    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    QList<qevercloud::Tag> foundTags;
    foundTags.reserve(3);
    errorMessage.clear();
    foundTags = localStorageManager.listAllTags(errorMessage);
    if (foundTags.isEmpty() && !errorMessage.isEmpty()) {
        QFAIL(qPrintable(errorMessage.nonLocalizedString()));
    }

    for (int i = 0; i < nTags; ++i) {
        const auto & tag = tags[i];

        if ((i > 2) && foundTags.contains(tag)) {
            errorMessage.setBase(
                "Found tag from linked notebook which "
                "should have been expunged");

            QNWARNING("tests:local_storage", errorMessage);
            QFAIL(qPrintable(errorMessage.nonLocalizedString()));
        }
        else if ((i <= 2) && !foundTags.contains(tag)) {
            errorMessage.setBase(
                "Could not find tag which should have "
                "remained in the local storage");

            QNWARNING("tests:local_storage", errorMessage);
            QFAIL(qPrintable(errorMessage.nonLocalizedString()));
        }
    }
}

} // namespace test
} // namespace quentier
