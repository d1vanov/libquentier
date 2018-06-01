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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H

#include "FakeNoteStore.h"
#include "FakeUserStore.h"
#include "FakeAuthenticationManager.h"
#include "FakeKeychainService.h"
#include <quentier/utility/Macros.h>
#include <quentier/types/Account.h>
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/synchronization/SynchronizationManager.h>
#include <QObject>
#include <QScopedPointer>
#include <QThread>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(SyncStatePersistenceManager)
QT_FORWARD_DECLARE_CLASS(SynchronizationManagerSignalsCatcher)

namespace test {

class SynchronizationTester: public QObject
{
    Q_OBJECT
public:
    SynchronizationTester(QObject * parent = Q_NULLPTR);
    virtual ~SynchronizationTester();

private Q_SLOTS:
    void init();
    void cleanup();

    void initTestCase();
    void cleanupTestCase();

    void testRemoteToLocalFullSyncWithUserOwnDataOnly();
    void testRemoteToLocalFullSyncWithLinkedNotebooks();

    void testIncrementalSyncWithNewRemoteItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithNewRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithNewRemoteItemsFromUserOwnDataAndLinkedNotebooks();
    void testIncrementalSyncWithModifiedRemoteItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithModifiedRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithModifiedRemoteItemsFromUserOwnDataAndLinkedNotebooks();
    void testIncrementalSyncWithModifiedAndNewRemoteItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithModifiedAndNewRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithModifiedAndNewRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithNewLocalItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithNewLocalItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithNewLocalItemsFromUserOwnDataAndLinkedNotebooks();
    void testIncrementalSyncWithModifiedLocalItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithModifiedLocalItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithModifiedLocalItemsFromUserOwnDataAndLinkedNotebooks();
    void testIncrementalSyncWithNewAndModifiedLocalItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithNewAndModifiedLocalItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithNewAndModifiedLocalItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithNewLocalAndNewRemoteItemsFromUsersOwnDataOnly();
    void testIncrementalSyncWithNewLocalAndNewRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithNewLocalAndNewRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromUsersOwnDataOnly();
    void testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromUsersOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromUsersOwnDataOnly();
    void testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromUsersOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromUsersOwnDataOnly();
    void testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromUsersOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithExpungedRemoteItemsFromUsersOwnDataOnly();
    void testIncrementalSyncWithExpungedRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithExpungedRemoteItemsFromUsersOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromUserOwnDataOnly();
    void testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromLinkedNotebooksOnly();
    void testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromUserOwnDataAndLinkedNotebooks();

    void testIncrementalSyncWithConflictingSavedSearchesFromUserOwnDataOnlyWithLargerRemoteUsn();
    void testIncrementalSyncWithConflictingTagsFromUserOwnDataOnlyWithLargerRemoteUsn();
    void testIncrementalSyncWithConflictingNotebooksFromUserOwnDataOnlyWithLargerRemoteUsn();
    void testIncrementalSyncWithConflictingNotesFromUserOwnDataOnlyWithLargerRemoteUsn();

    void testIncrementalSyncWithConflictingSavedSearchesFromUserOwnDataOnlyWithSameUsn();
    void testIncrementalSyncWithConflictingTagsFromUserOwnDataOnlyWithSameUsn();
    void testIncrementalSyncWithConflictingNotebooksFromUserOwnDataOnlyWithSameUsn();
    void testIncrementalSyncWithConflictingNotesFromUserOwnDataOnlyWithSameUsn();

    void testIncrementalSyncWithConflictingTagsFromLinkedNotebooksOnlyWithLargerRemoteUsn();
    void testIncrementalSyncWithConflictingNotebooksFromLinkedNotebooksOnlyWithLargerRemoteUsn();
    void testIncrementalSyncWithConflictingNotesFromLinkedNotebooksOnlyWithLargerRemoteUsn();

