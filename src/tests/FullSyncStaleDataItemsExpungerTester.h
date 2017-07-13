/*
 * Copyright 2017 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_TESTS_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_TESTER_H
#define LIB_QUENTIER_TESTS_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_TESTER_H

#include "../synchronization/FullSyncStaleDataItemsExpunger.h"

namespace quentier {
namespace test {

class FullSyncStaleDataItemsExpungerTester: public QObject
{
    Q_OBJECT
public:
    FullSyncStaleDataItemsExpungerTester(QObject * parent = Q_NULLPTR);
    virtual ~FullSyncStaleDataItemsExpungerTester();

private Q_SLOTS:
    void init();
    void cleanup();

    void testEmpty();

private:
    void setupBaseDataItems();

    void doTest(const bool useBaseDataItems,
                const QList<Notebook> & extraNotebooks,
                const QList<Tag> & extraTags,
                const QList<SavedSearch> & extraSavedSearches,
                const QList<Note> & extraNotes);

private:
    Account                     m_testAccount;
    LocalStorageManagerAsync *  m_pLocalStorageManagerAsync;
    FullSyncStaleDataItemsExpunger::SyncedGuids     m_syncedGuids;

    QList<NotebookSyncCache*>     m_notebookSyncCaches;
    QList<TagSyncCache*>          m_tagSyncCaches;
    SavedSearchSyncCache *        m_pSavedSearchSyncCache;

    bool    m_detectedTestFailure;
};

} // namespace test
} // namespace quentier

#endif // LIB_QUENTIER_TESTS_FULL_SYNC_STALE_DATA_ITEMS_EXPUNGER_TESTER_H
