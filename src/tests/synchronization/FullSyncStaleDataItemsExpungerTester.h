/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_SYNCHRONIZATION_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_TESTER_H
#define LIB_QUENTIER_TESTS_SYNCHRONIZATION_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_TESTER_H

#include "../../synchronization/FullSyncStaleDataItemsExpunger.h"

namespace quentier::test {

class FullSyncStaleDataItemsExpungerTester : public QObject
{
    Q_OBJECT
public:
    FullSyncStaleDataItemsExpungerTester(QObject * parent = nullptr);
    ~FullSyncStaleDataItemsExpungerTester() override;

private Q_SLOTS:
    void init();
    void cleanup();

    void testEmpty();
    void testNoStaleOrDirtyItems();

    void testOneStaleNotebook();
    void testOneStaleTag();
    void testOneStaleSavedSearch();
    void testOneStaleNote();
    void testOneStaleNotebookAndOneStaleTag();
    void testOneStaleNotebookAndOneStaleSavedSearch();
    void testOneStaleNotebookAndOneStaleNote();
    void testOneStaleTagAndOneStaleSavedSearch();
    void testOneStaleTagAndOneStaleNote();
    void testOneStaleSavedSearchAndOneStaleNote();
    void testOneStaleItemOfEachKind();

    void testSeveralStaleNotebooks();
    void testSeveralStaleTags();
    void testSeveralStaleSavedSearches();
    void testSeveralStaleNotes();
    void testSeveralStaleItemsOfEachKind();

    void testOneDirtyNotebook();
    void testOneDirtyTag();
    void testOneDirtySavedSearch();
    void testOneDirtyNote();
    void testOneDirtyItemOfEachKind();

    void testSeveralDirtyNotebooks();
    void testSeveralDirtyTags();
    void testSeveralDirtySavedSearches();
    void testSeveralDirtyNotes();
    void testSeveralDirtyItemsOfEachKind();

    void testOneStaleNotebookAndOneDirtyNotebook();
    void testOneStaleTagAndOneDirtyTag();
    void testOneStaleSavedSearchAndOneDirtySavedSearch();
    void testOneStaleNoteAndOneDirtyNote();
    void testSeveralStaleNotebooksAndSeveralDirtyNotebooks();
    void testSeveralStaleTagsAndSeveralDirtyTags();
    void testSeveralStaleSavedSearchesAndSeveralDirtySavedSearches();
    void testSeveralStaleNotesAndSeveralDirtyNotes();
    void testSeveralStaleAndDirtyItemsOfEachKind();

    void testDirtyNoteWithStaleNotebook();
    void testDirtyTagWithStaleParentTag();
    void testStaleNoteFromStaleNotebook();

private:
    void setupBaseDataItems();

    void doTest(
        bool useBaseDataItems,
        const QList<qevercloud::Notebook> & nonSyncedNotebooks,
        const QList<qevercloud::Tag> & nonSyncedTags,
        const QList<qevercloud::SavedSearch> & nonSyncedSavedSearches,
        const QList<qevercloud::Note> & nonSyncedNotes);

private:
    Account m_testAccount;
    LocalStorageManagerAsync * m_pLocalStorageManagerAsync = nullptr;
    FullSyncStaleDataItemsExpunger::SyncedGuids m_syncedGuids;

    NotebookSyncCache * m_pNotebookSyncCache = nullptr;
    TagSyncCache * m_pTagSyncCache = nullptr;
    SavedSearchSyncCache * m_pSavedSearchSyncCache = nullptr;

    bool m_detectedTestFailure;
};

} // namespace quentier::test

#endif // LIB_QUENTIER_TESTS_SYNCHRONIZATION_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_TESTER_H