    void testIncrementalSyncWithConflictingTagsFromLinkedNotebooksOnlyWithSameUsn();
    void testIncrementalSyncWithConflictingNotebooksFromLinkedNotebooksOnlyWithSameUsn();
    void testIncrementalSyncWithConflictingNotesFromLinkedNotebooksOnlyWithSameUsn();

    void testIncrementalSyncWithConflictingTagsFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn();
    void testIncrementalSyncWithConflictingNotebooksFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn();
    void testIncrementalSyncWithConflictingNotesFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn();

    void testIncrementalSyncWithConflictingTagsFromUserOwnDataAndLinkedNotebooksWithSameUsn();
    void testIncrementalSyncWithConflictingNotebooksFromUserOwnDataAndLinkedNotebooksWithSameUsn();
    void testIncrementalSyncWithConflictingNotesFromUserOwnDataAndLinkedNotebooksWithSameUsn();

    void testIncrementalSyncWithExpungedRemoteLinkedNotebookNotesProducingNotelessTags();

    void testIncrementalSyncWithRateLimitsBreachOnGetUserOwnSyncStateAttempt();
    void testIncrementalSyncWithRateLimitsBreachOnGetLinkedNotebookSyncStateAttempt();
    void testIncrementalSyncWithRateLimitsBreachOnGetUserOwnSyncChunkAttempt();

private:
    void setUserOwnItemsToRemoteStorage();
    void setLinkedNotebookItemsToRemoteStorage();
    void setNewUserOwnItemsToRemoteStorage();
    void setNewLinkedNotebookItemsToRemoteStorage();
    void setModifiedUserOwnItemsToRemoteStorage();
    void setModifiedLinkedNotebookItemsToRemoteStorage();
    void setExpungedUserOwnItemsToRemoteStorage();
    void setExpungedLinkedNotebookItemsToRemoteStorage();
    void setExpungedLinkedNotebookNotesToRemoteStorageToProduceNotelessLinkedNotebookTags();
    void expungeNotelessLinkedNotebookTagsFromRemoteStorage();

    void setNewUserOwnItemsToLocalStorage();
    void setNewLinkedNotebookItemsToLocalStorage();
    void setModifiedUserOwnItemsToLocalStorage();
    void setModifiedLinkedNotebookItemsToLocalStorage();

    struct ConflictingItemsUsnOption
    {
        enum type
        {
            SameUsn = 0,
            LargerRemoteUsn
        };
    };

    void setConflictingSavedSearchesFromUserOwnDataToLocalAndRemoteStorages(const ConflictingItemsUsnOption::type usnOption);
    void setConflictingTagsFromUserOwnDataToLocalAndRemoteStorages(const ConflictingItemsUsnOption::type usnOption);
    void setConflictingNotebooksFromUserOwnDataToLocalAndRemoteStorages(const ConflictingItemsUsnOption::type usnOption);
    void setConflictingNotesFromUserOwnDataToLocalAndRemoteStorages(const ConflictingItemsUsnOption::type usnOption);

    void setConflictingTagsFromLinkedNotebooksToLocalAndRemoteStorages(const ConflictingItemsUsnOption::type usnOption);
    void setConflictingNotebooksFromLinkedNotebooksToLocalAndRemoteStorages(const ConflictingItemsUsnOption::type usnOption);
    void setConflictingNotesFromLinkedNotebooksToLocalAndRemoteStorages(const ConflictingItemsUsnOption::type usnOption);

    void setConflictingTagsToLocalAndRemoteStoragesImpl(const QStringList & sourceTagGuids,
                                                        const ConflictingItemsUsnOption::type usnOption,
                                                        const bool shouldHaveLinkedNotebookGuid);
    void setConflictingNotebooksToLocalAndRemoteStoragesImpl(const QStringList & sourceNotebookGuids,
                                                             const ConflictingItemsUsnOption::type usnOption,
                                                             const bool shouldHaveLinkedNotebookGuid);
    void setConflictingNotesToLocalAndRemoteStoragesImpl(const QStringList & sourceNoteGuids,
                                                         const ConflictingItemsUsnOption::type usnOption);

