/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_TESTER_H
#define LIB_QUENTIER_TESTS_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_TESTER_H

#include <QObject>

namespace quentier {
namespace test {

class LocalStorageManagerTester final : public QObject
{
    Q_OBJECT
public:
    LocalStorageManagerTester(QObject * parent = nullptr);
    virtual ~LocalStorageManagerTester();

private Q_SLOTS:
    void init();

    void noteSearchQueryTest();
    void localStorageManagerNoteSearchQueryTest();

    void localStorageManagerIndividualSavedSearchTest();
    void localStorageManagerFindSavedSearchByNameWithDiacriticsTest();
    void localStorageManagerIndividualLinkedNotebookTest();
    void localStorageManagerIndividualTagTest();
    void localStorageManagerFindTagByNameWithDiacriticsTest();
    void localStorageManagerIndividualResourceTest();
    void localStorageManagedIndividualNoteTest();
    void localStorageManagerIndividualNotebookTest();
    void localStorageManagerFindNotebookByNameWithDiacriticsTest();
    void localStorageManagedIndividualUserTest();

    void localStorageManagerSequentialUpdatesTest();
    void localStorageManagerAccountHighUsnTest();
    void localStorageManagerAddNoteWithoutLocalUidTest();
    void localStorageManagerNoteTagIdsComplementTest();

    void localStorageManagerListSavedSearchesTest();
    void localStorageManagerListLinkedNotebooksTest();
    void localStorageManagerListTagsTest();
    void localStorageManagerListTagsWithNoteLocalUidsTest();
    void localStorageManagerListAllSharedNotebooksTest();
    void localStorageManagerListAllTagsPerNoteTest();
    void localStorageManagerListNotesTest();
    void localStorageManagerListNotebooksTest();

    void localStorageManagerExpungeNotelessTagsFromLinkedNotebooksTest();

    void localStorageManagerAsyncSavedSearchesTest();
    void localStorageManagerAsyncLinkedNotebooksTest();
    void localStorageManagerAsyncTagsTest();
    void localStorageManagerAsyncUsersTest();
    void localStorageManagerAsyncNotebooksTest();
    void localStorageManagerAsyncNotesTest();
    void localStorageManagerAsyncResourceTest();
    void localStorageManagerAsyncNoteNotebookAndTagListTrackingTest();

    void localStorageCacheManagerTest();
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_LOCAL_STORAGE_LOCAL_STORAGE_MANAGER_TESTER_H
