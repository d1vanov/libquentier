/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H

#include "FakeAuthenticationManager.h"
#include "FakeKeychainService.h"
#include "FakeNoteStore.h"
#include "FakeUserStore.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/synchronization/ISyncStateStorage.h>
#include <quentier/synchronization/SynchronizationManager.h>
#include <quentier/types/Account.h>

#include <QObject>

#include <memory>

namespace quentier {

class SynchronizationManagerSignalsCatcher;

namespace test {

class SynchronizationTester final : public QObject
{
    Q_OBJECT
public:
    SynchronizationTester(QObject * parent = nullptr);
    ~SynchronizationTester() override;

private Q_SLOTS:
    void init();
    void cleanup();

    void initTestCase();
    void cleanupTestCase();

    void testRemoteToLocalFullSyncWithUserOwnDataOnly();
    void testRemoteToLocalFullSyncWithLinkedNotebooks();

    void testIncrementalSyncWithNewRemoteItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithNewRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithNewRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithModifiedRemoteItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithModifiedRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithModifiedRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithModifiedAndNewRemoteItemsFromUserOwnDataOnly();

    void
    testIncrementalSyncWithModifiedAndNewRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithModifiedAndNewRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithNewLocalItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithNewLocalItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithNewLocalItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithModifiedLocalItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithModifiedLocalItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithModifiedLocalItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithNewAndModifiedLocalItemsFromUserOwnDataOnly();

    void
    testIncrementalSyncWithNewAndModifiedLocalItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithNewAndModifiedLocalItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithNewLocalAndNewRemoteItemsFromUsersOwnDataOnly();

    void
    testIncrementalSyncWithNewLocalAndNewRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithNewLocalAndNewRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void
    testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromUsersOwnDataOnly();

    void
    testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromUsersOwnDataAndLinkedNotebooks();

    void
    testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromUsersOwnDataOnly();

    void
    testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromUsersOwnDataAndLinkedNotebooks();

    void
    testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromUsersOwnDataOnly();

    void
    testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromUsersOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithExpungedRemoteItemsFromUsersOwnDataOnly();
    void testIncrementalSyncWithExpungedRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithExpungedRemoteItemsFromUsersOwnDataAndLinkedNotebooks();

    void
    testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromUserOwnDataOnly();

    void
    testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromLinkedNotebooksOnly();

    void
    testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void
    testIncrementalSyncWithConflictingSavedSearchesFromUserOwnDataOnlyWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingTagsFromUserOwnDataOnlyWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataOnlyWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingNotesFromUserOwnDataOnlyWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingSavedSearchesFromUserOwnDataOnlyWithSameUsn();

    void testIncrementalSyncWithConflictingTagsFromUserOwnDataOnlyWithSameUsn();

    void
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataOnlyWithSameUsn();

    void
    testIncrementalSyncWithConflictingNotesFromUserOwnDataOnlyWithSameUsn();

    void
    testIncrementalSyncWithConflictingTagsFromLinkedNotebooksOnlyWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingNotebooksFromLinkedNotebooksOnlyWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingNotesFromLinkedNotebooksOnlyWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingTagsFromLinkedNotebooksOnlyWithSameUsn();

    void
    testIncrementalSyncWithConflictingNotebooksFromLinkedNotebooksOnlyWithSameUsn();

    void
    testIncrementalSyncWithConflictingNotesFromLinkedNotebooksOnlyWithSameUsn();

    void
    testIncrementalSyncWithConflictingTagsFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingNotesFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn();

    void
    testIncrementalSyncWithConflictingTagsFromUserOwnDataAndLinkedNotebooksWithSameUsn();

    void
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataAndLinkedNotebooksWithSameUsn();

    void
    testIncrementalSyncWithConflictingNotesFromUserOwnDataAndLinkedNotebooksWithSameUsn();

    void
    testIncrementalSyncWithExpungedRemoteLinkedNotebookNotesProducingNotelessTags();

    void testIncrementalSyncWithRateLimitsBreachOnGetUserOwnSyncStateAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetLinkedNotebookSyncStateAttempt();

