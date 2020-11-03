/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#include "FullSyncStaleDataItemsExpungerTester.h"

#include "../../synchronization/NotebookSyncCache.h"
#include "../../synchronization/SavedSearchSyncCache.h"
#include "../../synchronization/TagSyncCache.h"

#include <quentier/utility/Compat.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/UidGenerator.h>

#include <QTimer>
#include <QtTest/QTest>

// 10 minutes should be enough
#define MAX_ALLOWED_MILLISECONDS 600000

// Local uids of base data items' notebooks
#define FIRST_NOTEBOOK_LOCAL_UID                                               \
    QStringLiteral(                                                            \
        "68b6df59-5e35-4850-a972-b5493dfead8a") // FIRST_NOTEBOOK_LOCAL_UID

#define SECOND_NOTEBOOK_LOCAL_UID                                              \
    QStringLiteral(                                                            \
        "b5f6eb38-428b-4964-b4ca-b72007e11c4f") // SECOND_NOTEBOOK_LOCAL_UID

#define THIRD_NOTEBOOK_LOCAL_UID                                               \
    QStringLiteral(                                                            \
        "7d919756-e83d-4a02-b94f-f6eab8e12885") // THIRD_NOTEBOOK_LOCAL_UID

namespace quentier {
namespace test {

template <class T>
struct CompareItemByLocalUid
{
    CompareItemByLocalUid(QString targetLocalUid) :
        m_targetLocalUid(std::move(targetLocalUid))
    {}

    bool operator()(const T & item) const
    {
        return item.localUid() == m_targetLocalUid;
    }

    QString m_targetLocalUid;
};

template <class T>
struct CompareItemByGuid
{
    CompareItemByGuid(QString targetGuid) : m_targetGuid(std::move(targetGuid))
    {}

    bool operator()(const T & item) const
    {
        return (item.hasGuid() && (item.guid() == m_targetGuid));
    }

