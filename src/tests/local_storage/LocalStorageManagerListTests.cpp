/*
 * Copyright 2019-2020 Dmitry Ivanov
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
#include <quentier/types/LinkedNotebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/SharedNotebook.h>
#include <quentier/types/Tag.h>

#include <QtTest/QtTest>

namespace quentier {
namespace test {

void TestListSavedSearches()
{
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nSearches = 5;
    QList<SavedSearch> searches;
    searches.reserve(nSearches);
    for (int i = 0; i < nSearches; ++i) {
        searches << SavedSearch();
        SavedSearch & search = searches.back();

        if (i > 1) {
            search.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        search.setUpdateSequenceNumber(i);

        search.setName(QStringLiteral("SavedSearch #") + QString::number(i));

        search.setQuery(
            QStringLiteral("Fake saved search query #") + QString::number(i));

        search.setQueryFormat(1);
        search.setIncludeAccount(true);
        search.setIncludeBusinessLinkedNotebooks(true);
        search.setIncludePersonalLinkedNotebooks(true);

        if (i > 2) {
            search.setDirty(true);
        }
        else {
            search.setDirty(false);
        }

        if (i < 3) {
            search.setLocal(true);
        }
        else {
            search.setLocal(false);
        }

        if ((i == 0) || (i == 4)) {
            search.setFavorited(true);
        }
        else {
            search.setFavorited(false);
        }

        bool res = localStorageManager.addSavedSearch(search, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all saved searches

    errorMessage.clear();

    QList<SavedSearch> foundSearches =
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
        const SavedSearch & foundSearch = foundSearches.at(i);
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
        const SavedSearch & search = searches.at(i);                           \
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

    const SavedSearch & firstSearch = foundSearches[0];
    const SavedSearch & secondSearch = foundSearches[1];

    if (!firstSearch.hasUpdateSequenceNumber() ||
        !secondSearch.hasUpdateSequenceNumber())
    {
        QFAIL(qPrintable(
            QStringLiteral("One of found saved searches doesn't "
                           "have the update sequence number "
                           "which is unexpected: first search: ") +
            firstSearch.toString() + QStringLiteral("\nSecond search: ") +
            secondSearch.toString()));
    }

    if (firstSearch.updateSequenceNumber() != 3) {
        QFAIL(qPrintable(
            QStringLiteral("First saved search was expected to "
                           "have update sequence number of 3, "
                           "instead it is ") +
            QString::number(firstSearch.updateSequenceNumber())));
    }

    if (secondSearch.updateSequenceNumber() != 4) {
        QFAIL(qPrintable(
            QStringLiteral("Second saved search was expected "
                           "to have update sequence number of 4, "
                           "instead it is ") +
            QString::number(secondSearch.updateSequenceNumber())));
    }
}

void TestListLinkedNotebooks()
{
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nLinkedNotebooks = 5;
    QList<LinkedNotebook> linkedNotebooks;
    linkedNotebooks.reserve(nLinkedNotebooks);
    for (int i = 0; i < nLinkedNotebooks; ++i) {
        linkedNotebooks << LinkedNotebook();
        LinkedNotebook & linkedNotebook = linkedNotebooks.back();

        linkedNotebook.setGuid(
            QStringLiteral("00000000-0000-0000-c000-00000000000") +
            QString::number(i + 1));

        linkedNotebook.setUpdateSequenceNumber(i);

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
            linkedNotebook.setDirty(true);
        }
        else {
            linkedNotebook.setDirty(false);
        }

        bool res =
            localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);

        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all linked notebooks

    errorMessage.clear();

    QList<LinkedNotebook> foundLinkedNotebooks =
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
        const LinkedNotebook & foundLinkedNotebook = foundLinkedNotebooks.at(i);
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
        const LinkedNotebook & linkedNotebook = linkedNotebooks.at(i);
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
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nTags = 5;
    QVector<Tag> tags;
    tags.reserve(nTags);
    for (int i = 0; i < nTags; ++i) {
        tags.push_back(Tag());
        Tag & tag = tags.back();

        if (i > 1) {
            tag.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        tag.setUpdateSequenceNumber(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 2) {
            tag.setParentGuid(tags.at(i - 1).guid());
        }

        if (i > 2) {
            tag.setDirty(true);
        }
        else {
            tag.setDirty(false);
        }

        if (i < 3) {
            tag.setLocal(true);
        }
        else {
            tag.setLocal(false);
        }

        if ((i == 0) || (i == 4)) {
            tag.setFavorited(true);
        }
        else {
            tag.setFavorited(false);
        }

        bool res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all tags

    errorMessage.clear();
    QList<Tag> foundTags = localStorageManager.listAllTags(errorMessage);

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
        const Tag & foundTag = foundTags.at(i);
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
        const Tag & tag = tags.at(i);                                          \
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
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int nTags = 5;
    QVector<Tag> tags;
    tags.reserve(nTags);
    for (int i = 0; i < nTags; ++i) {
        tags.push_back(Tag());
        Tag & tag = tags.back();

        if (i > 1) {
            tag.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        tag.setUpdateSequenceNumber(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 2) {
            tag.setParentGuid(tags.at(i - 1).guid());
        }

        if (i > 2) {
            tag.setDirty(true);
        }
        else {
            tag.setDirty(false);
        }

        if (i < 3) {
            tag.setLocal(true);
        }
        else {
            tag.setLocal(false);
        }

        if ((i == 0) || (i == 4)) {
            tag.setFavorited(true);
        }
        else {
            tag.setFavorited(false);
        }

        bool res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // Now add some notebooks and notes using the just created tags
    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    errorMessage.clear();
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    QMap<QString, QStringList> noteLocalUidsByTagLocalUid;

    int numNotes = 5;
    QList<Note> notes;
    notes.reserve(numNotes);
    for (int i = 0; i < numNotes; ++i) {
        notes << Note();
        Note & note = notes.back();

        if (i > 1) {
            note.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        if (i > 2) {
            note.setDirty(true);
        }
        else {
            note.setDirty(false);
        }

        if (i < 3) {
            note.setLocal(true);
        }
        else {
            note.setLocal(false);
        }

        if ((i == 0) || (i == 4)) {
            note.setFavorited(true);
        }
        else {
            note.setFavorited(false);
        }

#define APPEND_TAG_TO_NOTE(tagNum)                                             \
    note.addTagLocalUid(tags[tagNum].localUid());                              \
    noteLocalUidsByTagLocalUid[tags[tagNum].localUid()] << note.localUid()

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

        note.setUpdateSequenceNumber(i + 1);
        note.setTitle(QStringLiteral("Fake note title #") + QString::number(i));

        note.setContent(
            QStringLiteral("<en-note><h1>Hello, world #") + QString::number(i) +
            QStringLiteral("</h1></en-note>"));

        note.setCreationTimestamp(i + 1);
        note.setModificationTimestamp(i + 1);
        note.setActive(true);
        note.setNotebookGuid(notebook.guid());
        note.setNotebookLocalUid(notebook.localUid());

        res = localStorageManager.addNote(note, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    QList<std::pair<Tag, QStringList>> foundTagsWithNoteLocalUids;

#define CHECK_LIST_TAGS(flag, name, true_cond, false_cond)                     \
    errorMessage.clear();                                                      \
    foundTagsWithNoteLocalUids =                                               \
        localStorageManager.listTagsWithNoteLocalUids(flag, errorMessage);     \
    QVERIFY2(                                                                  \
        errorMessage.isEmpty(),                                                \
        qPrintable(errorMessage.nonLocalizedString()));                        \
    for (int i = 0; i < nTags; ++i) {                                          \
        const Tag & tag = tags.at(i);                                          \
        int tagIndex = 0;                                                      \
        bool res = false;                                                      \
        for (int size = foundTagsWithNoteLocalUids.size(); tagIndex < size;    \
             ++tagIndex) {                                                     \
            if (foundTagsWithNoteLocalUids[tagIndex].first == tag) {           \
                res = true;                                                    \
                break;                                                         \
            }                                                                  \
        }                                                                      \
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
        else if (res) {                                                        \
            auto noteIt = noteLocalUidsByTagLocalUid.find(tag.localUid());     \
            if (noteIt == noteLocalUidsByTagLocalUid.end() &&                  \
                !foundTagsWithNoteLocalUids[tagIndex].second.isEmpty())        \
            {                                                                  \
                QNWARNING(                                                     \
                    "tests:local_storage",                                     \
                    "Found irrelevant list of "                                \
                        << "note local uids for a tag: "                       \
                        << foundTagsWithNoteLocalUids[tagIndex].second.join(   \
                               QStringLiteral(", ")));                         \
                QFAIL("LocalStorageManager::ListTags with flag " name          \
                      " returned redundant note local uids");                  \
            }                                                                  \
            else if (noteIt != noteLocalUidsByTagLocalUid.end()) {             \
                if (foundTagsWithNoteLocalUids[tagIndex].second.isEmpty()) {   \
                    QNWARNING(                                                 \
                        "tests:local_storage",                                 \
                        "Found empty list of "                                 \
                            << "note local uids for a tag for which they "     \
                            << "were expected: "                               \
                            << noteIt.value().join(QStringLiteral(", ")));     \
                    QFAIL("LocalStorageManager::ListTags with flag " name      \
                          " did not return proper note local uids");           \
                }                                                              \
                else if (                                                      \
                    foundTagsWithNoteLocalUids[tagIndex].second.size() !=      \
                    noteIt.value().size())                                     \
                {                                                              \
                    QNWARNING(                                                 \
                        "tests:local_storage",                                 \
                        "Found list of note "                                  \
                            << "local uids for a tag with incorrect list "     \
                               "size: "                                        \
                            << foundTagsWithNoteLocalUids[tagIndex]            \
                                   .second.join(QStringLiteral(", ")));        \
                    QFAIL("LocalStorageManager::ListTags with flag " name      \
                          " did not return proper number of note local uids"); \
                }                                                              \
                else {                                                         \
                    for (int j = 0; j <                                        \
                         foundTagsWithNoteLocalUids[tagIndex].second.size();   \
                         ++j) {                                                \
                        if (!noteIt.value().contains(                          \
                                foundTagsWithNoteLocalUids[tagIndex]           \
                                    .second[j])) {                             \
                            QNWARNING(                                         \
                                "tests:local_storage",                         \
                                "Found incorrect "                             \
                                    << "list of note local uids for a tag: "   \
                                    << foundTagsWithNoteLocalUids[tagIndex]    \
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
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000000"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);
    notebook.setDefaultNotebook(true);
    notebook.setPublished(false);
    notebook.setStack(QStringLiteral("Fake notebook stack"));

    int numSharedNotebooks = 5;
    QList<SharedNotebook> sharedNotebooks;
    sharedNotebooks.reserve(numSharedNotebooks);
    for (int i = 0; i < numSharedNotebooks; ++i) {
        sharedNotebooks << SharedNotebook();
        SharedNotebook & sharedNotebook = sharedNotebooks.back();

        sharedNotebook.setId(i);
        sharedNotebook.setUserId(i);
        sharedNotebook.setNotebookGuid(notebook.guid());

        sharedNotebook.setEmail(
            QStringLiteral("Fake shared notebook email #") +
            QString::number(i));

        sharedNotebook.setCreationTimestamp(i + 1);
        sharedNotebook.setModificationTimestamp(i + 1);

        sharedNotebook.setGlobalId(
            QStringLiteral("Fake shared notebook global id #") +
            QString::number(i));

        sharedNotebook.setUsername(
            QStringLiteral("Fake shared notebook username #") +
            QString::number(i));

        sharedNotebook.setPrivilegeLevel(1);
        sharedNotebook.setReminderNotifyEmail(true);
        sharedNotebook.setReminderNotifyApp(false);

        notebook.addSharedNotebook(sharedNotebook);
    }

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    QList<SharedNotebook> foundSharedNotebooks =
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
        const SharedNotebook & foundSharedNotebook = foundSharedNotebooks.at(i);
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
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNumber(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreationTimestamp(1);
    note.setModificationTimestamp(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalUid(notebook.localUid());

    res = localStorageManager.addNote(note, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    int numTags = 5;
    QList<Tag> tags;
    tags.reserve(numTags);
    for (int i = 0; i < numTags; ++i) {
        tags << Tag();
        Tag & tag = tags.back();

        tag.setGuid(
            QStringLiteral("00000000-0000-0000-c000-00000000000") +
            QString::number(i + 1));

        tag.setUpdateSequenceNumber(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 1) {
            tag.setDirty(true);
        }
        else {
            tag.setDirty(false);
        }

        res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        note.addTagGuid(tag.guid());
        note.addTagLocalUid(tag.localUid());

        LocalStorageManager::UpdateNoteOptions updateNoteOptions(
            LocalStorageManager::UpdateNoteOption::UpdateTags);

        res = localStorageManager.updateNote(
            note, updateNoteOptions, errorMessage);

        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    Tag tagNotLinkedWithNote;

    tagNotLinkedWithNote.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000045"));

    tagNotLinkedWithNote.setUpdateSequenceNumber(9);
    tagNotLinkedWithNote.setName(QStringLiteral("Tag not linked with note"));

    res = localStorageManager.addTag(tagNotLinkedWithNote, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    // 1) Test method listing all tags per given note without any additional
    // conditions

    errorMessage.clear();

    QList<Tag> foundTags =
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
        const Tag & foundTag = foundTags.at(i);
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

    const Tag & firstTag = foundTags[0];
    const Tag & secondTag = foundTags[1];

    if (!firstTag.hasUpdateSequenceNumber()) {
        QFAIL(
            "First of found tags doesn't have the update sequence number "
            "set");
    }

    if (!secondTag.hasUpdateSequenceNumber()) {
        QFAIL(
            "Second of found tags doesn't have the update sequence number "
            "set");
    }

    if ((firstTag.updateSequenceNumber() != 3) ||
        (secondTag.updateSequenceNumber() != 2))
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
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    ErrorString errorMessage;
    bool res = localStorageManager.addNotebook(notebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    Notebook secondNotebook;

    secondNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000048"));

    secondNotebook.setUpdateSequenceNumber(1);
    secondNotebook.setName(QStringLiteral("Fake second notebook name"));
    secondNotebook.setCreationTimestamp(1);
    secondNotebook.setModificationTimestamp(1);

    res = localStorageManager.addNotebook(secondNotebook, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    Notebook notebookNotLinkedWithNotes;
    notebookNotLinkedWithNotes.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000049"));
    notebookNotLinkedWithNotes.setUpdateSequenceNumber(1);
    notebookNotLinkedWithNotes.setName(
        QStringLiteral("Fake notebook not linked with notes name name"));
    notebookNotLinkedWithNotes.setCreationTimestamp(1);
    notebookNotLinkedWithNotes.setModificationTimestamp(1);

    res = localStorageManager.addNotebook(
        notebookNotLinkedWithNotes, errorMessage);

    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    Tag firstTestTag;
    firstTestTag.setName(QStringLiteral("My first test tag"));
    res = localStorageManager.addTag(firstTestTag, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    Tag secondTestTag;
    secondTestTag.setName(QStringLiteral("My second test tag"));
    res = localStorageManager.addTag(secondTestTag, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    Tag thirdTestTag;
    thirdTestTag.setName(QStringLiteral("My third test tag"));
    res = localStorageManager.addTag(thirdTestTag, errorMessage);
    QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

    int numNotes = 5;
    QList<Note> notes;
    notes.reserve(numNotes);
    for (int i = 0; i < numNotes; ++i) {
        notes << Note();
        Note & note = notes.back();

        if (i > 1) {
            note.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        if (i > 2) {
            note.setDirty(true);
        }
        else {
            note.setDirty(false);
        }

        if (i < 3) {
            note.setLocal(true);
        }
        else {
            note.setLocal(false);
        }

        if ((i == 0) || (i == 4)) {
            note.setFavorited(true);
        }
        else {
            note.setFavorited(false);
        }

        if ((i == 1) || (i == 2) || (i == 4)) {
            note.addTagLocalUid(firstTestTag.localUid());
        }
        else if (i == 3) {
            note.addTagLocalUid(secondTestTag.localUid());
        }

        note.setUpdateSequenceNumber(i + 1);
        note.setTitle(QStringLiteral("Fake note title #") + QString::number(i));

        note.setContent(
            QStringLiteral("<en-note><h1>Hello, world #") + QString::number(i) +
            QStringLiteral("</h1></en-note>"));

        note.setCreationTimestamp(i + 1);
        note.setModificationTimestamp(i + 1);
        note.setActive(true);

        if (i != 3) {
            note.setNotebookGuid(notebook.guid());
            note.setNotebookLocalUid(notebook.localUid());
        }
        else {
            note.setNotebookGuid(secondNotebook.guid());
            note.setNotebookLocalUid(secondNotebook.localUid());
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

    QList<Note> foundNotes = localStorageManager.listNotesPerNotebook(
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

        const Note & note = notes[i];
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

    const Note & firstNote = foundNotes[0];
    const Note & secondNote = foundNotes[1];

    if (!firstNote.hasUpdateSequenceNumber()) {
        QFAIL(
            "First of found notes doesn't have the update sequence number "
            "set");
    }

    if (!secondNote.hasUpdateSequenceNumber()) {
        QFAIL(
            "Second of found notes doesn't have the update sequence number "
            "set");
    }

    if ((firstNote.updateSequenceNumber() != 5) ||
        (secondNote.updateSequenceNumber() != 3))
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

    const Note & firstNotePerTag = foundNotes[0];
    const Note & secondNotePerTag = foundNotes[1];

    if (!firstNotePerTag.hasUpdateSequenceNumber()) {
        QFAIL(
            "First of found notes doesn't have the update sequence number "
            "set");
    }

    if (!secondNotePerTag.hasUpdateSequenceNumber()) {
        QFAIL(
            "Second of found notes doesn't have the update sequence number "
            "set");
    }

    if (firstNotePerTag.updateSequenceNumber() <
        secondNotePerTag.updateSequenceNumber())
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
        const Note & foundNote = foundNotes[i];
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
        const Note & note = notes[i];                                          \
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

    // 12) Test method listing notes per notebook and tag local uids
    // using notebook local uids only as a filter
    QStringList notebookLocalUids = QStringList() << notebook.localUid();
    QStringList tagLocalUids;

    foundNotes = localStorageManager.listNotesPerNotebooksAndTags(
        notebookLocalUids, tagLocalUids, getNoteOptions, errorMessage,
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

        const Note & note = notes[i];
        if (!foundNotes.contains(note)) {
            QFAIL(
                "One of original notes was not found in the result of "
                "LocalStorageManager::ListNotesPerNotebooksAndTags "
                "when only notebooks are present within the filter");
        }
    }

    // 13) Test method listing notes per notebook and tag local uids
    // using tag local uids only as a filter
    notebookLocalUids.clear();
    tagLocalUids << firstTestTag.localUid();

    foundNotes = localStorageManager.listNotesPerNotebooksAndTags(
        notebookLocalUids, tagLocalUids, getNoteOptions, errorMessage,
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

        const Note & note = notes[i];
        if (!foundNotes.contains(note)) {
            QFAIL(
                "One of original notes was not found in the result of "
                "LocalStorageManager::ListNotesPerNotebooksAndTags "
                "when only tags are present within the filter");
        }
    }

    // 14) Test method listing notes per notebook and tag local uids
    // using notebook local uids and tag local uids as a filter
    notebookLocalUids << secondNotebook.localUid();
    tagLocalUids.clear();
    tagLocalUids << secondTestTag.localUid();

    foundNotes = localStorageManager.listNotesPerNotebooksAndTags(
        notebookLocalUids, tagLocalUids, getNoteOptions, errorMessage,
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
    QStringList noteLocalUids;
    noteLocalUids.reserve(3);
    for (int i = 0, size = noteLocalUids.size(); i < size; ++i) {
        noteLocalUids << notes[i].localUid();
    }

    errorMessage.clear();

    foundNotes = localStorageManager.listNotesByLocalUids(
        noteLocalUids, getNoteOptions, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        foundNotes.size() == noteLocalUids.size(),
        qPrintable(QString::fromUtf8("Unexpected number of notes found by "
                                     "method listing notes by local uids: "
                                     "expected %1 notes, got %2")
                       .arg(noteLocalUids.size(), foundNotes.size())));

    for (auto it = foundNotes.constBegin(), end = foundNotes.constEnd();
         it != end; ++it)
    {
        QVERIFY2(
            noteLocalUids.contains(it->localUid()),
            "Detected note returned by method listing notes by local uids "
            "which local uid is not present within the original list of "
            "local uids");
    }

    // 16) Test method listing notes by note local uids when the list of note
    // local uids contains uids not corresponding to existing notes
    int originalNoteLocalUidsSize = noteLocalUids.size();
    noteLocalUids << UidGenerator::Generate();
    noteLocalUids << UidGenerator::Generate();

    errorMessage.clear();
    foundNotes = localStorageManager.listNotesByLocalUids(
        noteLocalUids, getNoteOptions, errorMessage);

    QVERIFY2(
        errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

    QVERIFY2(
        foundNotes.size() == originalNoteLocalUidsSize,
        qPrintable(QString::fromUtf8("Unexpected number of notes found by "
                                     "method listing notes by local uids: "
                                     "expected %1 notes, got %2")
                       .arg(originalNoteLocalUidsSize, foundNotes.size())));

    for (auto it = foundNotes.constBegin(), end = foundNotes.constEnd();
         it != end; ++it)
    {
        QVERIFY2(
            noteLocalUids.contains(it->localUid()),
            "Detected note returned by method listing notes by local uids "
            "which local uid is not present within the original list of "
            "local uids");
    }
}

void TestListNotebooks()
{
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    ErrorString errorMessage;

    int numNotebooks = 5;
    QList<Notebook> notebooks;
    notebooks.reserve(numNotebooks);
    for (int i = 0; i < numNotebooks; ++i) {
        notebooks << Notebook();
        Notebook & notebook = notebooks.back();

        if (i > 1) {
            notebook.setGuid(
                QStringLiteral("00000000-0000-0000-c000-00000000000") +
                QString::number(i + 1));
        }

        notebook.setUpdateSequenceNumber(i + 1);

        notebook.setName(
            QStringLiteral("Fake notebook name #") + QString::number(i + 1));

        notebook.setCreationTimestamp(i + 1);
        notebook.setModificationTimestamp(i + 1);

        notebook.setDefaultNotebook(false);
        notebook.setLastUsed(false);

        notebook.setPublishingUri(
            QStringLiteral("Fake publishing uri #") + QString::number(i + 1));

        notebook.setPublishingOrder(1);
        notebook.setPublishingAscending(true);

        notebook.setPublishingPublicDescription(
            QStringLiteral("Fake public description"));

        notebook.setPublished(true);
        notebook.setStack(QStringLiteral("Fake notebook stack"));

        notebook.setBusinessNotebookDescription(
            QStringLiteral("Fake business notebook description"));

        notebook.setBusinessNotebookPrivilegeLevel(1);
        notebook.setBusinessNotebookRecommended(true);

        // NotebookRestrictions
        notebook.setCanReadNotes(true);
        notebook.setCanCreateNotes(true);
        notebook.setCanUpdateNotes(true);
        notebook.setCanExpungeNotes(false);
        notebook.setCanShareNotes(true);
        notebook.setCanEmailNotes(true);
        notebook.setCanSendMessageToRecipients(true);
        notebook.setCanUpdateNotebook(true);
        notebook.setCanExpungeNotebook(false);
        notebook.setCanSetDefaultNotebook(true);
        notebook.setCanSetNotebookStack(true);
        notebook.setCanPublishToPublic(true);
        notebook.setCanPublishToBusinessLibrary(false);
        notebook.setCanCreateTags(true);
        notebook.setCanUpdateTags(true);
        notebook.setCanExpungeTags(false);
        notebook.setCanSetParentTag(true);
        notebook.setCanCreateSharedNotebooks(true);
        notebook.setUpdateWhichSharedNotebookRestrictions(1);
        notebook.setExpungeWhichSharedNotebookRestrictions(1);

        if (i > 2) {
            notebook.setDirty(true);
        }
        else {
            notebook.setDirty(false);
        }

        if (i < 3) {
            notebook.setLocal(true);
        }
        else {
            notebook.setLocal(false);
        }

        if ((i == 0) || (i == 4)) {
            notebook.setFavorited(true);
        }
        else {
            notebook.setFavorited(false);
        }

        if (i > 1) {
            SharedNotebook sharedNotebook;
            sharedNotebook.setId(i + 1);
            sharedNotebook.setUserId(i + 1);
            sharedNotebook.setNotebookGuid(notebook.guid());

            sharedNotebook.setEmail(
                QStringLiteral("Fake shared notebook email #") +
                QString::number(i + 1));

            sharedNotebook.setCreationTimestamp(i + 1);
            sharedNotebook.setModificationTimestamp(i + 1);

            sharedNotebook.setGlobalId(
                QStringLiteral("Fake shared notebook global id #") +
                QString::number(i + 1));

            sharedNotebook.setUsername(
                QStringLiteral("Fake shared notebook username #") +
                QString::number(i + 1));

            sharedNotebook.setPrivilegeLevel(1);
            sharedNotebook.setReminderNotifyEmail(true);
            sharedNotebook.setReminderNotifyApp(false);

            notebook.addSharedNotebook(sharedNotebook);
        }

        bool res = localStorageManager.addNotebook(notebook, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
    }

    // 1) Test method listing all notebooks

    QList<Notebook> foundNotebooks =
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
        const Notebook & foundNotebook = foundNotebooks.at(i);
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
        const Notebook & notebook = notebooks.at(i);                           \
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
    Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    LocalStorageManager localStorageManager(account, startupOptions);

    LinkedNotebook linkedNotebook;

    linkedNotebook.setGuid(
        QStringLiteral("00000000-0000-0000-c000-000000000001"));

    linkedNotebook.setUpdateSequenceNumber(1);
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

    Notebook notebook;
    notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    notebook.setLinkedNotebookGuid(linkedNotebook.guid());
    notebook.setUpdateSequenceNumber(1);
    notebook.setName(QStringLiteral("Fake notebook name"));
    notebook.setCreationTimestamp(1);
    notebook.setModificationTimestamp(1);

    Note note;
    note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000046"));
    note.setUpdateSequenceNumber(1);
    note.setTitle(QStringLiteral("Fake note title"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreationTimestamp(1);
    note.setModificationTimestamp(1);
    note.setActive(true);
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalUid(notebook.localUid());

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
    QList<Tag> tags;
    tags.reserve(nTags);
    for (int i = 0; i < nTags; ++i) {
        tags.push_back(Tag());
        Tag & tag = tags.back();

        tag.setGuid(
            QStringLiteral("00000000-0000-0000-c000-00000000000") +
            QString::number(i + 1));

        tag.setUpdateSequenceNumber(i);
        tag.setName(QStringLiteral("Tag name #") + QString::number(i));

        if (i > 2) {
            tag.setLinkedNotebookGuid(linkedNotebook.guid());
        }

        errorMessage.clear();
        res = localStorageManager.addTag(tag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        errorMessage.clear();

        note.addTagGuid(tag.guid());
        note.addTagLocalUid(tag.localUid());

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

    QList<Tag> foundTags;
    foundTags.reserve(3);
    errorMessage.clear();
    foundTags = localStorageManager.listAllTags(errorMessage);
    if (foundTags.isEmpty() && !errorMessage.isEmpty()) {
        QFAIL(qPrintable(errorMessage.nonLocalizedString()));
    }

    for (int i = 0; i < nTags; ++i) {
        const Tag & tag = tags[i];

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