    void testIncrementalSyncWithRateLimitsBreachOnGetUserOwnSyncChunkAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetLinkedNotebookSyncChunkAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetNewNoteAfterDownloadingUserOwnSyncChunksAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedNoteAfterDownloadingUserOwnSyncChunksAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetNewResourceAfterDownloadingUserOwnSyncChunksAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedResourceAfterDownloadingUserOwnSyncChunksAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetNewNoteAfterDownloadingLinkedNotebookSyncChunksAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedNoteAfterDownloadingLinkedNotebookSyncChunksAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetNewResourceAfterDownloadingLinkedNotebookSyncChunksAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedResourceAfterDownloadingLinkedNotebookSyncChunksAttempt();

    void testIncrementalSyncWithRateLimitsBreachOnCreateSavedSearchAttempt();
    void testIncrementalSyncWithRateLimitsBreachOnUpdateSavedSearchAttempt();

    void testIncrementalSyncWithRateLimitsBreachOnCreateUserOwnTagAttempt();
    void testIncrementalSyncWithRateLimitsBreachOnUpdateUserOwnTagAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnCreateTagInLinkedNotebookAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnUpdateTagInLinkedNotebookAttempt();

    void testIncrementalSyncWithRateLimitsBreachOnCreateNotebookAttempt();
    void testIncrementalSyncWithRateLimitsBreachOnUpdateNotebookAttempt();

    void testIncrementalSyncWithRateLimitsBreachOnCreateUserOwnNoteAttempt();
    void testIncrementalSyncWithRateLimitsBreachOnUpdateUserOwnNoteAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnCreateNoteInLinkedNotebookAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnUpdateNoteInLinkedNotebookAttempt();

    void
    testIncrementalSyncWithRateLimitsBreachOnAuthenticateToLinkedNotebookAttempt();

private:
    void setUserOwnItemsToRemoteStorage();
    void setLinkedNotebookItemsToRemoteStorage();
    void setNewUserOwnItemsToRemoteStorage();
    void setNewLinkedNotebookItemsToRemoteStorage();
    void setNewUserOwnResourcesInExistingNotesToRemoteStorage();
    void setNewResourcesInExistingNotesFromLinkedNotebooksToRemoteStorage();
    void setModifiedUserOwnItemsToRemoteStorage();
    void setModifiedUserOwnResourcesOnlyToRemoteStorage();
    void setModifiedLinkedNotebookItemsToRemoteStorage();
    void setModifiedLinkedNotebookResourcesOnlyToRemoteStorage();
    void setExpungedUserOwnItemsToRemoteStorage();
    void setExpungedLinkedNotebookItemsToRemoteStorage();

    void
    setExpungedLinkedNotebookNotesToRemoteStorageToProduceNotelessLinkedNotebookTags();

    void expungeNotelessLinkedNotebookTagsFromRemoteStorage();

    void setNewUserOwnItemsToLocalStorage();
    void setNewLinkedNotebookItemsToLocalStorage();
    void setModifiedUserOwnItemsToLocalStorage();
    void setModifiedLinkedNotebookItemsToLocalStorage();

    enum class ConflictingItemsUsnOption
    {
        SameUsn = 0,
        LargerRemoteUsn
    };

    friend QDebug & operator<<(
        QDebug & dbg, ConflictingItemsUsnOption option);

    void setConflictingSavedSearchesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption usnOption);

    void setConflictingTagsFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption usnOption);

