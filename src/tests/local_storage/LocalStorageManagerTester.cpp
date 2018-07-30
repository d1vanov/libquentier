/*
 * Copyright 2018 Dmitry Ivanov
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

#include "LocalStorageManagerTester.h"
#include "LocalStorageManagerTests.h"
#include "SavedSearchLocalStorageManagerAsyncTester.h"
#include "LinkedNotebookLocalStorageManagerAsyncTester.h"
#include "TagLocalStorageManagerAsyncTester.h"
#include "UserLocalStorageManagerAsyncTester.h"
#include "NotebookLocalStorageManagerAsyncTester.h"
#include "NoteLocalStorageManagerAsyncTester.h"
#include "NoteSearchQueryTest.h"
#include "ResourceLocalStorageManagerAsyncTester.h"
#include "LocalStorageManagerNoteSearchQueryTest.h"
#include "LocalStorageCacheAsyncTester.h"
#include <quentier/logging/QuentierLogger.h>
#include <quentier/types/RegisterMetatypes.h>
#include <quentier/utility/SysInfo.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <QTextStream>
#include <QtTest/QTest>
#include <QTimer>

// 10 minutes should be enough
#define MAX_ALLOWED_TEST_DURATION_MSEC 600000

#define CATCH_EXCEPTION() \
    catch(const std::exception & exception) { \
        SysInfo sysInfo; \
        QFAIL(qPrintable(QStringLiteral("Caught exception: ") + QString::fromUtf8(exception.what()) + \
                         QStringLiteral(", backtrace: ") + sysInfo.stackTrace())); \
    }

#if QT_VERSION >= 0x050000
inline void nullMessageHandler(QtMsgType type, const QMessageLogContext &, const QString & message) {
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#else
inline void nullMessageHandler(QtMsgType type, const char * message) {
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#endif

namespace quentier {
namespace test {

LocalStorageManagerTester::LocalStorageManagerTester(QObject * parent) :
    QObject(parent)
{}

LocalStorageManagerTester::~LocalStorageManagerTester()
{}

void LocalStorageManagerTester::init()
{
    registerMetatypes();

#if QT_VERSION >= 0x050000
    qInstallMessageHandler(nullMessageHandler);
#else
    qInstallMsgHandler(nullMessageHandler);
#endif
}

void LocalStorageManagerTester::noteSearchQueryTest()
{
    try
    {
        QString error;
        bool res = NoteSearchQueryTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerNoteSearchQueryTest()
{
    try
    {
        QString error;
        bool res = LocalStorageManagerNoteSearchQueryTest(error);
        if (!res) {
            QFAIL(qPrintable(error));
        }
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualSavedSearchTest()
{
    try
    {
        QString error;
        bool res = TestSavedSearchAddFindUpdateExpungeInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualLinkedNotebookTest()
{
    try
    {
        QString error;
        bool res = TestLinkedNotebookAddFindUpdateExpungeInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualTagTest()
{
    try
    {
        QString error;
        bool res = TestTagAddFindUpdateExpungeInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualResourceTest()
{
    try
    {
        QString error;
        bool res = TestResourceAddFindUpdateExpungeInLocalStorage(error);
        QVERIFY2(res == true, error.toStdString().c_str());
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagedIndividualNoteTest()
{
    try
    {
        QString error;
        bool res = TestNoteFindUpdateDeleteExpungeInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualNotebookTest()
{
    try
    {
        QString error;
        bool res = TestNotebookFindUpdateDeleteExpungeInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagedIndividualUserTest()
{
    try
    {
        QString error;
        bool res = TestUserAddFindUpdateDeleteExpungeInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerSequentialUpdatesTest()
{
    try
    {
        QString error;
        bool res = TestSequentialUpdatesInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAccountHighUsnTest()
{
    try
    {
        QString error;
        bool res = TestAccountHighUsnInLocalStorage(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAddNoteWithoutLocalUidTest()
{
    try
    {
        QString error;
        bool res = TestAddingNoteWithoutLocalUid(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerNoteTagIdsComplementTest()
{
    try
    {
        QString error;
        bool res = TestNoteTagIdsComplementWhenAddingAndUpdatingNote(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListSavedSearchesTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

        ErrorString errorMessage;

        int nSearches = 5;
        QList<SavedSearch> searches;
        searches.reserve(nSearches);
        for(int i = 0; i < nSearches; ++i)
        {
            searches << SavedSearch();
            SavedSearch & search = searches.back();

            if (i > 1) {
                search.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
            }

            search.setUpdateSequenceNumber(i);
            search.setName(QStringLiteral("SavedSearch #") + QString::number(i));
            search.setQuery(QStringLiteral("Fake saved search query #") + QString::number(i));
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
        QList<SavedSearch> foundSearches = localStorageManager.listAllSavedSearches(errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        int numFoundSearches = foundSearches.size();
        if (numFoundSearches != nSearches) {
            QFAIL(qPrintable(QStringLiteral("Error: number of saved searches in the result of LocalStorageManager::ListAllSavedSearches (") +
                             QString::number(numFoundSearches) + QStringLiteral(") does not match the original number of added saved searches (") +
                             QString::number(nSearches) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundSearches; ++i)
        {
            const SavedSearch & foundSearch = foundSearches.at(i);
            if (!searches.contains(foundSearch)) {
                QFAIL("One of saved searches from the result of LocalStorageManager::ListAllSavedSearches "
                      "was not found in the list of original searches");
            }
        }

#define CHECK_LIST_SAVED_SEARCHES_BY_FLAG(flag, flag_name, true_condition, false_condition) \
        errorMessage.clear(); \
        foundSearches = localStorageManager.listSavedSearches(flag, errorMessage); \
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString())); \
        \
        for(int i = 0; i < nSearches; ++i) \
        { \
            const SavedSearch & search = searches.at(i); \
            bool res = foundSearches.contains(search); \
            if ((true_condition) && !res) { \
                QNWARNING(QStringLiteral("Not found saved search: ") << search); \
                QFAIL("One of " flag_name " SavedSearches was not found by LocalStorageManager::ListSavedSearches"); \
            } \
            else if ((false_condition) && res) { \
                QNWARNING(QStringLiteral("Found irrelevant saved search: ") << search); \
                QFAIL("LocalStorageManager::ListSavedSearches with flag " flag_name " returned incorrect saved search"); \
            } \
        }

        // 2) Test method listing only dirty saved searches
        CHECK_LIST_SAVED_SEARCHES_BY_FLAG(LocalStorageManager::ListDirty, "dirty", i > 2, i <= 2);

        // 3) Test method listing only local saved searches
        CHECK_LIST_SAVED_SEARCHES_BY_FLAG(LocalStorageManager::ListLocal, "local", i < 3, i >= 3);

        // 4) Test method listing only saved searches without guid
        CHECK_LIST_SAVED_SEARCHES_BY_FLAG(LocalStorageManager::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

        // 5) Test method listing only favorited saved searches
        CHECK_LIST_SAVED_SEARCHES_BY_FLAG(LocalStorageManager::ListFavoritedElements, "favorited", (i == 0) || (i == 4), (i != 0) && (i != 4));

        // 6) Test method listing dirty favorited saved searches with guid
        CHECK_LIST_SAVED_SEARCHES_BY_FLAG(LocalStorageManager::ListDirty | LocalStorageManager::ListElementsWithGuid | LocalStorageManager::ListFavoritedElements,
                                          "dirty, favorited, having guid", i == 4, i != 4);

        // 7) Test method listing local favorited saved searches
        CHECK_LIST_SAVED_SEARCHES_BY_FLAG(LocalStorageManager::ListLocal | LocalStorageManager::ListFavoritedElements,
                                          "local, favorited", i == 0, i != 0);

        // 8) Test method listing saved searches with guid set also specifying limit, offset and order
        size_t limit = 2;
        size_t offset = 1;
        LocalStorageManager::ListSavedSearchesOrder::type order = LocalStorageManager::ListSavedSearchesOrder::ByUpdateSequenceNumber;

        errorMessage.clear();
        foundSearches = localStorageManager.listSavedSearches(LocalStorageManager::ListElementsWithGuid, errorMessage, limit, offset, order);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        if (foundSearches.size() != static_cast<int>(limit)) {
            QFAIL(qPrintable(QStringLiteral("Unexpected number of found saved searches not corresponding to the specified limit: limit = ") +
                             QString::number(limit) + QStringLiteral(", number of searches found is ") + QString::number(foundSearches.size())));
        }

        const SavedSearch & firstSearch = foundSearches[0];
        const SavedSearch & secondSearch = foundSearches[1];

        if (!firstSearch.hasUpdateSequenceNumber() || !secondSearch.hasUpdateSequenceNumber()) {
            QFAIL(qPrintable(QStringLiteral("One of found saved searches doesn't have the update sequence number which is unexpected: first search: ") +
                             firstSearch.toString() + QStringLiteral("\nSecond search: ") + secondSearch.toString()));
        }

        if (firstSearch.updateSequenceNumber() != 3) {
            QFAIL(qPrintable(QStringLiteral("First saved search was expected to have update sequence number of 3, instead it is ") +
                             QString::number(firstSearch.updateSequenceNumber())));
        }

        if (secondSearch.updateSequenceNumber() != 4) {
            QFAIL(qPrintable(QStringLiteral("Second saved search was expected to have update sequence number of 4, instead it is ") +
                             QString::number(secondSearch.updateSequenceNumber())));
        }
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListLinkedNotebooksTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

        ErrorString errorMessage;

        int nLinkedNotebooks = 5;
        QList<LinkedNotebook> linkedNotebooks;
        linkedNotebooks.reserve(nLinkedNotebooks);
        for(int i = 0; i < nLinkedNotebooks; ++i)
        {
            linkedNotebooks << LinkedNotebook();
            LinkedNotebook & linkedNotebook = linkedNotebooks.back();

            linkedNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
            linkedNotebook.setUpdateSequenceNumber(i);
            linkedNotebook.setShareName(QStringLiteral("Linked notebook share name #") + QString::number(i));
            linkedNotebook.setUsername(QStringLiteral("Linked notebook username #") + QString::number(i));
            linkedNotebook.setShardId(QStringLiteral("Linked notebook shard id #") + QString::number(i));
            linkedNotebook.setSharedNotebookGlobalId(QStringLiteral("Linked notebook shared notebook global id #") + QString::number(i));
            linkedNotebook.setUri(QStringLiteral("Linked notebook uri #") + QString::number(i));
            linkedNotebook.setNoteStoreUrl(QStringLiteral("Linked notebook note store url #") + QString::number(i));
            linkedNotebook.setWebApiUrlPrefix(QStringLiteral("Linked notebook web api url prefix #") + QString::number(i));
            linkedNotebook.setStack(QStringLiteral("Linked notebook stack #") + QString::number(i));
            linkedNotebook.setBusinessId(1);

            if (i > 2) {
                linkedNotebook.setDirty(true);
            }
            else {
                linkedNotebook.setDirty(false);
            }

            bool res = localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);
            QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
        }

        // 1) Test method listing all linked notebooks

        errorMessage.clear();
        QList<LinkedNotebook> foundLinkedNotebooks = localStorageManager.listAllLinkedNotebooks(errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        int numFoundLinkedNotebooks = foundLinkedNotebooks.size();
        if (numFoundLinkedNotebooks != nLinkedNotebooks) {
            QFAIL(qPrintable(QStringLiteral("Error: number of linked notebooks in the result of LocalStorageManager::ListAllLinkedNotebooks (") +
                             QString::number(numFoundLinkedNotebooks) + QStringLiteral(") does not match the original number of added linked notebooks (") +
                             QString::number(nLinkedNotebooks) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundLinkedNotebooks; ++i)
        {
            const LinkedNotebook & foundLinkedNotebook = foundLinkedNotebooks.at(i);
            if (!linkedNotebooks.contains(foundLinkedNotebook)) {
                QFAIL("One of linked notebooks from the result of LocalStorageManager::ListAllLinkedNotebooks "
                      "was not found in the list of original linked notebooks");
            }
        }

        // 2) Test method listing only dirty linked notebooks
        errorMessage.clear();
        foundLinkedNotebooks = localStorageManager.listLinkedNotebooks(LocalStorageManager::ListDirty, errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        for(int i = 0; i < nLinkedNotebooks; ++i)
        {
            const LinkedNotebook & linkedNotebook = linkedNotebooks.at(i);
            bool res = foundLinkedNotebooks.contains(linkedNotebook);
            if ((i > 2) && !res) {
                QNWARNING(QStringLiteral("Not found linked notebook: ") << linkedNotebook);
                QFAIL("One of dirty linked notebooks was not found by LocalStorageManager::ListLinkedNotebooks");
            }
            else if ((i <= 2) && res) {
                QNWARNING(QStringLiteral("Found irrelevant linked notebook: ") << linkedNotebook);
                QFAIL("LocalStorageManager::ListLinkedNotebooks with flag ListDirty returned incorrect linked notebook");
            }
        }
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListTagsTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

        ErrorString errorMessage;

        int nTags = 5;
        QVector<Tag> tags;
        tags.reserve(nTags);
        for(int i = 0; i < nTags; ++i)
        {
            tags.push_back(Tag());
            Tag & tag = tags.back();

            if (i > 1) {
                tag.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
            }

            tag.setUpdateSequenceNumber(i);
            tag.setName(QStringLiteral("Tag name #") + QString::number(i));

            if (i > 2) {
                tag.setParentGuid(tags.at(i-1).guid());
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
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        int numFoundTags = foundTags.size();
        if (numFoundTags != nTags) {
            QFAIL(qPrintable(QStringLiteral("Error: number of tags in the result of LocalStorageManager::ListAllTags (") +
                             QString::number(numFoundTags) + QStringLiteral(") does not match the original number of added tags (") +
                             QString::number(nTags) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundTags; ++i)
        {
            const Tag & foundTag = foundTags.at(i);
            if (!tags.contains(foundTag)) {
                QFAIL("One of tags from the result of LocalStorageManager::ListAllTags "
                      "was not found in the list of original tags");
            }
        }

#define CHECK_LIST_TAGS_BY_FLAG(flag, flag_name, true_condition, false_condition) \
        errorMessage.clear(); \
        foundTags = localStorageManager.listTags(flag, errorMessage); \
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString())); \
        \
        for(int i = 0; i < nTags; ++i) \
        { \
            const Tag & tag = tags.at(i); \
            bool res = foundTags.contains(tag); \
            if ((true_condition) && !res) { \
                QNWARNING(QStringLiteral("Not found tag: ") << tag); \
                QFAIL("One of " flag_name " Tags was not found by LocalStorageManager::ListTags"); \
            } \
            else if ((false_condition) && res) { \
                QNWARNING(QStringLiteral("Found irrelevant tag: ") << tag); \
                QFAIL("LocalStorageManager::ListTags with flag " flag_name " returned incorrect tag"); \
            } \
        }

        // 2) Test method listing only dirty tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListDirty, "dirty", i > 2, i <= 2);

        // 3) Test method listing only local tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListLocal, "local", i < 3, i >= 3);

        // 4) Test method listing only tags without guid
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

        // 5) Test method listing only favorited tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListFavoritedElements, "favorited", (i == 0) || (i == 4), (i != 0) && (i != 4));

        // 6) Test method listing dirty favorited tags with guid
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListDirty | LocalStorageManager::ListElementsWithGuid | LocalStorageManager::ListFavoritedElements,
                                "dirty, favorited, having guid", i == 4, i != 4);

        // 7) Test method listing local favorited tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListLocal | LocalStorageManager::ListFavoritedElements,
                                "local, favorited", i == 0, i != 0);

#undef CHECK_LIST_TAGS_BY_FLAG
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListTagsWithNoteLocalUidsTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

        ErrorString errorMessage;

        int nTags = 5;
        QVector<Tag> tags;
        tags.reserve(nTags);
        for(int i = 0; i < nTags; ++i)
        {
            tags.push_back(Tag());
            Tag & tag = tags.back();

            if (i > 1) {
                tag.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
            }

            tag.setUpdateSequenceNumber(i);
            tag.setName(QStringLiteral("Tag name #") + QString::number(i));

            if (i > 2) {
                tag.setParentGuid(tags.at(i-1).guid());
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
        for(int i = 0; i < numNotes; ++i)
        {
            notes << Note();
            Note & note = notes.back();

            if (i > 1) {
                note.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
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

#define APPEND_TAG_TO_NOTE(tagNum) \
            note.addTagLocalUid(tags[tagNum].localUid()); \
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

            note.setUpdateSequenceNumber(i+1);
            note.setTitle(QStringLiteral("Fake note title #") + QString::number(i));
            note.setContent(QStringLiteral("<en-note><h1>Hello, world #") + QString::number(i) + QStringLiteral("</h1></en-note>"));
            note.setCreationTimestamp(i+1);
            note.setModificationTimestamp(i+1);
            note.setActive(true);
            note.setNotebookGuid(notebook.guid());
            note.setNotebookLocalUid(notebook.localUid());

            res = localStorageManager.addNote(note, errorMessage);
            QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
        }

        QList<std::pair<Tag, QStringList> > foundTagsWithNoteLocalUids;

#define CHECK_LIST_TAGS_BY_FLAG(flag, flag_name, true_condition, false_condition) \
        errorMessage.clear(); \
        foundTagsWithNoteLocalUids = localStorageManager.listTagsWithNoteLocalUids(flag, errorMessage); \
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString())); \
        \
        for(int i = 0; i < nTags; ++i) \
        { \
            const Tag & tag = tags.at(i); \
            int tagIndex = 0; \
            bool res = false; \
            for(int size = foundTagsWithNoteLocalUids.size(); tagIndex < size; ++tagIndex) { \
                if (foundTagsWithNoteLocalUids[tagIndex].first == tag) { \
                    res = true; \
                    break; \
                } \
            } \
            if ((true_condition) && !res) { \
                QNWARNING(QStringLiteral("Not found tag: ") << tag); \
                QFAIL("One of " flag_name " Tags was not found by LocalStorageManager::ListTags"); \
            } \
            else if ((false_condition) && res) { \
                QNWARNING(QStringLiteral("Found irrelevant tag: ") << tag); \
                QFAIL("LocalStorageManager::ListTags with flag " flag_name " returned incorrect tag"); \
            } \
            else if (res) { \
                auto noteIt = noteLocalUidsByTagLocalUid.find(tag.localUid()); \
                if (noteIt == noteLocalUidsByTagLocalUid.end() && !foundTagsWithNoteLocalUids[tagIndex].second.isEmpty()) { \
                    QNWARNING(QStringLiteral("Found irrelevant list of note local uids for a tag: ") << foundTagsWithNoteLocalUids[tagIndex].second.join(QStringLiteral(", "))); \
                    QFAIL("LocalStorageManager::ListTags with flag " flag_name " returned redundant note local uids"); \
                } \
                else if (noteIt != noteLocalUidsByTagLocalUid.end()) { \
                    if (foundTagsWithNoteLocalUids[tagIndex].second.isEmpty()) { \
                        QNWARNING(QStringLiteral("Found empty list of note local uids for a tag for which they were expected: ") << noteIt.value().join(QStringLiteral(", "))); \
                        QFAIL("LocalStorageManager::ListTags with flag " flag_name " did not return proper note local uids"); \
                    } \
                    else if (foundTagsWithNoteLocalUids[tagIndex].second.size() != noteIt.value().size()) { \
                        QNWARNING(QStringLiteral("Found list of note local uids for a tag with incorrect list size: ") << foundTagsWithNoteLocalUids[tagIndex].second.join(QStringLiteral(", "))); \
                        QFAIL("LocalStorageManager::ListTags with flag " flag_name " did not return proper number of note local uids"); \
                    } \
                    else { \
                        for(int j = 0; j < foundTagsWithNoteLocalUids[tagIndex].second.size(); ++j) { \
                            if (!noteIt.value().contains(foundTagsWithNoteLocalUids[tagIndex].second[j])) { \
                                QNWARNING(QStringLiteral("Found incorrect list of note local uids for a tag: ") << foundTagsWithNoteLocalUids[tagIndex].second.join(QStringLiteral(", "))); \
                                QFAIL("LocalStorageManager::ListTags with flag " flag_name " did not return correct set of note local uids"); \
                            } \
                        } \
                    } \
                } \
            } \
        }

        // 1) Test method listing all tags with note local uids
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListAll, "all", true, false);

        // 2) Test method listing only dirty tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListDirty, "dirty", i > 2, i <= 2);

        // 3) Test method listing only local tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListLocal, "local", i < 3, i >= 3);

        // 4) Test method listing only tags without guid
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

        // 5) Test method listing only favorited tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListFavoritedElements, "favorited", (i == 0) || (i == 4), (i != 0) && (i != 4));

        // 6) Test method listing dirty favorited tags with guid
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListDirty | LocalStorageManager::ListElementsWithGuid | LocalStorageManager::ListFavoritedElements,
                                "dirty, favorited, having guid", i == 4, i != 4);

        // 7) Test method listing local favorited tags
        CHECK_LIST_TAGS_BY_FLAG(LocalStorageManager::ListLocal | LocalStorageManager::ListFavoritedElements,
                                "local, favorited", i == 0, i != 0);
#undef CHECK_LIST_TAGS_BY_FLAG
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListAllSharedNotebooksTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

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
        for(int i = 0; i < numSharedNotebooks; ++i)
        {
            sharedNotebooks << SharedNotebook();
            SharedNotebook & sharedNotebook = sharedNotebooks.back();

            sharedNotebook.setId(i);
            sharedNotebook.setUserId(i);
            sharedNotebook.setNotebookGuid(notebook.guid());
            sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email #") + QString::number(i));
            sharedNotebook.setCreationTimestamp(i+1);
            sharedNotebook.setModificationTimestamp(i+1);
            sharedNotebook.setGlobalId(QStringLiteral("Fake shared notebook global id #") + QString::number(i));
            sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username #") + QString::number(i));
            sharedNotebook.setPrivilegeLevel(1);
            sharedNotebook.setReminderNotifyEmail(true);
            sharedNotebook.setReminderNotifyApp(false);

            notebook.addSharedNotebook(sharedNotebook);
        }

        ErrorString errorMessage;
        bool res = localStorageManager.addNotebook(notebook, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        QList<SharedNotebook> foundSharedNotebooks = localStorageManager.listAllSharedNotebooks(errorMessage);
        QVERIFY2(!foundSharedNotebooks.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        int numFoundSharedNotebooks = foundSharedNotebooks.size();
        if (numFoundSharedNotebooks != numSharedNotebooks) {
            QFAIL(qPrintable(QStringLiteral("Error: number of shared notebooks in the result of LocalStorageManager::ListAllSharedNotebooks (") +
                             QString::number(numFoundSharedNotebooks) + QStringLiteral(") does not match the original number of added shared notebooks (") +
                             QString::number(numSharedNotebooks) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundSharedNotebooks; ++i)
        {
            const SharedNotebook & foundSharedNotebook = foundSharedNotebooks.at(i);
            if (!sharedNotebooks.contains(foundSharedNotebook)) {
                QFAIL("One of shared notebooks from the result of LocalStorageManager::ListAllSharedNotebooks "
                      "was not found in the list of original shared notebooks");
            }
        }
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListAllTagsPerNoteTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

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
        for(int i = 0; i < numTags; ++i)
        {
            tags << Tag();
            Tag & tag = tags.back();

            tag.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
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

            res = localStorageManager.updateNote(note, /* update resources = */ false, /* update tags = */ true, errorMessage);
            QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
        }

        Tag tagNotLinkedWithNote;
        tagNotLinkedWithNote.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000045"));
        tagNotLinkedWithNote.setUpdateSequenceNumber(9);
        tagNotLinkedWithNote.setName(QStringLiteral("Tag not linked with note"));

        res = localStorageManager.addTag(tagNotLinkedWithNote, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        // 1) Test method listing all tags per given note without any additional conditions

        errorMessage.clear();
        QList<Tag> foundTags = localStorageManager.listAllTagsPerNote(note, errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        int numFoundTags = foundTags.size();
        if (numFoundTags != numTags) {
            QFAIL(qPrintable(QStringLiteral("Error: number of tags in the result of LocalStorageManager::ListAllTagsPerNote (") +
                             QString::number(numFoundTags) + QStringLiteral(") does not match the original number of added tags (") +
                             QString::number(numTags) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundTags; ++i)
        {
            const Tag & foundTag = foundTags.at(i);
            if (!tags.contains(foundTag)) {
                QFAIL("One of tags from the result of LocalStorageManager::ListAllTagsPerNote "
                      "was not found in the list of original tags");
            }
        }

        if (foundTags.contains(tagNotLinkedWithNote)) {
            QFAIL("Found tag not linked with testing note in the result of LocalStorageManager::ListAllTagsPerNote");
        }

        // 2) Test method listing all tags per note consideting only dirty ones + with limit, offset,
        // specific order and order direction

        errorMessage.clear();
        const size_t limit = 2;
        const size_t offset = 1;
        const LocalStorageManager::ListObjectsOptions flag = LocalStorageManager::ListDirty;
        const LocalStorageManager::ListTagsOrder::type order = LocalStorageManager::ListTagsOrder::ByUpdateSequenceNumber;
        const LocalStorageManager::OrderDirection::type orderDirection = LocalStorageManager::OrderDirection::Descending;
        foundTags = localStorageManager.listAllTagsPerNote(note, errorMessage, flag, limit, offset,
                                                           order, orderDirection);
        if (foundTags.size() != static_cast<int>(limit)) {
            QFAIL(qPrintable(QStringLiteral("Found unexpected amount of tags: expected to find ") + QString::number(limit) +
                             QStringLiteral(" tags, found ") + QString::number(foundTags.size()) + QStringLiteral(" tags")));
        }

        const Tag & firstTag = foundTags[0];
        const Tag & secondTag = foundTags[1];

        if (!firstTag.hasUpdateSequenceNumber()) {
            QFAIL("First of found tags doesn't have the update sequence number set");
        }

        if (!secondTag.hasUpdateSequenceNumber()) {
            QFAIL("Second of found tags doesn't have the update sequence number set");
        }

        if ((firstTag.updateSequenceNumber() != 3) || (secondTag.updateSequenceNumber() != 2)) {
            QFAIL(qPrintable(QStringLiteral("Unexpected order of found tags by update sequence number: first tag: ") +
                             firstTag.toString() + QStringLiteral("\nSecond tag: ") + secondTag.toString()));
        }
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListNotesTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

        Notebook notebook;
        notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
        notebook.setUpdateSequenceNumber(1);
        notebook.setName(QStringLiteral("Fake notebook name"));
        notebook.setCreationTimestamp(1);
        notebook.setModificationTimestamp(1);

        ErrorString errorMessage;
        bool res = localStorageManager.addNotebook(notebook, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        Notebook notebookNotLinkedWithNotes;
        notebookNotLinkedWithNotes.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
        notebookNotLinkedWithNotes.setUpdateSequenceNumber(1);
        notebookNotLinkedWithNotes.setName(QStringLiteral("Fake notebook not linked with notes name name"));
        notebookNotLinkedWithNotes.setCreationTimestamp(1);
        notebookNotLinkedWithNotes.setModificationTimestamp(1);

        res = localStorageManager.addNotebook(notebookNotLinkedWithNotes, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        Tag testTag;
        testTag.setName(QStringLiteral("My test tag"));
        res = localStorageManager.addTag(testTag, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        int numNotes = 5;
        QList<Note> notes;
        notes.reserve(numNotes);
        for(int i = 0; i < numNotes; ++i)
        {
            notes << Note();
            Note & note = notes.back();

            if (i > 1) {
                note.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
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
                note.addTagLocalUid(testTag.localUid());
            }

            note.setUpdateSequenceNumber(i+1);
            note.setTitle(QStringLiteral("Fake note title #") + QString::number(i));
            note.setContent(QStringLiteral("<en-note><h1>Hello, world #") + QString::number(i) + QStringLiteral("</h1></en-note>"));
            note.setCreationTimestamp(i+1);
            note.setModificationTimestamp(i+1);
            note.setActive(true);
            note.setNotebookGuid(notebook.guid());
            note.setNotebookLocalUid(notebook.localUid());

            res = localStorageManager.addNote(note, errorMessage);
            QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
        }

        // 1) Test method listing all notes per notebook

        errorMessage.clear();
        QList<Note> foundNotes = localStorageManager.listNotesPerNotebook(notebook, errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        int numFoundNotes = foundNotes.size();
        if (numFoundNotes != numNotes) {
            QFAIL(qPrintable(QStringLiteral("Error: number of notes in the result of LocalStorageManager::ListNotesPerNotebook (") +
                             QString::number(numFoundNotes) + QStringLiteral(") does not match the original number of added notes (") +
                             QString::number(numNotes) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundNotes; ++i)
        {
            const Note & foundNote = foundNotes.at(i);
            if (!notes.contains(foundNote)) {
                QFAIL("One of notes from the result of LocalStorageManager::ListNotesPerNotebook "
                      "was not found in the list of original notes");
            }
        }

        // 2) Ensure the method listing notes per notebook actually checks the notebook

        errorMessage.clear();
        foundNotes = localStorageManager.listNotesPerNotebook(notebookNotLinkedWithNotes, errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        if (foundNotes.size() != 0) {
            QFAIL(qPrintable(QStringLiteral("Found non-zero number of notes in the result of LocalStorageManager::ListNotesPerNotebook "
                                            "called with guid of notebook not containing any notes (found ") +
                             QString::number(foundNotes.size()) + QStringLiteral(" notes)")));
        }

        // 3) Test method listing notes per notebook considering only the notes with guid + with limit, offset,
        // specific order and order direction

        errorMessage.clear();
        size_t limit = 2;
        size_t offset = 1;
        LocalStorageManager::ListNotesOrder::type order = LocalStorageManager::ListNotesOrder::ByUpdateSequenceNumber;
        LocalStorageManager::OrderDirection::type orderDirection = LocalStorageManager::OrderDirection::Descending;
        foundNotes = localStorageManager.listNotesPerNotebook(notebook, errorMessage, /* with resource metadata = */ true,
                                                              /* with resource binary data = */ true,
                                                              LocalStorageManager::ListElementsWithGuid, limit, offset,
                                                              order, orderDirection);
        if (foundNotes.size() != static_cast<int>(limit)) {
            QFAIL(qPrintable(QStringLiteral("Found unexpected amount of notes: expected to find ") + QString::number(limit) +
                             QStringLiteral(" notes, found ") + QString::number(foundNotes.size()) + QStringLiteral(" notes")));
        }

        const Note & firstNote = foundNotes[0];
        const Note & secondNote = foundNotes[1];

        if (!firstNote.hasUpdateSequenceNumber()) {
            QFAIL("First of found notes doesn't have the update sequence number set");
        }

        if (!secondNote.hasUpdateSequenceNumber()) {
            QFAIL("Second of found notes doesn't have the update sequence number set");
        }

        if ((firstNote.updateSequenceNumber() != 4) || (secondNote.updateSequenceNumber() != 3)) {
            QFAIL(qPrintable(QStringLiteral("Unexpected order of found notes by update sequence number: first note: ") +
                             firstNote.toString() + QStringLiteral("\nSecond note: ") + secondNote.toString()));
        }

        // 4) Test method listing notes per tag considering only the notes with guid + with limit, offset,
        // specific order and order direction

        errorMessage.clear();
        limit = 2;
        offset = 0;

        foundNotes = localStorageManager.listNotesPerTag(testTag, errorMessage, /* with resource metadata = */ true,
                                                         /* with resource binary data = */ true,
                                                         LocalStorageManager::ListElementsWithGuid, limit, offset,
                                                         order, orderDirection);
        if (foundNotes.size() != static_cast<int>(limit)) {
            QFAIL(qPrintable(QStringLiteral("Found unexpected amount of notes: expected to find ") + QString::number(limit) +
                             QStringLiteral(" notes, found ") + QString::number(foundNotes.size()) + QStringLiteral(" notes")));
        }

        const Note & firstNotePerTag = foundNotes[0];
        const Note & secondNotePerTag = foundNotes[1];

        if (!firstNotePerTag.hasUpdateSequenceNumber()) {
            QFAIL("First of found notes doesn't have the update sequence number set");
        }

        if (!secondNotePerTag.hasUpdateSequenceNumber()) {
            QFAIL("Second of found notes doesn't have the update sequence number set");
        }

        if (firstNotePerTag.updateSequenceNumber() < secondNotePerTag.updateSequenceNumber()) {
            QFAIL("Incorrect sorting of found notes, expected descending sorting by update sequence number");
        }

        if ((firstNotePerTag != notes[4]) && (secondNotePerTag != notes[2])) {
            QFAIL("Found unexpected notes per tag");
        }

        // 5) Test method listing all notes
        errorMessage.clear();
        foundNotes = localStorageManager.listNotes(LocalStorageManager::ListAll, errorMessage);
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        numFoundNotes = foundNotes.size();
        if (numFoundNotes != numNotes) {
            QFAIL(qPrintable(QStringLiteral("Error number of notes in the result of LocalStorageManager::ListNotes with flag ListAll (") +
                             QString::number(numFoundNotes) + QStringLiteral(") does not match the original number of added notes (") +
                             QString::number(numNotes) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundNotes; ++i)
        {
            const Note & foundNote = foundNotes[i];
            if (!notes.contains(foundNote)) {
                QFAIL("One of notes from the result of LocalStorageManager::ListNotes with flag ListAll "
                      "was not found in the list of original notes");
            }
        }

#define CHECK_LIST_NOTES_BY_FLAG(flag, flag_name, true_condition, false_condition) \
        errorMessage.clear(); \
        foundNotes = localStorageManager.listNotes(flag, errorMessage); \
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString())); \
        \
        for(int i = 0; i < numNotes; ++i) \
        { \
            const Note & note = notes[i]; \
            bool res = foundNotes.contains(note); \
            if ((true_condition) && !res) { \
                QNWARNING(QStringLiteral("Not found note: ") << note); \
                QFAIL("One of " flag_name " notes was not found by LocalStorageManager::ListNotes"); \
            } \
            else if ((false_condition) && res) { \
                QNWARNING(QStringLiteral("Found irrelevant note: ") << note); \
                QFAIL("LocalStorageManager::ListNotes with flag " flag_name " returned incorrect note"); \
            } \
        }

        // 6) Test method listing only dirty notes
        CHECK_LIST_NOTES_BY_FLAG(LocalStorageManager::ListDirty, "dirty", i > 2, i <= 2);

        // 7) Test method listing only local notes
        CHECK_LIST_NOTES_BY_FLAG(LocalStorageManager::ListLocal, "local", i < 3, i >= 3);

        // 8) Test method listing only notes without guid
        CHECK_LIST_NOTES_BY_FLAG(LocalStorageManager::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

        // 9) Test method listing only favorited notes
        CHECK_LIST_NOTES_BY_FLAG(LocalStorageManager::ListFavoritedElements, "favorited", (i == 0) || (i == 4), (i != 0) && (i != 4));

        // 10) Test method listing dirty favorited notes with guid
        CHECK_LIST_NOTES_BY_FLAG(LocalStorageManager::ListDirty | LocalStorageManager::ListElementsWithGuid | LocalStorageManager::ListFavoritedElements,
                                 "dirty, favorited, having guid", i == 4, i != 4);

        // 11) Test method listing local favorited notes
        CHECK_LIST_NOTES_BY_FLAG(LocalStorageManager::ListLocal | LocalStorageManager::ListFavoritedElements,
                                 "local, favorited", i == 0, i != 0);
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListNotebooksTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

        ErrorString errorMessage;

        int numNotebooks = 5;
        QList<Notebook> notebooks;
        notebooks.reserve(numNotebooks);
        for(int i = 0; i < numNotebooks; ++i)
        {
            notebooks << Notebook();
            Notebook & notebook = notebooks.back();

            if (i > 1) {
                notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
            }

            notebook.setUpdateSequenceNumber(i+1);
            notebook.setName(QStringLiteral("Fake notebook name #") + QString::number(i+1));
            notebook.setCreationTimestamp(i+1);
            notebook.setModificationTimestamp(i+1);

            notebook.setDefaultNotebook(false);
            notebook.setLastUsed(false);
            notebook.setPublishingUri(QStringLiteral("Fake publishing uri #") + QString::number(i+1));
            notebook.setPublishingOrder(1);
            notebook.setPublishingAscending(true);
            notebook.setPublishingPublicDescription(QStringLiteral("Fake public description"));
            notebook.setPublished(true);
            notebook.setStack(QStringLiteral("Fake notebook stack"));
            notebook.setBusinessNotebookDescription(QStringLiteral("Fake business notebook description"));
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
                sharedNotebook.setId(i+1);
                sharedNotebook.setUserId(i+1);
                sharedNotebook.setNotebookGuid(notebook.guid());
                sharedNotebook.setEmail(QStringLiteral("Fake shared notebook email #") + QString::number(i+1));
                sharedNotebook.setCreationTimestamp(i+1);
                sharedNotebook.setModificationTimestamp(i+1);
                sharedNotebook.setGlobalId(QStringLiteral("Fake shared notebook global id #") + QString::number(i+1));
                sharedNotebook.setUsername(QStringLiteral("Fake shared notebook username #") + QString::number(i+1));
                sharedNotebook.setPrivilegeLevel(1);
                sharedNotebook.setReminderNotifyEmail(true);
                sharedNotebook.setReminderNotifyApp(false);

                notebook.addSharedNotebook(sharedNotebook);
            }

            bool res = localStorageManager.addNotebook(notebook, errorMessage);
            QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
        }

        // 1) Test method listing all notebooks

        QList<Notebook> foundNotebooks = localStorageManager.listAllNotebooks(errorMessage);
        QVERIFY2(!foundNotebooks.isEmpty(), qPrintable(errorMessage.nonLocalizedString()));

        int numFoundNotebooks = foundNotebooks.size();
        if (numFoundNotebooks != numNotebooks) {
            QFAIL(qPrintable(QStringLiteral("Error: number of notebooks in the result of LocalStorageManager::ListAllNotebooks (") +
                             QString::number(numFoundNotebooks) + QStringLiteral(") does not match the original number of added notebooks (") +
                             QString::number(numNotebooks) + QStringLiteral(")")));
        }

        for(int i = 0; i < numFoundNotebooks; ++i)
        {
            const Notebook & foundNotebook = foundNotebooks.at(i);
            if (!notebooks.contains(foundNotebook)) {
                QFAIL("One of notebooks from the result of LocalStorageManager::ListAllNotebooks "
                      "was not found in the list of original notebooks");
            }
        }

#define CHECK_LIST_NOTEBOOKS_BY_FLAG(flag, flag_name, true_condition, false_condition) \
        errorMessage.clear(); \
        foundNotebooks = localStorageManager.listNotebooks(flag, errorMessage); \
        QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage.nonLocalizedString())); \
        \
        for(int i = 0; i < numNotebooks; ++i) \
        { \
            const Notebook & notebook = notebooks.at(i); \
            bool res = foundNotebooks.contains(notebook); \
            if ((true_condition) && !res) { \
                QNWARNING(QStringLiteral("Not found notebook: ") << notebook); \
                QFAIL("One of " flag_name " notebooks was not found by LocalStorageManager::ListNotebooks"); \
            } \
            else if ((false_condition) && res) { \
                QNWARNING(QStringLiteral("Found irrelevant notebook: ") << notebook); \
                QFAIL("LocalStorageManager::ListNotebooks with flag " flag_name " returned incorrect notebook"); \
            } \
        }

        // 2) Test method listing only dirty notebooks
        CHECK_LIST_NOTEBOOKS_BY_FLAG(LocalStorageManager::ListDirty, "dirty", i > 2, i <= 2);

        // 3) Test method listing only local notebooks
        CHECK_LIST_NOTEBOOKS_BY_FLAG(LocalStorageManager::ListLocal, "local", i < 3, i >= 3);

        // 4) Test method listing only notebooks without guid
        CHECK_LIST_NOTEBOOKS_BY_FLAG(LocalStorageManager::ListElementsWithoutGuid, "guidless", i <= 1, i > 1);

        // 5) Test method listing only favorited notebooks
        CHECK_LIST_NOTEBOOKS_BY_FLAG(LocalStorageManager::ListFavoritedElements, "favorited", (i == 0) || (i == 4), (i != 0) && (i != 4));

        // 6) Test method listing dirty favorited notebooks with guid
        CHECK_LIST_NOTEBOOKS_BY_FLAG(LocalStorageManager::ListDirty | LocalStorageManager::ListElementsWithGuid |
                                     LocalStorageManager::ListFavoritedElements,
                                     "dirty, favorited, having guid", i == 4, i != 4);

        // 7) Test method listing local favorited notebooks
        CHECK_LIST_NOTEBOOKS_BY_FLAG(LocalStorageManager::ListLocal | LocalStorageManager::ListFavoritedElements,
                                     "local, favorited", i == 0, i != 0);
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerExpungeNotelessTagsFromLinkedNotebooksTest()
{
    try
    {
        const bool startFromScratch = true;
        const bool overrideLock = false;
        Account account(QStringLiteral("CoreTesterFakeUser"), Account::Type::Local);
        LocalStorageManager localStorageManager(account, startFromScratch, overrideLock);

        LinkedNotebook linkedNotebook;
        linkedNotebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000001"));
        linkedNotebook.setUpdateSequenceNumber(1);
        linkedNotebook.setShareName(QStringLiteral("Linked notebook share name"));
        linkedNotebook.setUsername(QStringLiteral("Linked notebook username"));
        linkedNotebook.setShardId(QStringLiteral("Linked notebook shard id"));
        linkedNotebook.setSharedNotebookGlobalId(QStringLiteral("Linked notebook shared notebook global id"));
        linkedNotebook.setUri(QStringLiteral("Linked notebook uri"));
        linkedNotebook.setNoteStoreUrl(QStringLiteral("Linked notebook note store url"));
        linkedNotebook.setWebApiUrlPrefix(QStringLiteral("Linked notebook web api url prefix"));
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
        bool res = localStorageManager.addLinkedNotebook(linkedNotebook, errorMessage);
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
        for(int i = 0; i < nTags; ++i)
        {
            tags.push_back(Tag());
            Tag & tag = tags.back();

            tag.setGuid(QStringLiteral("00000000-0000-0000-c000-00000000000") + QString::number(i+1));
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

            res = localStorageManager.updateNote(note, /* update resources = */ false, /* update tags = */ true, errorMessage);
            QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));
        }

        errorMessage.clear();
        res = localStorageManager.expungeNote(note, errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        errorMessage.clear();
        res = localStorageManager.expungeNotelessTagsFromLinkedNotebooks(errorMessage);
        QVERIFY2(res == true, qPrintable(errorMessage.nonLocalizedString()));

        QList<Tag> foundTags;
        foundTags.reserve(3);
        errorMessage.clear();
        foundTags = localStorageManager.listAllTags(errorMessage);
        if (foundTags.isEmpty() && !errorMessage.isEmpty()) {
            QFAIL(qPrintable(errorMessage.nonLocalizedString()));
        }

        for(int i = 0; i < nTags; ++i)
        {
            const Tag & tag = tags[i];

            if ((i > 2) && foundTags.contains(tag)) {
                errorMessage.setBase("Found tag from linked notebook which should have been expunged");
                QNWARNING(errorMessage);
                QFAIL(qPrintable(errorMessage.nonLocalizedString()));
            }
            else if ((i <= 2) && !foundTags.contains(tag)) {
                errorMessage.setBase("Could not find tag which should have remained in the local storage");
                QNWARNING(errorMessage);
                QFAIL(qPrintable(errorMessage.nonLocalizedString()));
            }
        }
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncSavedSearchesTest()
{
    int savedSeachAsyncTestsResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        SavedSearchLocalStorageManagerAsyncTester savedSearchAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&savedSearchAsyncTester, QNSIGNAL(SavedSearchLocalStorageManagerAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&savedSearchAsyncTester, QNSIGNAL(SavedSearchLocalStorageManagerAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &savedSearchAsyncTester, SLOT(onInitTestCase()));
        savedSeachAsyncTestsResult = loop.exec();
    }

    if (savedSeachAsyncTestsResult == -1) {
        QFAIL("Internal error: incorrect return status from SavedSearch async tester");
    }
    else if (savedSeachAsyncTestsResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in SavedSearch async tester");
    }
    else if (savedSeachAsyncTestsResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("SavedSearch async tester failed to finish in time");
    }
}

void LocalStorageManagerTester::localStorageManagerAsyncLinkedNotebooksTest()
{
    int linkedNotebookAsyncTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        LinkedNotebookLocalStorageManagerAsyncTester linkedNotebookAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&linkedNotebookAsyncTester, QNSIGNAL(LinkedNotebookLocalStorageManagerAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&linkedNotebookAsyncTester, QNSIGNAL(LinkedNotebookLocalStorageManagerAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &linkedNotebookAsyncTester, SLOT(onInitTestCase()));
        linkedNotebookAsyncTestResult = loop.exec();
    }

    if (linkedNotebookAsyncTestResult == -1) {
        QFAIL("Internal error: incorrect return status from LinkedNotebook async tester");
    }
    else if (linkedNotebookAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in LinkedNotebook async tester");
    }
    else if (linkedNotebookAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("LinkedNotebook async tester failed to finish in time");
    }
}

void LocalStorageManagerTester::localStorageManagerAsyncTagsTest()
{
    int tagAsyncTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        TagLocalStorageManagerAsyncTester tagAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&tagAsyncTester, QNSIGNAL(TagLocalStorageManagerAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&tagAsyncTester, QNSIGNAL(TagLocalStorageManagerAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &tagAsyncTester, SLOT(onInitTestCase()));
        tagAsyncTestResult = loop.exec();
    }

    if (tagAsyncTestResult == -1) {
        QFAIL("Internal error: incorrect return status from Tag async tester");
    }
    else if (tagAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in Tag async tester");
    }
    else if (tagAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Tag async tester failed to finish in time");
    }
}

void LocalStorageManagerTester::localStorageManagerAsyncUsersTest()
{
    int userAsyncTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        UserLocalStorageManagerAsyncTester userAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&userAsyncTester, QNSIGNAL(UserLocalStorageManagerAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&userAsyncTester, QNSIGNAL(UserLocalStorageManagerAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &userAsyncTester, SLOT(onInitTestCase()));
        userAsyncTestResult = loop.exec();
    }

    if (userAsyncTestResult == -1) {
        QFAIL("Internal error: incorrect return status from User async tester");
    }
    else if (userAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in User async tester");
    }
    else if (userAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("User async tester failed to finish in time");
    }
}

void LocalStorageManagerTester::localStorageManagerAsyncNotebooksTest()
{
    int notebookAsyncTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        NotebookLocalStorageManagerAsyncTester notebookAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&notebookAsyncTester, QNSIGNAL(NotebookLocalStorageManagerAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&notebookAsyncTester, QNSIGNAL(NotebookLocalStorageManagerAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &notebookAsyncTester, SLOT(onInitTestCase()));
        notebookAsyncTestResult = loop.exec();
    }

    if (notebookAsyncTestResult == -1) {
        QFAIL("Internal error: incorrect return status from Notebook async tester");
    }
    else if (notebookAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in Notebook async tester");
    }
    else if (notebookAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Notebook async tester failed to finish in time");
    }
}

void LocalStorageManagerTester::localStorageManagerAsyncNotesTest()
{
    int noteAsyncTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        NoteLocalStorageManagerAsyncTester noteAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&noteAsyncTester, QNSIGNAL(NoteLocalStorageManagerAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&noteAsyncTester, QNSIGNAL(NoteLocalStorageManagerAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &noteAsyncTester, SLOT(onInitTestCase()));
        noteAsyncTestResult = loop.exec();
    }

    if (noteAsyncTestResult == -1) {
        QFAIL("Internal error: incorrect return status from Note async tester");
    }
    else if (noteAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in Note async tester");
    }
    else if (noteAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Note async tester failed to finish in time");
    }
}

void LocalStorageManagerTester::localStorageManagerAsyncResourceTest()
{
    int resourceAsyncTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        ResourceLocalStorageManagerAsyncTester resourceAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&resourceAsyncTester, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&resourceAsyncTester, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &resourceAsyncTester, SLOT(onInitTestCase()));
        resourceAsyncTestResult = loop.exec();
    }

    if (resourceAsyncTestResult == -1) {
        QFAIL("Internal error: incorrect return status from Resource async tester");
    }
    else if (resourceAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in Resource async tester");
    }
    else if (resourceAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Resource async tester failed to finish in time");
    }
}

void LocalStorageManagerTester::localStorageCacheManagerTest()
{
    int localStorageCacheAsyncTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        LocalStorageCacheAsyncTester localStorageCacheAsyncTester;

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&localStorageCacheAsyncTester, QNSIGNAL(LocalStorageCacheAsyncTester,success), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&localStorageCacheAsyncTester, QNSIGNAL(LocalStorageCacheAsyncTester,failure,QString), &loop, QNSLOT(EventLoopWithExitStatus,exitAsFailureWithError,QString));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &localStorageCacheAsyncTester, SLOT(onInitTestCase()));
        localStorageCacheAsyncTestResult = loop.exec();
    }

    if (localStorageCacheAsyncTestResult == -1) {
        QFAIL("Internal error: incorrect return status from local storage cache async tester");
    }
    else if (localStorageCacheAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in local storage cache async tester");
    }
    else if (localStorageCacheAsyncTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Local storage cache async tester failed to finish in time");
    }
}


} // namespace test
} // namespace quentier
