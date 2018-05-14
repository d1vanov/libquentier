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

    void testSimpleRemoteToLocalFullSync();
    void testRemoteToLocalFullSyncWithLinkedNotebooks();
    void testSimpleIncrementalSyncWithNewRemoteItems();
    void testIncrementalSyncWithNewRemoteItemsWithLinkedNotebooks();
    void testSimpleIncrementalSyncWithModifiedRemoteItems();
    void testIncrementalSyncWithModifiedRemoteItemsWithLinkedNotebooks();
    void testSimpleIncrementalSyncWithModifiedAndNewRemoteItems();
    void testIncrementalSyncWithModifiedAndNewRemoteItemsWithLinkedNotebooks();

private:
    void setUserOwnItemsToRemoteStorage();
    void setLinkedNotebookItemsToRemoteStorage();
    void setNewUserOwnItemsToRemoteStorage();
    void setNewLinkedNotebookItemsToRemoteStorage();
    void setModifiedUserOwnItemsToRemoteStorage();
    void setModifiedLinkedNotebookItemsToRemoteStorage();

    void setNewItemsToLocalStorage();
    void setModifiedRemoteItemsToLocalStorage();
    void copyRemoteItemsToLocalStorage();

    void setRemoteStorageSyncStateToPersistentSyncSettings();

    void checkEventsOrder(const SynchronizationManagerSignalsCatcher & catcher);
    void checkIdentityOfLocalAndRemoteItems();

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

private:
    Account                         m_testAccount;
    QThread *                       m_pLocalStorageManagerThread;
    LocalStorageManagerAsync *      m_pLocalStorageManagerAsync;
    FakeNoteStore *                 m_pFakeNoteStore;
    FakeUserStore *                 m_pFakeUserStore;
    FakeAuthenticationManager *     m_pFakeAuthenticationManager;
    FakeKeychainService *           m_pFakeKeychainService;
    SynchronizationManager *        m_pSynchronizationManager;
    bool                            m_detectedTestFailure;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_SYNCHRONIZATION_TESTER_H