    void setConflictingNotebooksFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption usnOption);

    void setConflictingNotesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption usnOption);

    void setConflictingTagsFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption usnOption);

    void setConflictingNotebooksFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption usnOption);

    void setConflictingNotesFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption usnOption);

    void setConflictingTagsToLocalAndRemoteStoragesImpl(
        const QStringList & sourceTagGuids,
        ConflictingItemsUsnOption usnOption,
        bool shouldHaveLinkedNotebookGuid);

    void setConflictingNotebooksToLocalAndRemoteStoragesImpl(
        const QStringList & sourceNotebookGuids,
        ConflictingItemsUsnOption usnOption,
        bool shouldHaveLinkedNotebookGuid);

    void setConflictingNotesToLocalAndRemoteStoragesImpl(
        const QStringList & sourceNoteGuids,
        ConflictingItemsUsnOption usnOption);

    void copyRemoteItemsToLocalStorage();

    void setRemoteStorageSyncStateToPersistentSyncSettings();

    void checkProgressNotificationsOrder(
        const SynchronizationManagerSignalsCatcher & catcher);

    void checkIdentityOfLocalAndRemoteItems();
    void checkPersistentSyncState();
    void checkExpectedNamesOfConflictingItemsAfterSync();
    void checkLocalCopiesOfConflictingNotesWereCreated();
    void checkNoConflictingNotesWereCreated();

    void checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        const SynchronizationManagerSignalsCatcher & catcher,
        int numExpectedSyncStateEntries,
        int rateLimitTriggeredSyncStateEntryIndex);

    // List stuff from local storage
    void listSavedSearchesFromLocalStorage(
        qint32 afterUSN,
        QHash<QString, qevercloud::SavedSearch> & savedSearches) const;

    void listTagsFromLocalStorage(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Tag> & tags) const;

    void listNotebooksFromLocalStorage(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Notebook> & notebooks) const;

    void listNotesFromLocalStorage(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Note> & notes) const;

    void listResourcesFromLocalStorage(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Resource> & resources) const;

    void listLinkedNotebooksFromLocalStorage(
        qint32 afterUSN,
        QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks) const;

    // List stuff from fake note store
    void listSavedSearchesFromFakeNoteStore(
        qint32 afterUSN,
        QHash<QString, qevercloud::SavedSearch> & savedSearches) const;

    void listTagsFromFakeNoteStore(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Tag> & tags) const;

    void listNotebooksFromFakeNoteStore(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Notebook> & notebooks) const;

    void listNotesFromFakeNoteStore(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Note> & notes) const;

    void listResourcesFromFakeNoteStore(
        qint32 afterUSN, const QString & linkedNotebookGuid,
        QHash<QString, qevercloud::Resource> & resources) const;

    void listLinkedNotebooksFromFakeNoteStore(
        qint32 afterUSN,
        QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks) const;

    // Print stuff from both local storage and fake note store into a warning
    // level log entry
    void printContentsOfLocalStorageAndFakeNoteStoreToWarnLog(
        const QString & prefix = {}, const QString & linkedNotebookGuid = {});

    void runTest(SynchronizationManagerSignalsCatcher & catcher);

private:
    Account m_testAccount;
    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;

    FakeNoteStorePtr m_pFakeNoteStore;
    FakeUserStorePtr m_pFakeUserStore;
    FakeAuthenticationManagerPtr m_pFakeAuthenticationManager;
    FakeKeychainServicePtr m_pFakeKeychainService;
    ISyncStateStoragePtr m_pSyncStateStorage;

    SynchronizationManager * m_pSynchronizationManager = nullptr;
    bool m_detectedTestFailure = false;

    /**
     * Struct containing a collection of guids of data items (remote or local)
     * which are to be used in incremental sync tests checking how the sync
     * handles modifications or expunging of existing data items on local or
     * remote side.
     *
     * Using the particular set of items to modify/expunge is important for
     * making the tests reproducible.
     */
    struct GuidsOfItemsUsedForSyncTest
    {
        QStringList m_savedSearchGuids;
        QStringList m_linkedNotebookGuids;
        QStringList m_tagGuids;
        QStringList m_notebookGuids;
        QStringList m_noteGuids;
        QStringList m_resourceGuids;
    };

    GuidsOfItemsUsedForSyncTest m_guidsOfUsersOwnRemoteItemsToModify;
    GuidsOfItemsUsedForSyncTest m_guidsOfLinkedNotebookRemoteItemsToModify;

    GuidsOfItemsUsedForSyncTest m_guidsOfUserOwnLocalItemsToModify;
    GuidsOfItemsUsedForSyncTest m_guidsOfLinkedNotebookLocalItemsToModify;

    GuidsOfItemsUsedForSyncTest m_guidsOfUserOwnRemoteItemsToExpunge;
    GuidsOfItemsUsedForSyncTest m_guidsOfLinkedNotebookRemoteItemsToExpunge;

    QHash<QString, QString> m_expectedSavedSearchNamesByGuid;
    QHash<QString, QString> m_expectedTagNamesByGuid;
    QHash<QString, QString> m_expectedNotebookNamesByGuid;
    QHash<QString, QString> m_expectedNoteTitlesByGuid;

    QSet<QString>
        m_guidsOfLinkedNotebookNotesToExpungeToProduceNotelessLinkedNotebookTags;

    QSet<QString> m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H