    QString m_targetGuid;
};

FullSyncStaleDataItemsExpungerTester::FullSyncStaleDataItemsExpungerTester(
    QObject * parent) :
    QObject(parent),
    m_testAccount(
        QStringLiteral("FullSyncStaleDataItemsExpungerTesterFakeUser"),
        Account::Type::Evernote, qevercloud::UserID(1))
{}

FullSyncStaleDataItemsExpungerTester::~FullSyncStaleDataItemsExpungerTester() {}

void FullSyncStaleDataItemsExpungerTester::init()
{
    m_testAccount = Account(
        m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1);

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(m_testAccount, startupOptions, this);

    m_pLocalStorageManagerAsync->init();

    m_pNotebookSyncCache =
        new NotebookSyncCache(*m_pLocalStorageManagerAsync, QString(), this);

    m_pTagSyncCache =
        new TagSyncCache(*m_pLocalStorageManagerAsync, QString(), this);

    m_pSavedSearchSyncCache =
        new SavedSearchSyncCache(*m_pLocalStorageManagerAsync, this);
}

void FullSyncStaleDataItemsExpungerTester::cleanup()
{
    delete m_pLocalStorageManagerAsync;
    m_pLocalStorageManagerAsync = nullptr;

    delete m_pNotebookSyncCache;
    m_pNotebookSyncCache = nullptr;

    delete m_pTagSyncCache;
    m_pTagSyncCache = nullptr;

    delete m_pSavedSearchSyncCache;
    m_pSavedSearchSyncCache = nullptr;

    m_syncedGuids.m_syncedNotebookGuids.clear();
    m_syncedGuids.m_syncedTagGuids.clear();
    m_syncedGuids.m_syncedNoteGuids.clear();
    m_syncedGuids.m_syncedSavedSearchGuids.clear();
}

void FullSyncStaleDataItemsExpungerTester::testEmpty()
{
    doTest(
        /* use base data items = */ false, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testNoStaleOrDirtyItems()
{
    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNotebook()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleTag()
{
    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(100);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    QList<Tag> nonSyncedTags;
    nonSyncedTags << staleTag;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleSavedSearch()
{
    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(100);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << staleSearch;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNote()
{
    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(100);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << staleNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNotebookAndOneStaleTag()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(101);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<Tag> nonSyncedTags;
    nonSyncedTags << staleTag;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleNotebookAndOneStaleSavedSearch()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(101);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << staleSearch;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNotebookAndOneStaleNote()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(100);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << staleNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleTagAndOneStaleSavedSearch()
{
    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(100);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(101);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    QList<Tag> nonSyncedTags;
    nonSyncedTags << staleTag;

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << staleSearch;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleTagAndOneStaleNote()
{
    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(100);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(101);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    QList<Tag> nonSyncedTags;
    nonSyncedTags << staleTag;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << staleNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleSavedSearchAndOneStaleNote()
{
    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(100);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(101);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << staleSearch;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << staleNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleItemOfEachKind()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(101);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(100);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(100);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<Tag> nonSyncedTags;
    nonSyncedTags << staleTag;

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << staleSearch;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << staleNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleNotebooks()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNumber(101);
    secondStaleNotebook.setLocal(false);
    secondStaleNotebook.setDirty(false);

    Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNumber(102);
    thirdStaleNotebook.setLocal(false);
    thirdStaleNotebook.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleTags()
{
    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(100);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNumber(101);
    secondStaleTag.setLocal(false);
    secondStaleTag.setDirty(false);

    Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNumber(102);
    thirdStaleTag.setLocal(false);
    thirdStaleTag.setDirty(false);

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleSavedSearches()
{
    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(100);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNumber(102);
    secondStaleSavedSearch.setLocal(false);
    secondStaleSavedSearch.setDirty(false);

    SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNumber(103);
    thirdStaleSavedSearch.setLocal(false);
    thirdStaleSavedSearch.setDirty(false);

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleNotes()
{
    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(100);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNumber(101);
    secondStaleNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondStaleNote.setLocal(false);
    secondStaleNote.setDirty(false);

    Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNumber(103);
    thirdStaleNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdStaleNote.setLocal(false);
    thirdStaleNote.setDirty(false);

    QList<Note> nonSyncedNotes;
    nonSyncedNotes.reserve(3);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << secondStaleNote;
    nonSyncedNotes << thirdStaleNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleItemsOfEachKind()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNumber(101);
    secondStaleNotebook.setLocal(false);
    secondStaleNotebook.setDirty(false);

    Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNumber(102);
    thirdStaleNotebook.setLocal(false);
    thirdStaleNotebook.setDirty(false);

    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(103);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNumber(104);
    secondStaleTag.setLocal(false);
    secondStaleTag.setDirty(false);

    Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNumber(105);
    thirdStaleTag.setLocal(false);
    thirdStaleTag.setDirty(false);

    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(106);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNumber(107);
    secondStaleSavedSearch.setLocal(false);
    secondStaleSavedSearch.setDirty(false);

    SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNumber(108);
    thirdStaleSavedSearch.setLocal(false);
    thirdStaleSavedSearch.setDirty(false);

    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(109);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNumber(110);
    secondStaleNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondStaleNote.setLocal(false);
    secondStaleNote.setDirty(false);

    Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNumber(111);
    thirdStaleNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdStaleNote.setLocal(false);
    thirdStaleNote.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes.reserve(3);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << secondStaleNote;
    nonSyncedNotes << thirdStaleNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtyNotebook()
{
    Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNumber(100);
    dirtyNotebook.setLocal(false);
    dirtyNotebook.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << dirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtyTag()
{
    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(100);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);

    QList<Tag> nonSyncedTags;
    nonSyncedTags << dirtyTag;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtySavedSearch()
{
    SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNumber(100);
    dirtySavedSearch.setLocal(false);
    dirtySavedSearch.setDirty(true);

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << dirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtyNote()
{
    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(100);
    dirtyNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtyItemOfEachKind()
{
    Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNumber(100);
    dirtyNotebook.setLocal(false);
    dirtyNotebook.setDirty(true);

    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(101);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);

    SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNumber(102);
    dirtySavedSearch.setLocal(false);
    dirtySavedSearch.setDirty(true);

    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(103);
    dirtyNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << dirtyNotebook;

    QList<Tag> nonSyncedTags;
    nonSyncedTags << dirtyTag;

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << dirtySavedSearch;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyNotebooks()
{
    Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNumber(100);
    dirtyNotebook.setLocal(false);
    dirtyNotebook.setDirty(true);

    Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNumber(101);
    secondDirtyNotebook.setLocal(false);
    secondDirtyNotebook.setDirty(true);

    Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNumber(102);
    thirdDirtyNotebook.setLocal(false);
    thirdDirtyNotebook.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyTags()
{
    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(100);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);

    Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNumber(101);
    secondDirtyTag.setLocal(false);
    secondDirtyTag.setDirty(true);

    Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNumber(102);
    thirdDirtyTag.setLocal(false);
    thirdDirtyTag.setDirty(true);

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtySavedSearches()
{
    SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNumber(100);
    dirtySavedSearch.setLocal(false);
    dirtySavedSearch.setDirty(true);

    SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNumber(101);
    secondDirtySavedSearch.setLocal(false);
    secondDirtySavedSearch.setDirty(true);

    SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNumber(102);
    thirdDirtySavedSearch.setLocal(false);
    thirdDirtySavedSearch.setDirty(true);

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyNotes()
{
    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(100);
    dirtyNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNumber(101);
    secondDirtyNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondDirtyNote.setLocal(false);
    secondDirtyNote.setDirty(true);

    Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNumber(102);
    thirdDirtyNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdDirtyNote.setLocal(false);
    thirdDirtyNote.setDirty(true);

    QList<Note> nonSyncedNotes;
    nonSyncedNotes.reserve(3);
    nonSyncedNotes << dirtyNote;
    nonSyncedNotes << secondDirtyNote;
    nonSyncedNotes << thirdDirtyNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyItemsOfEachKind()
{
    Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNumber(100);
    dirtyNotebook.setLocal(false);
    dirtyNotebook.setDirty(true);

    Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNumber(101);
    secondDirtyNotebook.setLocal(false);
    secondDirtyNotebook.setDirty(true);

    Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNumber(102);
    thirdDirtyNotebook.setLocal(false);
    thirdDirtyNotebook.setDirty(true);

    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(103);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);

    Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNumber(104);
    secondDirtyTag.setLocal(false);
    secondDirtyTag.setDirty(true);

    Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNumber(105);
    thirdDirtyTag.setLocal(false);
    thirdDirtyTag.setDirty(true);

    SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNumber(106);
    dirtySavedSearch.setLocal(false);
    dirtySavedSearch.setDirty(true);

    SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNumber(107);
    secondDirtySavedSearch.setLocal(false);
    secondDirtySavedSearch.setDirty(true);

    SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNumber(108);
    thirdDirtySavedSearch.setLocal(false);
    thirdDirtySavedSearch.setDirty(true);

    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(109);
    dirtyNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNumber(110);
    secondDirtyNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondDirtyNote.setLocal(false);
    secondDirtyNote.setDirty(true);

    Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNumber(111);
    thirdDirtyNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdDirtyNote.setLocal(false);
    thirdDirtyNote.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes.reserve(3);
    nonSyncedNotes << dirtyNote;
    nonSyncedNotes << secondDirtyNote;
    nonSyncedNotes << thirdDirtyNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleNotebookAndOneDirtyNotebook()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNumber(101);
    dirtyNotebook.setLocal(false);
    dirtyNotebook.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(2);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << dirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleTagAndOneDirtyTag()
{
    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(100);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(101);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(2);
    nonSyncedTags << staleTag;
    nonSyncedTags << dirtyTag;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleSavedSearchAndOneDirtySavedSearch()
{
    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(100);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNumber(101);
    dirtySavedSearch.setLocal(false);
    dirtySavedSearch.setDirty(true);

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(2);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << dirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNoteAndOneDirtyNote()
{
    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(100);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(101);
    dirtyNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    QList<Note> nonSyncedNotes;
    nonSyncedNotes.reserve(2);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleNotebooksAndSeveralDirtyNotebooks()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNumber(101);
    secondStaleNotebook.setLocal(false);
    secondStaleNotebook.setDirty(false);

    Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNumber(102);
    thirdStaleNotebook.setLocal(false);
    thirdStaleNotebook.setDirty(false);

    Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNumber(103);
    dirtyNotebook.setLocal(false);
    dirtyNotebook.setDirty(true);

    Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNumber(104);
    secondDirtyNotebook.setLocal(false);
    secondDirtyNotebook.setDirty(true);

    Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNumber(105);
    thirdDirtyNotebook.setLocal(false);
    thirdDirtyNotebook.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(6);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleTagsAndSeveralDirtyTags()
{
    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(100);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNumber(101);
    secondStaleTag.setLocal(false);
    secondStaleTag.setDirty(false);

    Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNumber(102);
    thirdStaleTag.setLocal(false);
    thirdStaleTag.setDirty(false);

    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(103);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);

    Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNumber(104);
    secondDirtyTag.setLocal(false);
    secondDirtyTag.setDirty(true);

    Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNumber(105);
    thirdDirtyTag.setLocal(false);
    thirdDirtyTag.setDirty(true);

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(6);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleSavedSearchesAndSeveralDirtySavedSearches()
{
    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(100);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNumber(101);
    secondStaleSavedSearch.setLocal(false);
    secondStaleSavedSearch.setDirty(false);

    SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNumber(102);
    thirdStaleSavedSearch.setLocal(false);
    thirdStaleSavedSearch.setDirty(false);

    SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNumber(103);
    dirtySavedSearch.setLocal(false);
    dirtySavedSearch.setDirty(true);

    SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNumber(104);
    secondDirtySavedSearch.setLocal(false);
    secondDirtySavedSearch.setDirty(true);

    SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNumber(105);
    thirdDirtySavedSearch.setLocal(false);
    thirdDirtySavedSearch.setDirty(true);

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(6);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        nonSyncedSavedSearches, QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleNotesAndSeveralDirtyNotes()
{
    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(100);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNumber(101);
    secondStaleNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondStaleNote.setLocal(false);
    secondStaleNote.setDirty(false);

    Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNumber(103);
    thirdStaleNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdStaleNote.setLocal(false);
    thirdStaleNote.setDirty(false);

    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(100);
    dirtyNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNumber(101);
    secondDirtyNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondDirtyNote.setLocal(false);
    secondDirtyNote.setDirty(true);

    Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNumber(102);
    thirdDirtyNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdDirtyNote.setLocal(false);
    thirdDirtyNote.setDirty(true);

    QList<Note> nonSyncedNotes;
    nonSyncedNotes.reserve(6);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << secondStaleNote;
    nonSyncedNotes << thirdStaleNote;
    nonSyncedNotes << dirtyNote;
    nonSyncedNotes << secondDirtyNote;
    nonSyncedNotes << thirdDirtyNote;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleAndDirtyItemsOfEachKind()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNumber(101);
    secondStaleNotebook.setLocal(false);
    secondStaleNotebook.setDirty(false);

    Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNumber(102);
    thirdStaleNotebook.setLocal(false);
    thirdStaleNotebook.setDirty(false);

    Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNumber(103);
    dirtyNotebook.setLocal(false);
    dirtyNotebook.setDirty(true);

    Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNumber(104);
    secondDirtyNotebook.setLocal(false);
    secondDirtyNotebook.setDirty(true);

    Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNumber(105);
    thirdDirtyNotebook.setLocal(false);
    thirdDirtyNotebook.setDirty(true);

    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(106);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNumber(107);
    secondStaleTag.setLocal(false);
    secondStaleTag.setDirty(false);

    Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNumber(108);
    thirdStaleTag.setLocal(false);
    thirdStaleTag.setDirty(false);

    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(109);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);

    Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNumber(110);
    secondDirtyTag.setLocal(false);
    secondDirtyTag.setDirty(true);

    Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNumber(111);
    thirdDirtyTag.setLocal(false);
    thirdDirtyTag.setDirty(true);

    SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNumber(112);
    staleSearch.setLocal(false);
    staleSearch.setDirty(false);

    SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNumber(113);
    secondStaleSavedSearch.setLocal(false);
    secondStaleSavedSearch.setDirty(false);

    SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNumber(114);
    thirdStaleSavedSearch.setLocal(false);
    thirdStaleSavedSearch.setDirty(false);

    SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNumber(115);
    dirtySavedSearch.setLocal(false);
    dirtySavedSearch.setDirty(true);

    SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNumber(116);
    secondDirtySavedSearch.setLocal(false);
    secondDirtySavedSearch.setDirty(true);

    SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNumber(117);
    thirdDirtySavedSearch.setLocal(false);
    thirdDirtySavedSearch.setDirty(true);

    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(118);
    staleNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNumber(119);
    secondStaleNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondStaleNote.setLocal(false);
    secondStaleNote.setDirty(false);

    Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNumber(120);
    thirdStaleNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdStaleNote.setLocal(false);
    thirdStaleNote.setDirty(false);

    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(121);
    dirtyNote.setNotebookLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNumber(122);
    secondDirtyNote.setNotebookLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondDirtyNote.setLocal(false);
    secondDirtyNote.setDirty(true);

    Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNumber(123);
    thirdDirtyNote.setNotebookLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdDirtyNote.setLocal(false);
    thirdDirtyNote.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(6);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(6);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    QList<SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(6);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes.reserve(6);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << secondStaleNote;
    nonSyncedNotes << thirdStaleNote;
    nonSyncedNotes << dirtyNote;
    nonSyncedNotes << secondDirtyNote;
    nonSyncedNotes << thirdDirtyNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testDirtyNoteWithStaleNotebook()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNumber(100);
    dirtyNote.setNotebookLocalUid(staleNotebook.localUid());
    dirtyNote.setLocal(false);
    dirtyNote.setDirty(true);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testDirtyTagWithStaleParentTag()
{
    Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNumber(100);
    staleTag.setLocal(false);
    staleTag.setDirty(false);

    Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNumber(101);
    dirtyTag.setLocal(false);
    dirtyTag.setDirty(true);
    dirtyTag.setParentGuid(staleTag.guid());
    dirtyTag.setParentLocalUid(staleTag.localUid());

    QList<Tag> nonSyncedTags;
    nonSyncedTags.reserve(2);
    nonSyncedTags << staleTag;
    nonSyncedTags << dirtyTag;

    doTest(
        /* use base data items = */ true, QList<Notebook>(), nonSyncedTags,
        QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::testStaleNoteFromStaleNotebook()
{
    Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNumber(100);
    staleNotebook.setLocal(false);
    staleNotebook.setDirty(false);

    Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNumber(100);
    staleNote.setNotebookLocalUid(staleNotebook.localUid());
    staleNote.setLocal(false);
    staleNote.setDirty(false);

    QList<Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<Note> nonSyncedNotes;
    nonSyncedNotes << staleNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, QList<Tag>(),
        QList<SavedSearch>(), nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::setupBaseDataItems()
{
    if (Q_UNLIKELY(!m_pLocalStorageManagerAsync)) {
        QFAIL(
            "Detected null pointer to LocalStorageManagerAsync while trying "
            "to set up the base data items");
        return;
    }

    LocalStorageManager * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL(
            "Detected null pointer to LocalStorageManager while trying "
            "to set up the base data items");
        return;
    }

    Notebook firstNotebook;
    firstNotebook.setLocalUid(FIRST_NOTEBOOK_LOCAL_UID);
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setName(QStringLiteral("First notebook"));
    firstNotebook.setUpdateSequenceNumber(42);
    firstNotebook.setLocal(false);
    firstNotebook.setDirty(false);

    Notebook secondNotebook;
    secondNotebook.setLocalUid(SECOND_NOTEBOOK_LOCAL_UID);
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second notebook"));
    secondNotebook.setUpdateSequenceNumber(43);
    secondNotebook.setLocal(false);
    secondNotebook.setDirty(false);

    Notebook thirdNotebook;
    thirdNotebook.setLocalUid(THIRD_NOTEBOOK_LOCAL_UID);
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setName(QStringLiteral("Third notebook"));
    thirdNotebook.setUpdateSequenceNumber(44);
    thirdNotebook.setLocal(false);
    thirdNotebook.setDirty(false);

    Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First tag"));
    firstTag.setUpdateSequenceNumber(45);
    firstTag.setLocal(false);
    firstTag.setDirty(false);

    Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second tag"));
    secondTag.setUpdateSequenceNumber(46);
    secondTag.setLocal(false);
    secondTag.setDirty(false);

    Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third tag"));
    thirdTag.setUpdateSequenceNumber(47);
    thirdTag.setLocal(false);
    thirdTag.setDirty(false);

    Tag fourthTag;
    fourthTag.setGuid(UidGenerator::Generate());
    fourthTag.setName(QStringLiteral("Fourth tag"));
    fourthTag.setUpdateSequenceNumber(48);
    fourthTag.setLocal(false);
    fourthTag.setDirty(false);
    fourthTag.setParentGuid(secondTag.guid());
    fourthTag.setParentLocalUid(secondTag.localUid());

    SavedSearch firstSearch;
    firstSearch.setGuid(UidGenerator::Generate());
    firstSearch.setName(QStringLiteral("First search"));
    firstSearch.setQuery(QStringLiteral("First search query"));
    firstSearch.setUpdateSequenceNumber(49);
    firstSearch.setLocal(false);
    firstSearch.setDirty(false);

    SavedSearch secondSearch;
    secondSearch.setGuid(UidGenerator::Generate());
    secondSearch.setName(QStringLiteral("Second search"));
    secondSearch.setQuery(QStringLiteral("Second search query"));
    secondSearch.setUpdateSequenceNumber(50);
    secondSearch.setLocal(false);
    secondSearch.setDirty(false);

    Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setTitle(QStringLiteral("First note"));

    firstNote.setContent(
        QStringLiteral("<en-note><h1>First note content</h1></en-note>"));

    firstNote.setUpdateSequenceNumber(51);
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setNotebookLocalUid(firstNotebook.localUid());
    firstNote.setLocal(false);
    firstNote.setDirty(false);

    Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setTitle(QStringLiteral("Second note"));

    secondNote.setContent(
        QStringLiteral("<en-note><h1>Second note content</h1></en-note>"));

    secondNote.setUpdateSequenceNumber(52);
    secondNote.setNotebookGuid(firstNotebook.guid());
    secondNote.setNotebookLocalUid(firstNotebook.localUid());
    secondNote.setLocal(false);
    secondNote.setDirty(false);

    Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setTitle(QStringLiteral("Third note"));

    thirdNote.setContent(
        QStringLiteral("<en-note><h1>Third note content</h1></en-note>"));

    thirdNote.setUpdateSequenceNumber(53);
    thirdNote.setNotebookGuid(firstNotebook.guid());
    thirdNote.setNotebookLocalUid(firstNotebook.localUid());
    thirdNote.addTagGuid(firstTag.guid());
    thirdNote.addTagGuid(secondTag.guid());
    thirdNote.addTagLocalUid(firstTag.localUid());
    thirdNote.addTagLocalUid(secondTag.localUid());
    thirdNote.setLocal(false);
    thirdNote.setDirty(false);

    Note fourthNote;
    fourthNote.setGuid(UidGenerator::Generate());
    fourthNote.setTitle(QStringLiteral("Fourth note"));

    fourthNote.setContent(
        QStringLiteral("<en-note><h1>Fourth note content</h1></en-note>"));

    fourthNote.setUpdateSequenceNumber(54);
    fourthNote.setNotebookGuid(secondNotebook.guid());
    fourthNote.setNotebookLocalUid(secondNotebook.localUid());
    fourthNote.addTagGuid(thirdTag.guid());
    fourthNote.addTagLocalUid(thirdTag.localUid());
    fourthNote.setLocal(false);
    fourthNote.setDirty(false);

    Note fifthNote;
    fifthNote.setGuid(UidGenerator::Generate());
    fifthNote.setTitle(QStringLiteral("Fifth note"));

    fifthNote.setContent(
        QStringLiteral("<en-note><h1>Fifth note content</h1></en-note>"));

    fifthNote.setUpdateSequenceNumber(55);
    fifthNote.setNotebookGuid(thirdNotebook.guid());
    fifthNote.setNotebookLocalUid(thirdNotebook.localUid());
    fifthNote.setLocal(false);
    fifthNote.setDirty(false);

    ErrorString errorDescription;

    bool res =
        pLocalStorageManager->addNotebook(firstNotebook, errorDescription);

    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addNotebook(secondNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addNotebook(thirdNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addTag(firstTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addTag(secondTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addTag(thirdTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addTag(fourthTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addSavedSearch(firstSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addSavedSearch(secondSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addNote(firstNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addNote(secondNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addNote(thirdNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addNote(fourthNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    errorDescription.clear();
    res = pLocalStorageManager->addNote(fifthNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Q_UNUSED(m_syncedGuids.m_syncedNotebookGuids.insert(firstNotebook.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedNotebookGuids.insert(secondNotebook.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedNotebookGuids.insert(thirdNotebook.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(firstTag.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(secondTag.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(thirdTag.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(fourthTag.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedSavedSearchGuids.insert(firstSearch.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedSavedSearchGuids.insert(secondSearch.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(firstNote.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(secondNote.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(thirdNote.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(fourthNote.guid()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(fifthNote.guid()))
}

void FullSyncStaleDataItemsExpungerTester::doTest(
    const bool useBaseDataItems, const QList<Notebook> & nonSyncedNotebooks,
    const QList<Tag> & nonSyncedTags,
    const QList<SavedSearch> & nonSyncedSavedSearches,
    const QList<Note> & nonSyncedNotes)
{
    if (Q_UNLIKELY(!m_pLocalStorageManagerAsync)) {
        QFAIL("Detected null pointer to LocalStorageManagerAsync");
    }

    auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Detected null pointer to LocalStorageManager");
    }

    if (Q_UNLIKELY(!m_pSavedSearchSyncCache)) {
        QFAIL("Detected null pointer to SavedSearchSyncCache");
    }

    if (useBaseDataItems) {
        setupBaseDataItems();
    }

    for (auto notebook: qAsConst(nonSyncedNotebooks)) {
        ErrorString errorDescription;
        bool res =
            pLocalStorageManager->addNotebook(notebook, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    for (auto tag: qAsConst(nonSyncedTags)) {
        ErrorString errorDescription;
        bool res = pLocalStorageManager->addTag(tag, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    for (auto search: qAsConst(nonSyncedSavedSearches)) {
        ErrorString errorDescription;
        bool res =
            pLocalStorageManager->addSavedSearch(search, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    for (auto note: qAsConst(nonSyncedNotes)) {
        ErrorString errorDescription;
        bool res = pLocalStorageManager->addNote(note, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    FullSyncStaleDataItemsExpunger expunger(
        *m_pLocalStorageManagerAsync, *m_pNotebookSyncCache, *m_pTagSyncCache,
        *m_pSavedSearchSyncCache, m_syncedGuids, QString());

    EventLoopWithExitStatus::ExitStatus expungerTestStatus =
        EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_MILLISECONDS);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        QObject::connect(
            &expunger, &FullSyncStaleDataItemsExpunger::finished, &loop,
            &EventLoopWithExitStatus::exitAsSuccess);

        QObject::connect(
            &expunger, &FullSyncStaleDataItemsExpunger::failure, &loop,
            &EventLoopWithExitStatus::exitAsFailureWithErrorString);

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(
            0, &expunger, &FullSyncStaleDataItemsExpunger::start);

        Q_UNUSED(loop.exec())
        expungerTestStatus = loop.exitStatus();
    }

    if (expungerTestStatus == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL(
            "Detected failure during the asynchronous loop processing "
            "in FullSyncStaleDataItemsExpunger");
    }
    else if (expungerTestStatus == EventLoopWithExitStatus::ExitStatus::Timeout)
    {
        QFAIL("FullSyncStaleDataItemsExpunger failed to finish in time");
    }

    // ====== Check remaining notebooks, verify each of them was intended to be
    //        preserved + verify all of notebooks intended to be preserved were
    //        actually preserved ======

    ErrorString errorDescription;

    auto remainingNotebooks = pLocalStorageManager->listNotebooks(
        LocalStorageManager::ListObjectsOption::ListAll, errorDescription);

    if (remainingNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    for (const auto & notebook: qAsConst(remainingNotebooks)) {
        if (notebook.hasGuid()) {
            auto guidIt =
                m_syncedGuids.m_syncedNotebookGuids.find(notebook.guid());

            if (guidIt != m_syncedGuids.m_syncedNotebookGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced notebook which survived the purge "
                "performed by FullSyncStaleDataItemsExpunger and kept its "
                "guid");
        }

        if (!notebook.isDirty()) {
            QFAIL(
                "Found a non-synced and non-dirty notebook which survived "
                "the purge performed by FullSyncStaleDataItemsExpunger");
        }

        auto extraNotebookIt = std::find_if(
            nonSyncedNotebooks.constBegin(), nonSyncedNotebooks.constEnd(),
            CompareItemByLocalUid<Notebook>(notebook.localUid()));

        if (extraNotebookIt == nonSyncedNotebooks.constEnd()) {
            QFAIL(
                "Found a notebook which survived the purge performed by "
                "FullSyncStaleDataItemsExpunger but has no guid "
                "and is not contained within the list of extra notebooks");
        }
    }

    for (const auto & notebook: qAsConst(nonSyncedNotebooks)) {
        auto remainingNotebookIt = std::find_if(
            remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
            CompareItemByLocalUid<Notebook>(notebook.localUid()));

        if ((remainingNotebookIt == remainingNotebooks.constEnd()) &&
            notebook.isDirty())
        {
            QFAIL(
                "One of extra notebooks which was dirty has not survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be preserved");
        }
        else if (
            (remainingNotebookIt != remainingNotebooks.constEnd()) &&
            !notebook.isDirty())
        {
            QFAIL(
                "One of etxra notebooks which was not dirty has survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be expunged");
        }
    }

    for (const auto & syncedGuid: qAsConst(m_syncedGuids.m_syncedNotebookGuids))
    {
        auto remainingNotebookIt = std::find_if(
            remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
            CompareItemByGuid<Notebook>(syncedGuid));

        if (remainingNotebookIt == remainingNotebooks.constEnd()) {
            QFAIL(
                "Could not find a notebook within the remaining ones which "
                "guid was marked as synced");
        }
    }

    // ====== Check remaining tags, verify each of them was intended to be
    //        preserved + verify all of tags intended to be preserved were
    //        actually preserved

    errorDescription.clear();

    auto remainingTags = pLocalStorageManager->listTags(
        LocalStorageManager::ListObjectsOption::ListAll, errorDescription);

    if (remainingTags.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    for (const auto & tag: qAsConst(remainingTags)) {
        if (tag.hasGuid()) {
            auto guidIt = m_syncedGuids.m_syncedTagGuids.find(tag.guid());
            if (guidIt != m_syncedGuids.m_syncedTagGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced tag which survived the purge performed "
                "by FullSyncStaleDataItemsExpunger and kept its guid");
        }

        if (!tag.isDirty()) {
            QFAIL(
                "Found a non-synced and non-dirty tag which survived "
                "the purge performed by FullSyncStaleDataItemsExpunger");
        }

        auto extraTagIt = std::find_if(
            nonSyncedTags.constBegin(), nonSyncedTags.constEnd(),
            CompareItemByLocalUid<Tag>(tag.localUid()));

        if (extraTagIt == nonSyncedTags.constEnd()) {
            QFAIL(
                "Found a tag which survived the purge performed by "
                "FullSyncStaleDataItemsExpunger but has no guid "
                "and is not contained within the list of extra tags");
        }
    }

    for (const auto & tag: qAsConst(nonSyncedTags)) {
        auto remainingTagIt = std::find_if(
            remainingTags.constBegin(), remainingTags.constEnd(),
            CompareItemByLocalUid<Tag>(tag.localUid()));

        if ((remainingTagIt == remainingTags.constEnd()) && tag.isDirty()) {
            QFAIL(
                "One of extra tags which was dirty has not survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be preserved");
        }
        else if ((remainingTagIt != remainingTags.constEnd()) && !tag.isDirty())
        {
            QFAIL(
                "One of extra tags which was not dirty has survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be expunged");
        }
    }

    for (const auto & syncedGuid: qAsConst(m_syncedGuids.m_syncedTagGuids)) {
        auto remainingTagIt = std::find_if(
            remainingTags.constBegin(), remainingTags.constEnd(),
            CompareItemByGuid<Tag>(syncedGuid));

        if (remainingTagIt == remainingTags.constEnd()) {
            QFAIL(
                "Could not find a tag within the remaining ones which guid "
                "was marked as synced");
        }
    }

    // ====== Check remaining saved searches, verify each of them was intended
    //        to be preserved + verify all of saved searches intended to be
    //        preserved were actually preserved ======

    errorDescription.clear();

    auto remainingSavedSearches = pLocalStorageManager->listSavedSearches(
        LocalStorageManager::ListObjectsOption::ListAll, errorDescription);

    if (remainingSavedSearches.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    for (const auto & search: qAsConst(remainingSavedSearches)) {
        if (search.hasGuid()) {
            auto guidIt =
                m_syncedGuids.m_syncedSavedSearchGuids.find(search.guid());

            if (guidIt != m_syncedGuids.m_syncedSavedSearchGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced saved search which survived the purge "
                "performed by FullSyncStaleDataItemsExpunger and kept its "
                "guid");
        }

        if (!search.isDirty()) {
            QFAIL(
                "Found a non-synced and non-dirty saved search which "
                "survived the purge performed by "
                "FullSyncStaleDataItemsExpunger");
        }
    }

    for (const auto & search: qAsConst(nonSyncedSavedSearches)) {
        auto remainingSavedSearchIt = std::find_if(
            remainingSavedSearches.constBegin(),
            remainingSavedSearches.constEnd(),
            CompareItemByLocalUid<SavedSearch>(search.localUid()));

        if ((remainingSavedSearchIt == remainingSavedSearches.constEnd()) &&
            search.isDirty())
        {
            QFAIL(
                "One of extra saved searches which was dirty has not "
                "survived the purge performed by "
                "FullSyncStaleDataItemsExpunger even though it was intended "
                "to be preserved");
        }
        else if (
            (remainingSavedSearchIt != remainingSavedSearches.constEnd()) &&
            !search.isDirty())
        {
            QFAIL(
                "One of extra saved searches which was not dirty has "
                "survived the purge performed by "
                "FullSyncStaleDataItemsExpunger even though it was intended "
                "to be expunged");
        }
    }

    for (const auto & syncedGuid:
         qAsConst(m_syncedGuids.m_syncedSavedSearchGuids)) {
        auto remainingSavedSearchIt = std::find_if(
            remainingSavedSearches.constBegin(),
            remainingSavedSearches.constEnd(),
            CompareItemByGuid<SavedSearch>(syncedGuid));

        if (remainingSavedSearchIt == remainingSavedSearches.constEnd()) {
            QFAIL(
                "Could not find a saved search within the remaining ones "
                "which guid was marked as synced");
        }
    }

    // ====== Check remaining notes, verify each of them was intended to be
    //        preserved + verify all of notes intended to be preserved were
    //        actually preserved

    errorDescription.clear();

    LocalStorageManager::GetNoteOptions getNoteOptions(
        LocalStorageManager::GetNoteOption::WithResourceMetadata);

    auto remainingNotes = pLocalStorageManager->listNotes(
        LocalStorageManager::ListObjectsOption::ListAll, getNoteOptions,
        errorDescription);

    if (remainingNotes.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    for (const auto & note: qAsConst(remainingNotes)) {
        if (!note.hasNotebookGuid() && !note.hasNotebookLocalUid()) {
            QFAIL("Found a note without notebook guid and notebook local uid");
        }

        auto remainingNotebookIt = remainingNotebooks.constEnd();
        if (note.hasNotebookGuid()) {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByGuid<Notebook>(note.notebookGuid()));
        }
        else {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByLocalUid<Notebook>(note.notebookLocalUid()));
        }

        if (Q_UNLIKELY(remainingNotebookIt == remainingNotebooks.constEnd())) {
            QFAIL(
                "Found a note which corresponding notebook has been expunged "
                "but the note still exists within the local storage");
        }

        if (note.hasGuid()) {
            auto guidIt = m_syncedGuids.m_syncedNoteGuids.find(note.guid());
            if (guidIt != m_syncedGuids.m_syncedNoteGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced note which survived the purge performed "
                "by FullSyncStaleDataItemsExpunger and kept its guid");
        }

        if (!note.isDirty()) {
            QFAIL(
                "Found a non-synced and non-dirty note which survived "
                "the purge performed by FullSyncStaleDataItemsExpunger");
        }
    }

    for (const auto & note: qAsConst(nonSyncedNotes)) {
        if (!note.hasNotebookGuid() && !note.hasNotebookLocalUid()) {
            QFAIL(
                "One of non-synced notes has no notebook guid and no "
                "notebook local uid");
        }

        auto remainingNotebookIt = remainingNotebooks.constEnd();
        if (note.hasNotebookGuid()) {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByGuid<Notebook>(note.notebookGuid()));
        }
        else {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByLocalUid<Notebook>(note.notebookLocalUid()));
        }

        auto remainingNoteIt = std::find_if(
            remainingNotes.constBegin(), remainingNotes.constEnd(),
            CompareItemByLocalUid<Note>(note.localUid()));

        if ((remainingNoteIt == remainingNotes.constEnd()) && note.isDirty() &&
            (remainingNotebookIt != remainingNotebooks.constEnd()))
        {
            QFAIL(
                "One of extra notes which was dirty has not survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be preserved");
        }
        else if (
            (remainingNoteIt != remainingNotes.constEnd()) && !note.isDirty()) {
            QFAIL(
                "One of extra notes which was not dirty has survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be expunged");
        }
    }

    for (const auto & syncedGuid: qAsConst(m_syncedGuids.m_syncedNoteGuids)) {
        auto remainingNoteIt = std::find_if(
            remainingNotes.constBegin(), remainingNotes.constEnd(),
            CompareItemByGuid<Note>(syncedGuid));

        if (remainingNoteIt == remainingNotes.constEnd()) {
            QFAIL(
                "Could not find a note within the remaining ones which guid "
                "was marked as synced");
        }
    }
}

} // namespace test
} // namespace quentier