    void copyRemoteItemsToLocalStorage();

    void setRemoteStorageSyncStateToPersistentSyncSettings();

    void checkProgressNotificationsOrder(const SynchronizationManagerSignalsCatcher & catcher);
    void checkIdentityOfLocalAndRemoteItems();
    void checkPersistentSyncState();
    void checkExpectedNamesOfConflictingItemsAfterSync();
    void checkLocalCopiesOfConflictingNotesWereCreated();
    void checkNoConflictingNotesWereCreated();

    void checkSyncStatePersistedRightAfterAPIRateLimitBreach(const SynchronizationManagerSignalsCatcher & catcher);

    void listSavedSearchesFromLocalStorage(const qint32 afterUSN,
                                           QHash<QString, qevercloud::SavedSearch> & savedSearches) const;

    void listTagsFromLocalStorage(const qint32 afterUSN, const QString & linkedNotebookGuid,
                                  QHash<QString, qevercloud::Tag> & tags) const;

    void listNotebooksFromLocalStorage(const qint32 afterUSN, const QString & linkedNotebookGuid,
                                       QHash<QString, qevercloud::Notebook> & notebooks) const;

    void listNotesFromLocalStorage(const qint32 afterUSN, const QString & linkedNotebookGuid,
                                   QHash<QString, qevercloud::Note> & notes) const;

    void listLinkedNotebooksFromLocalStorage(const qint32 afterUSN,
                                             QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks) const;

    void runTest(SynchronizationManagerSignalsCatcher & catcher);

private:
    Account                         m_testAccount;
    QThread *                       m_pLocalStorageManagerThread;
    LocalStorageManagerAsync *      m_pLocalStorageManagerAsync;
    FakeNoteStore *                 m_pFakeNoteStore;
    FakeUserStore *                 m_pFakeUserStore;
    FakeAuthenticationManager *     m_pFakeAuthenticationManager;
    FakeKeychainService *           m_pFakeKeychainService;
    SyncStatePersistenceManager *   m_pSyncStatePersistenceManager;
    SynchronizationManager *        m_pSynchronizationManager;
    bool                            m_detectedTestFailure;

    /**
     * Struct containing a collection of guids of data items (remote or local)
     * which are to be used in incremental sync tests checking how the sync
     * handles modifications or expunging of existing data items on local or remote side.
     * Using the particular set of items to modify/expunge is important for making
     * the tests reproducible.
     */
    struct GuidsOfItemsUsedForSyncTest
    {
        QStringList     m_savedSearchGuids;
        QStringList     m_linkedNotebookGuids;
        QStringList     m_tagGuids;
        QStringList     m_notebookGuids;
        QStringList     m_noteGuids;
        QStringList     m_resourceGuids;
    };

    GuidsOfItemsUsedForSyncTest     m_guidsOfUsersOwnRemoteItemsToModify;
    GuidsOfItemsUsedForSyncTest     m_guidsOfLinkedNotebookRemoteItemsToModify;

    GuidsOfItemsUsedForSyncTest     m_guidsOfUserOwnLocalItemsToModify;
    GuidsOfItemsUsedForSyncTest     m_guidsOfLinkedNotebookLocalItemsToModify;

    GuidsOfItemsUsedForSyncTest     m_guidsOfUserOwnRemoteItemsToExpunge;
    GuidsOfItemsUsedForSyncTest     m_guidsOfLinkedNotebookRemoteItemsToExpunge;

    QHash<QString, QString>         m_expectedSavedSearchNamesByGuid;
    QHash<QString, QString>         m_expectedTagNamesByGuid;
    QHash<QString, QString>         m_expectedNotebookNamesByGuid;
    QHash<QString, QString>         m_expectedNoteTitlesByGuid;

    QSet<QString>                   m_guidsOfLinkedNotebookNotesToExpungeToProduceNotelessLinkedNotebookTags;
    QSet<QString>                   m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H
