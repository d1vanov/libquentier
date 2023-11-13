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

#include "FullSyncStaleDataItemsExpungerTester.h"

#include "../../synchronization/NotebookSyncCache.h"
#include "../../synchronization/SavedSearchSyncCache.h"
#include "../../synchronization/TagSyncCache.h"

#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/UidGenerator.h>

#include <QTimer>
#include <QTest>

// 10 minutes should be enough
constexpr int gTestMaxMilliseconds = 600000;

// Local ids of base data items' notebooks
static const QString gFirstNotebookLocalId =
    QStringLiteral("68b6df59-5e35-4850-a972-b5493dfead8a");

static const QString gSecondNotebookLocalId =
    QStringLiteral("b5f6eb38-428b-4964-b4ca-b72007e11c4f");

static const QString gThirdNotebookLocalId =
    QStringLiteral("7d919756-e83d-4a02-b94f-f6eab8e12885");

namespace quentier::test {

template <class T>
struct CompareItemByLocalId
{
    CompareItemByLocalId(QString targetLocalId) :
        m_targetLocalId(std::move(targetLocalId))
    {}

    [[nodiscard]] bool operator()(const T & item) const noexcept
    {
        return item.localId() == m_targetLocalId;
    }

    QString m_targetLocalId;
};

template <class T>
struct CompareItemByGuid
{
    CompareItemByGuid(QString targetGuid) : m_targetGuid(std::move(targetGuid))
    {}

    [[nodiscard]] bool operator()(const T & item) const noexcept
    {
        return (item.guid() && (*item.guid() == m_targetGuid));
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

FullSyncStaleDataItemsExpungerTester::~FullSyncStaleDataItemsExpungerTester() =
    default;

void FullSyncStaleDataItemsExpungerTester::init()
{
    m_testAccount = Account{
        m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1};

    const LocalStorageManager::StartupOptions startupOptions(
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
        /* use base data items = */ false, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testNoStaleOrDirtyItems()
{
    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNotebook()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    const auto nonSyncedNotebooks = QList<qevercloud::Notebook>{}
        << staleNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleTag()
{
    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(100);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    const auto nonSyncedTags = QList<qevercloud::Tag>{} << staleTag;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleSavedSearch()
{
    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(100);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    const auto nonSyncedSavedSearches = QList<qevercloud::SavedSearch>{}
        << staleSearch;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches,
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNote()
{
    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(100);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    const auto nonSyncedNotes = QList<qevercloud::Note>{} << staleNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNotebookAndOneStaleTag()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(101);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    const auto nonSyncedNotebooks = QList<qevercloud::Notebook>{}
        << staleNotebook;

    const auto nonSyncedTags = QList<qevercloud::Tag>{} << staleTag;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        QList<qevercloud::SavedSearch>{}, QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleNotebookAndOneStaleSavedSearch()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(101);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    const auto nonSyncedNotebooks = QList<qevercloud::Notebook>{}
        << staleNotebook;

    const auto nonSyncedSavedSearches = QList<qevercloud::SavedSearch>{}
        << staleSearch;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches,
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNotebookAndOneStaleNote()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(100);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    const auto nonSyncedNotebooks = QList<qevercloud::Notebook>{}
        << staleNotebook;

    const auto nonSyncedNotes = QList<qevercloud::Note>{} << staleNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleTagAndOneStaleSavedSearch()
{
    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(100);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(101);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    const auto nonSyncedTags = QList<qevercloud::Tag>{} << staleTag;

    const auto nonSyncedSavedSearches = QList<qevercloud::SavedSearch>{}
        << staleSearch;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, nonSyncedSavedSearches, QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleTagAndOneStaleNote()
{
    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(100);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(101);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    const auto nonSyncedTags = QList<qevercloud::Tag>{} << staleTag;
    const auto nonSyncedNotes = QList<qevercloud::Note>{} << staleNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{}, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleSavedSearchAndOneStaleNote()
{
    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(100);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(101);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    const auto nonSyncedSavedSearches = QList<qevercloud::SavedSearch>{}
        << staleSearch;

    const auto nonSyncedNotes = QList<qevercloud::Note>{} << staleNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleItemOfEachKind()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(101);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(100);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(100);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    const auto nonSyncedNotebooks = QList<qevercloud::Notebook>{}
        << staleNotebook;

    const auto nonSyncedTags = QList<qevercloud::Tag>{} << staleTag;

    const auto nonSyncedSavedSearches = QList<qevercloud::SavedSearch>{}
        << staleSearch;

    const auto nonSyncedNotes = QList<qevercloud::Note>{} << staleNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleNotebooks()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNum(101);
    secondStaleNotebook.setLocalOnly(false);
    secondStaleNotebook.setLocallyModified(false);

    qevercloud::Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNum(102);
    thirdStaleNotebook.setLocalOnly(false);
    thirdStaleNotebook.setLocallyModified(false);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleTags()
{
    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(100);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNum(101);
    secondStaleTag.setLocalOnly(false);
    secondStaleTag.setLocallyModified(false);

    qevercloud::Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNum(102);
    thirdStaleTag.setLocalOnly(false);
    thirdStaleTag.setLocallyModified(false);

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleSavedSearches()
{
    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(100);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    qevercloud::SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNum(102);
    secondStaleSavedSearch.setLocalOnly(false);
    secondStaleSavedSearch.setLocallyModified(false);

    qevercloud::SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNum(103);
    thirdStaleSavedSearch.setLocalOnly(false);
    thirdStaleSavedSearch.setLocallyModified(false);

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches,
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleNotes()
{
    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(100);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    qevercloud::Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNum(101);
    secondStaleNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondStaleNote.setLocalOnly(false);
    secondStaleNote.setLocallyModified(false);

    qevercloud::Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNum(103);
    thirdStaleNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdStaleNote.setLocalOnly(false);
    thirdStaleNote.setLocallyModified(false);

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes.reserve(3);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << secondStaleNote;
    nonSyncedNotes << thirdStaleNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralStaleItemsOfEachKind()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNum(101);
    secondStaleNotebook.setLocalOnly(false);
    secondStaleNotebook.setLocallyModified(false);

    qevercloud::Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNum(102);
    thirdStaleNotebook.setLocalOnly(false);
    thirdStaleNotebook.setLocallyModified(false);

    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(103);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNum(104);
    secondStaleTag.setLocalOnly(false);
    secondStaleTag.setLocallyModified(false);

    qevercloud::Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNum(105);
    thirdStaleTag.setLocalOnly(false);
    thirdStaleTag.setLocallyModified(false);

    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(106);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    qevercloud::SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNum(107);
    secondStaleSavedSearch.setLocalOnly(false);
    secondStaleSavedSearch.setLocallyModified(false);

    qevercloud::SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNum(108);
    thirdStaleSavedSearch.setLocalOnly(false);
    thirdStaleSavedSearch.setLocallyModified(false);

    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(109);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    qevercloud::Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNum(110);
    secondStaleNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondStaleNote.setLocalOnly(false);
    secondStaleNote.setLocallyModified(false);

    qevercloud::Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNum(111);
    thirdStaleNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdStaleNote.setLocalOnly(false);
    thirdStaleNote.setLocallyModified(false);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;

    QList<qevercloud::Note> nonSyncedNotes;
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
    qevercloud::Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNum(100);
    dirtyNotebook.setLocalOnly(false);
    dirtyNotebook.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << dirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtyTag()
{
    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(100);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags << dirtyTag;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtySavedSearch()
{
    qevercloud::SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNum(100);
    dirtySavedSearch.setLocalOnly(false);
    dirtySavedSearch.setLocallyModified(true);

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << dirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches,
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtyNote()
{
    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(100);
    dirtyNote.setNotebookLocalId(gFirstNotebookLocalId);
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testOneDirtyItemOfEachKind()
{
    qevercloud::Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNum(100);
    dirtyNotebook.setLocalOnly(false);
    dirtyNotebook.setLocallyModified(true);

    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(101);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);

    qevercloud::SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNum(102);
    dirtySavedSearch.setLocalOnly(false);
    dirtySavedSearch.setLocallyModified(true);

    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(103);
    dirtyNote.setNotebookLocalId(gFirstNotebookLocalId);
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << dirtyNotebook;

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags << dirtyTag;

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches << dirtySavedSearch;

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks, nonSyncedTags,
        nonSyncedSavedSearches, nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyNotebooks()
{
    qevercloud::Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNum(100);
    dirtyNotebook.setLocalOnly(false);
    dirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNum(101);
    secondDirtyNotebook.setLocalOnly(false);
    secondDirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNum(102);
    thirdDirtyNotebook.setLocalOnly(false);
    thirdDirtyNotebook.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyTags()
{
    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(100);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);

    qevercloud::Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNum(101);
    secondDirtyTag.setLocalOnly(false);
    secondDirtyTag.setLocallyModified(true);

    qevercloud::Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNum(102);
    thirdDirtyTag.setLocalOnly(false);
    thirdDirtyTag.setLocallyModified(true);

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtySavedSearches()
{
    qevercloud::SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNum(100);
    dirtySavedSearch.setLocalOnly(false);
    dirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNum(101);
    secondDirtySavedSearch.setLocalOnly(false);
    secondDirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNum(102);
    thirdDirtySavedSearch.setLocalOnly(false);
    thirdDirtySavedSearch.setLocallyModified(true);

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches,
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyNotes()
{
    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(100);
    dirtyNote.setNotebookLocalId(gFirstNotebookLocalId);
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    qevercloud::Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNum(101);
    secondDirtyNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondDirtyNote.setLocalOnly(false);
    secondDirtyNote.setLocallyModified(true);

    qevercloud::Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNum(102);
    thirdDirtyNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdDirtyNote.setLocalOnly(false);
    thirdDirtyNote.setLocallyModified(true);

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes.reserve(3);
    nonSyncedNotes << dirtyNote;
    nonSyncedNotes << secondDirtyNote;
    nonSyncedNotes << thirdDirtyNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testSeveralDirtyItemsOfEachKind()
{
    qevercloud::Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNum(100);
    dirtyNotebook.setLocalOnly(false);
    dirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNum(101);
    secondDirtyNotebook.setLocalOnly(false);
    secondDirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNum(102);
    thirdDirtyNotebook.setLocalOnly(false);
    thirdDirtyNotebook.setLocallyModified(true);

    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(103);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);

    qevercloud::Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNum(104);
    secondDirtyTag.setLocalOnly(false);
    secondDirtyTag.setLocallyModified(true);

    qevercloud::Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNum(105);
    thirdDirtyTag.setLocalOnly(false);
    thirdDirtyTag.setLocallyModified(true);

    qevercloud::SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNum(106);
    dirtySavedSearch.setLocalOnly(false);
    dirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNum(107);
    secondDirtySavedSearch.setLocalOnly(false);
    secondDirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNum(108);
    thirdDirtySavedSearch.setLocalOnly(false);
    thirdDirtySavedSearch.setLocallyModified(true);

    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(109);
    dirtyNote.setNotebookLocalId(gFirstNotebookLocalId);
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    qevercloud::Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNum(110);
    secondDirtyNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondDirtyNote.setLocalOnly(false);
    secondDirtyNote.setLocallyModified(true);

    qevercloud::Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNum(111);
    thirdDirtyNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdDirtyNote.setLocalOnly(false);
    thirdDirtyNote.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(3);
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(3);
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(3);
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    QList<qevercloud::Note> nonSyncedNotes;
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
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNum(101);
    dirtyNotebook.setLocalOnly(false);
    dirtyNotebook.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(2);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << dirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleTagAndOneDirtyTag()
{
    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(100);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(101);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(2);
    nonSyncedTags << staleTag;
    nonSyncedTags << dirtyTag;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::
    testOneStaleSavedSearchAndOneDirtySavedSearch()
{
    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(100);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    qevercloud::SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNum(101);
    dirtySavedSearch.setLocalOnly(false);
    dirtySavedSearch.setLocallyModified(true);

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(2);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << dirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches,
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testOneStaleNoteAndOneDirtyNote()
{
    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(100);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(101);
    dirtyNote.setNotebookLocalId(gFirstNotebookLocalId);
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes.reserve(2);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleNotebooksAndSeveralDirtyNotebooks()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNum(101);
    secondStaleNotebook.setLocalOnly(false);
    secondStaleNotebook.setLocallyModified(false);

    qevercloud::Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNum(102);
    thirdStaleNotebook.setLocalOnly(false);
    thirdStaleNotebook.setLocallyModified(false);

    qevercloud::Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNum(103);
    dirtyNotebook.setLocalOnly(false);
    dirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNum(104);
    secondDirtyNotebook.setLocalOnly(false);
    secondDirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNum(105);
    thirdDirtyNotebook.setLocalOnly(false);
    thirdDirtyNotebook.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(6);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleTagsAndSeveralDirtyTags()
{
    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(100);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNum(101);
    secondStaleTag.setLocalOnly(false);
    secondStaleTag.setLocallyModified(false);

    qevercloud::Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNum(102);
    thirdStaleTag.setLocalOnly(false);
    thirdStaleTag.setLocallyModified(false);

    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(103);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);

    qevercloud::Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNum(104);
    secondDirtyTag.setLocalOnly(false);
    secondDirtyTag.setLocallyModified(true);

    qevercloud::Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNum(105);
    thirdDirtyTag.setLocalOnly(false);
    thirdDirtyTag.setLocallyModified(true);

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(6);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleSavedSearchesAndSeveralDirtySavedSearches()
{
    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(100);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    qevercloud::SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNum(101);
    secondStaleSavedSearch.setLocalOnly(false);
    secondStaleSavedSearch.setLocallyModified(false);

    qevercloud::SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNum(102);
    thirdStaleSavedSearch.setLocalOnly(false);
    thirdStaleSavedSearch.setLocallyModified(false);

    qevercloud::SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNum(103);
    dirtySavedSearch.setLocalOnly(false);
    dirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNum(104);
    secondDirtySavedSearch.setLocalOnly(false);
    secondDirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNum(105);
    thirdDirtySavedSearch.setLocalOnly(false);
    thirdDirtySavedSearch.setLocallyModified(true);

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(6);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, nonSyncedSavedSearches,
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleNotesAndSeveralDirtyNotes()
{
    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(100);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    qevercloud::Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNum(101);
    secondStaleNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondStaleNote.setLocalOnly(false);
    secondStaleNote.setLocallyModified(false);

    qevercloud::Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNum(103);
    thirdStaleNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdStaleNote.setLocalOnly(false);
    thirdStaleNote.setLocallyModified(false);

    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(100);
    dirtyNote.setNotebookLocalId(gFirstNotebookLocalId);
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    qevercloud::Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNum(101);
    secondDirtyNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondDirtyNote.setLocalOnly(false);
    secondDirtyNote.setLocallyModified(true);

    qevercloud::Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNum(102);
    thirdDirtyNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdDirtyNote.setLocalOnly(false);
    thirdDirtyNote.setLocallyModified(true);

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes.reserve(6);
    nonSyncedNotes << staleNote;
    nonSyncedNotes << secondStaleNote;
    nonSyncedNotes << thirdStaleNote;
    nonSyncedNotes << dirtyNote;
    nonSyncedNotes << secondDirtyNote;
    nonSyncedNotes << thirdDirtyNote;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::
    testSeveralStaleAndDirtyItemsOfEachKind()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Notebook secondStaleNotebook;
    secondStaleNotebook.setName(QStringLiteral("Second stale notebook"));
    secondStaleNotebook.setGuid(UidGenerator::Generate());
    secondStaleNotebook.setUpdateSequenceNum(101);
    secondStaleNotebook.setLocalOnly(false);
    secondStaleNotebook.setLocallyModified(false);

    qevercloud::Notebook thirdStaleNotebook;
    thirdStaleNotebook.setName(QStringLiteral("Third stale notebook"));
    thirdStaleNotebook.setGuid(UidGenerator::Generate());
    thirdStaleNotebook.setUpdateSequenceNum(102);
    thirdStaleNotebook.setLocalOnly(false);
    thirdStaleNotebook.setLocallyModified(false);

    qevercloud::Notebook dirtyNotebook;
    dirtyNotebook.setName(QStringLiteral("Dirty notebook"));
    dirtyNotebook.setGuid(UidGenerator::Generate());
    dirtyNotebook.setUpdateSequenceNum(103);
    dirtyNotebook.setLocalOnly(false);
    dirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook secondDirtyNotebook;
    secondDirtyNotebook.setName(QStringLiteral("Second dirty notebook"));
    secondDirtyNotebook.setGuid(UidGenerator::Generate());
    secondDirtyNotebook.setUpdateSequenceNum(104);
    secondDirtyNotebook.setLocalOnly(false);
    secondDirtyNotebook.setLocallyModified(true);

    qevercloud::Notebook thirdDirtyNotebook;
    thirdDirtyNotebook.setName(QStringLiteral("Third dirty notebook"));
    thirdDirtyNotebook.setGuid(UidGenerator::Generate());
    thirdDirtyNotebook.setUpdateSequenceNum(105);
    thirdDirtyNotebook.setLocalOnly(false);
    thirdDirtyNotebook.setLocallyModified(true);

    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(106);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::Tag secondStaleTag;
    secondStaleTag.setName(QStringLiteral("Second stale tag"));
    secondStaleTag.setGuid(UidGenerator::Generate());
    secondStaleTag.setUpdateSequenceNum(107);
    secondStaleTag.setLocalOnly(false);
    secondStaleTag.setLocallyModified(false);

    qevercloud::Tag thirdStaleTag;
    thirdStaleTag.setName(QStringLiteral("Third stale tag"));
    thirdStaleTag.setGuid(UidGenerator::Generate());
    thirdStaleTag.setUpdateSequenceNum(108);
    thirdStaleTag.setLocalOnly(false);
    thirdStaleTag.setLocallyModified(false);

    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(109);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);

    qevercloud::Tag secondDirtyTag;
    secondDirtyTag.setName(QStringLiteral("Second dirty tag"));
    secondDirtyTag.setGuid(UidGenerator::Generate());
    secondDirtyTag.setUpdateSequenceNum(110);
    secondDirtyTag.setLocalOnly(false);
    secondDirtyTag.setLocallyModified(true);

    qevercloud::Tag thirdDirtyTag;
    thirdDirtyTag.setName(QStringLiteral("Third dirty tag"));
    thirdDirtyTag.setGuid(UidGenerator::Generate());
    thirdDirtyTag.setUpdateSequenceNum(111);
    thirdDirtyTag.setLocalOnly(false);
    thirdDirtyTag.setLocallyModified(true);

    qevercloud::SavedSearch staleSearch;
    staleSearch.setName(QStringLiteral("Stale saved search"));
    staleSearch.setQuery(QStringLiteral("stale"));
    staleSearch.setGuid(UidGenerator::Generate());
    staleSearch.setUpdateSequenceNum(112);
    staleSearch.setLocalOnly(false);
    staleSearch.setLocallyModified(false);

    qevercloud::SavedSearch secondStaleSavedSearch;
    secondStaleSavedSearch.setName(QStringLiteral("Second stale saved search"));
    secondStaleSavedSearch.setQuery(QStringLiteral("stale2"));
    secondStaleSavedSearch.setGuid(UidGenerator::Generate());
    secondStaleSavedSearch.setUpdateSequenceNum(113);
    secondStaleSavedSearch.setLocalOnly(false);
    secondStaleSavedSearch.setLocallyModified(false);

    qevercloud::SavedSearch thirdStaleSavedSearch;
    thirdStaleSavedSearch.setName(QStringLiteral("Third stale saved search"));
    thirdStaleSavedSearch.setQuery(QStringLiteral("stale3"));
    thirdStaleSavedSearch.setGuid(UidGenerator::Generate());
    thirdStaleSavedSearch.setUpdateSequenceNum(114);
    thirdStaleSavedSearch.setLocalOnly(false);
    thirdStaleSavedSearch.setLocallyModified(false);

    qevercloud::SavedSearch dirtySavedSearch;
    dirtySavedSearch.setName(QStringLiteral("Dirty saved search"));
    dirtySavedSearch.setQuery(QStringLiteral("dirty"));
    dirtySavedSearch.setGuid(UidGenerator::Generate());
    dirtySavedSearch.setUpdateSequenceNum(115);
    dirtySavedSearch.setLocalOnly(false);
    dirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch secondDirtySavedSearch;
    secondDirtySavedSearch.setName(QStringLiteral("Second dirty saved search"));
    secondDirtySavedSearch.setQuery(QStringLiteral("dirty2"));
    secondDirtySavedSearch.setGuid(UidGenerator::Generate());
    secondDirtySavedSearch.setUpdateSequenceNum(116);
    secondDirtySavedSearch.setLocalOnly(false);
    secondDirtySavedSearch.setLocallyModified(true);

    qevercloud::SavedSearch thirdDirtySavedSearch;
    thirdDirtySavedSearch.setName(QStringLiteral("Third dirty saved search"));
    thirdDirtySavedSearch.setQuery(QStringLiteral("dirty3"));
    thirdDirtySavedSearch.setGuid(UidGenerator::Generate());
    thirdDirtySavedSearch.setUpdateSequenceNum(117);
    thirdDirtySavedSearch.setLocalOnly(false);
    thirdDirtySavedSearch.setLocallyModified(true);

    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(118);
    staleNote.setNotebookLocalId(gFirstNotebookLocalId);
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    qevercloud::Note secondStaleNote;
    secondStaleNote.setTitle(QStringLiteral("Second stale note"));

    secondStaleNote.setContent(QStringLiteral(
        "<en-note><h1>Second stale note content</h1></en-note>"));

    secondStaleNote.setGuid(UidGenerator::Generate());
    secondStaleNote.setUpdateSequenceNum(119);
    secondStaleNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondStaleNote.setLocalOnly(false);
    secondStaleNote.setLocallyModified(false);

    qevercloud::Note thirdStaleNote;
    thirdStaleNote.setTitle(QStringLiteral("Third stale note"));

    thirdStaleNote.setContent(
        QStringLiteral("<en-note><h1>Third stale note content</h1></en-note>"));

    thirdStaleNote.setGuid(UidGenerator::Generate());
    thirdStaleNote.setUpdateSequenceNum(120);
    thirdStaleNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdStaleNote.setLocalOnly(false);
    thirdStaleNote.setLocallyModified(false);

    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(121);
    dirtyNote.setNotebookLocalId(gFirstNotebookLocalId);
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    qevercloud::Note secondDirtyNote;
    secondDirtyNote.setTitle(QStringLiteral("Second dirty note"));

    secondDirtyNote.setContent(QStringLiteral(
        "<en-note><h1>Second dirty note content</h1></en-note>"));

    secondDirtyNote.setGuid(UidGenerator::Generate());
    secondDirtyNote.setUpdateSequenceNum(122);
    secondDirtyNote.setNotebookLocalId(gSecondNotebookLocalId);
    secondDirtyNote.setLocalOnly(false);
    secondDirtyNote.setLocallyModified(true);

    qevercloud::Note thirdDirtyNote;
    thirdDirtyNote.setTitle(QStringLiteral("Third dirty note"));

    thirdDirtyNote.setContent(
        QStringLiteral("<en-note><h1>Third dirty note content</h1></en-note>"));

    thirdDirtyNote.setGuid(UidGenerator::Generate());
    thirdDirtyNote.setUpdateSequenceNum(123);
    thirdDirtyNote.setNotebookLocalId(gThirdNotebookLocalId);
    thirdDirtyNote.setLocalOnly(false);
    thirdDirtyNote.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks.reserve(6);
    nonSyncedNotebooks << staleNotebook;
    nonSyncedNotebooks << secondStaleNotebook;
    nonSyncedNotebooks << thirdStaleNotebook;
    nonSyncedNotebooks << dirtyNotebook;
    nonSyncedNotebooks << secondDirtyNotebook;
    nonSyncedNotebooks << thirdDirtyNotebook;

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(6);
    nonSyncedTags << staleTag;
    nonSyncedTags << secondStaleTag;
    nonSyncedTags << thirdStaleTag;
    nonSyncedTags << dirtyTag;
    nonSyncedTags << secondDirtyTag;
    nonSyncedTags << thirdDirtyTag;

    QList<qevercloud::SavedSearch> nonSyncedSavedSearches;
    nonSyncedSavedSearches.reserve(6);
    nonSyncedSavedSearches << staleSearch;
    nonSyncedSavedSearches << secondStaleSavedSearch;
    nonSyncedSavedSearches << thirdStaleSavedSearch;
    nonSyncedSavedSearches << dirtySavedSearch;
    nonSyncedSavedSearches << secondDirtySavedSearch;
    nonSyncedSavedSearches << thirdDirtySavedSearch;

    QList<qevercloud::Note> nonSyncedNotes;
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
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Note dirtyNote;
    dirtyNote.setTitle(QStringLiteral("Dirty note"));

    dirtyNote.setContent(
        QStringLiteral("<en-note><h1>Dirty note content</h1></en-note>"));

    dirtyNote.setGuid(UidGenerator::Generate());
    dirtyNote.setUpdateSequenceNum(100);
    dirtyNote.setNotebookLocalId(staleNotebook.localId());
    dirtyNote.setLocalOnly(false);
    dirtyNote.setLocallyModified(true);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes << dirtyNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
}

void FullSyncStaleDataItemsExpungerTester::testDirtyTagWithStaleParentTag()
{
    qevercloud::Tag staleTag;
    staleTag.setName(QStringLiteral("Stale tag"));
    staleTag.setGuid(UidGenerator::Generate());
    staleTag.setUpdateSequenceNum(100);
    staleTag.setLocalOnly(false);
    staleTag.setLocallyModified(false);

    qevercloud::Tag dirtyTag;
    dirtyTag.setName(QStringLiteral("Dirty tag"));
    dirtyTag.setGuid(UidGenerator::Generate());
    dirtyTag.setUpdateSequenceNum(101);
    dirtyTag.setLocalOnly(false);
    dirtyTag.setLocallyModified(true);
    dirtyTag.setParentGuid(staleTag.guid());
    dirtyTag.setParentTagLocalId(staleTag.localId());

    QList<qevercloud::Tag> nonSyncedTags;
    nonSyncedTags.reserve(2);
    nonSyncedTags << staleTag;
    nonSyncedTags << dirtyTag;

    doTest(
        /* use base data items = */ true, QList<qevercloud::Notebook>{},
        nonSyncedTags, QList<qevercloud::SavedSearch>{},
        QList<qevercloud::Note>{});
}

void FullSyncStaleDataItemsExpungerTester::testStaleNoteFromStaleNotebook()
{
    qevercloud::Notebook staleNotebook;
    staleNotebook.setName(QStringLiteral("Stale notebook"));
    staleNotebook.setGuid(UidGenerator::Generate());
    staleNotebook.setUpdateSequenceNum(100);
    staleNotebook.setLocalOnly(false);
    staleNotebook.setLocallyModified(false);

    qevercloud::Note staleNote;
    staleNote.setTitle(QStringLiteral("Stale note"));

    staleNote.setContent(
        QStringLiteral("<en-note><h1>Stale note content</h1></en-note>"));

    staleNote.setGuid(UidGenerator::Generate());
    staleNote.setUpdateSequenceNum(100);
    staleNote.setNotebookLocalId(staleNotebook.localId());
    staleNote.setLocalOnly(false);
    staleNote.setLocallyModified(false);

    QList<qevercloud::Notebook> nonSyncedNotebooks;
    nonSyncedNotebooks << staleNotebook;

    QList<qevercloud::Note> nonSyncedNotes;
    nonSyncedNotes << staleNote;

    doTest(
        /* use base data items = */ true, nonSyncedNotebooks,
        QList<qevercloud::Tag>{}, QList<qevercloud::SavedSearch>{},
        nonSyncedNotes);
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

    qevercloud::Notebook firstNotebook;
    firstNotebook.setLocalId(gFirstNotebookLocalId);
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setName(QStringLiteral("First notebook"));
    firstNotebook.setUpdateSequenceNum(42);
    firstNotebook.setLocalOnly(false);
    firstNotebook.setLocallyModified(false);

    qevercloud::Notebook secondNotebook;
    secondNotebook.setLocalId(gSecondNotebookLocalId);
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second notebook"));
    secondNotebook.setUpdateSequenceNum(43);
    secondNotebook.setLocalOnly(false);
    secondNotebook.setLocallyModified(false);

    qevercloud::Notebook thirdNotebook;
    thirdNotebook.setLocalId(gThirdNotebookLocalId);
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setName(QStringLiteral("Third notebook"));
    thirdNotebook.setUpdateSequenceNum(44);
    thirdNotebook.setLocalOnly(false);
    thirdNotebook.setLocallyModified(false);

    qevercloud::Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First tag"));
    firstTag.setUpdateSequenceNum(45);
    firstTag.setLocalOnly(false);
    firstTag.setLocallyModified(false);

    qevercloud::Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second tag"));
    secondTag.setUpdateSequenceNum(46);
    secondTag.setLocalOnly(false);
    secondTag.setLocallyModified(false);

    qevercloud::Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setName(QStringLiteral("Third tag"));
    thirdTag.setUpdateSequenceNum(47);
    thirdTag.setLocalOnly(false);
    thirdTag.setLocallyModified(false);

    qevercloud::Tag fourthTag;
    fourthTag.setGuid(UidGenerator::Generate());
    fourthTag.setName(QStringLiteral("Fourth tag"));
    fourthTag.setUpdateSequenceNum(48);
    fourthTag.setLocalOnly(false);
    fourthTag.setLocallyModified(false);
    fourthTag.setParentGuid(secondTag.guid());
    fourthTag.setParentTagLocalId(secondTag.localId());

    qevercloud::SavedSearch firstSearch;
    firstSearch.setGuid(UidGenerator::Generate());
    firstSearch.setName(QStringLiteral("First search"));
    firstSearch.setQuery(QStringLiteral("First search query"));
    firstSearch.setUpdateSequenceNum(49);
    firstSearch.setLocalOnly(false);
    firstSearch.setLocallyModified(false);

    qevercloud::SavedSearch secondSearch;
    secondSearch.setGuid(UidGenerator::Generate());
    secondSearch.setName(QStringLiteral("Second search"));
    secondSearch.setQuery(QStringLiteral("Second search query"));
    secondSearch.setUpdateSequenceNum(50);
    secondSearch.setLocalOnly(false);
    secondSearch.setLocallyModified(false);

    qevercloud::Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setTitle(QStringLiteral("First note"));

    firstNote.setContent(
        QStringLiteral("<en-note><h1>First note content</h1></en-note>"));

    firstNote.setUpdateSequenceNum(51);
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setNotebookLocalId(firstNotebook.localId());
    firstNote.setLocalOnly(false);
    firstNote.setLocallyModified(false);

    qevercloud::Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setTitle(QStringLiteral("Second note"));

    secondNote.setContent(
        QStringLiteral("<en-note><h1>Second note content</h1></en-note>"));

    secondNote.setUpdateSequenceNum(52);
    secondNote.setNotebookGuid(firstNotebook.guid());
    secondNote.setNotebookLocalId(firstNotebook.localId());
    secondNote.setLocalOnly(false);
    secondNote.setLocallyModified(false);

    qevercloud::Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setTitle(QStringLiteral("Third note"));

    thirdNote.setContent(
        QStringLiteral("<en-note><h1>Third note content</h1></en-note>"));

    thirdNote.setUpdateSequenceNum(53);
    thirdNote.setNotebookGuid(firstNotebook.guid());
    thirdNote.setNotebookLocalId(firstNotebook.localId());
    thirdNote.setTagGuids(
        QList<qevercloud::Guid>{} << firstTag.guid().value()
                                  << secondTag.guid().value());
    thirdNote.setTagLocalIds(
        QStringList{} << firstTag.localId() << secondTag.localId());
    thirdNote.setLocalOnly(false);
    thirdNote.setLocallyModified(false);

    qevercloud::Note fourthNote;
    fourthNote.setGuid(UidGenerator::Generate());
    fourthNote.setTitle(QStringLiteral("Fourth note"));

    fourthNote.setContent(
        QStringLiteral("<en-note><h1>Fourth note content</h1></en-note>"));

    fourthNote.setUpdateSequenceNum(54);
    fourthNote.setNotebookGuid(secondNotebook.guid());
    fourthNote.setNotebookLocalId(secondNotebook.localId());
    fourthNote.setTagGuids(
        QList<qevercloud::Guid>{} << thirdTag.guid().value());
    fourthNote.setTagLocalIds(QStringList{} << thirdTag.localId());
    fourthNote.setLocalOnly(false);
    fourthNote.setLocallyModified(false);

    qevercloud::Note fifthNote;
    fifthNote.setGuid(UidGenerator::Generate());
    fifthNote.setTitle(QStringLiteral("Fifth note"));

    fifthNote.setContent(
        QStringLiteral("<en-note><h1>Fifth note content</h1></en-note>"));

    fifthNote.setUpdateSequenceNum(55);
    fifthNote.setNotebookGuid(thirdNotebook.guid());
    fifthNote.setNotebookLocalId(thirdNotebook.localId());
    fifthNote.setLocalOnly(false);
    fifthNote.setLocallyModified(false);

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

    Q_UNUSED(m_syncedGuids.m_syncedNotebookGuids.insert(
        firstNotebook.guid().value()))

    Q_UNUSED(m_syncedGuids.m_syncedNotebookGuids.insert(
        secondNotebook.guid().value()))

    Q_UNUSED(m_syncedGuids.m_syncedNotebookGuids.insert(
        thirdNotebook.guid().value()))

    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(firstTag.guid().value()))
    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(secondTag.guid().value()))
    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(thirdTag.guid().value()))
    Q_UNUSED(m_syncedGuids.m_syncedTagGuids.insert(fourthTag.guid().value()))

    Q_UNUSED(m_syncedGuids.m_syncedSavedSearchGuids.insert(
        firstSearch.guid().value()))

    Q_UNUSED(m_syncedGuids.m_syncedSavedSearchGuids.insert(
        secondSearch.guid().value()))

    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(firstNote.guid().value()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(secondNote.guid().value()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(thirdNote.guid().value()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(fourthNote.guid().value()))
    Q_UNUSED(m_syncedGuids.m_syncedNoteGuids.insert(fifthNote.guid().value()))
}

void FullSyncStaleDataItemsExpungerTester::doTest(
    const bool useBaseDataItems,
    const QList<qevercloud::Notebook> & nonSyncedNotebooks,
    const QList<qevercloud::Tag> & nonSyncedTags,
    const QList<qevercloud::SavedSearch> & nonSyncedSavedSearches,
    const QList<qevercloud::Note> & nonSyncedNotes)
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
        timer.setInterval(gTestMaxMilliseconds);
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

        timer.start();
        QTimer::singleShot(
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
        if (notebook.guid()) {
            auto guidIt =
                m_syncedGuids.m_syncedNotebookGuids.find(*notebook.guid());

            if (guidIt != m_syncedGuids.m_syncedNotebookGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced notebook which survived the purge "
                "performed by FullSyncStaleDataItemsExpunger and kept its "
                "guid");
        }

        if (!notebook.isLocallyModified()) {
            QFAIL(
                "Found a non-synced and non-dirty notebook which survived "
                "the purge performed by FullSyncStaleDataItemsExpunger");
        }

        auto extraNotebookIt = std::find_if(
            nonSyncedNotebooks.constBegin(), nonSyncedNotebooks.constEnd(),
            CompareItemByLocalId<qevercloud::Notebook>(notebook.localId()));

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
            CompareItemByLocalId<qevercloud::Notebook>(notebook.localId()));

        if ((remainingNotebookIt == remainingNotebooks.constEnd()) &&
            notebook.isLocallyModified())
        {
            QFAIL(
                "One of extra notebooks which was dirty has not survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be preserved");
        }
        else if (
            (remainingNotebookIt != remainingNotebooks.constEnd()) &&
            !notebook.isLocallyModified())
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
            CompareItemByGuid<qevercloud::Notebook>(syncedGuid));

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
        if (tag.guid()) {
            auto guidIt = m_syncedGuids.m_syncedTagGuids.find(*tag.guid());
            if (guidIt != m_syncedGuids.m_syncedTagGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced tag which survived the purge performed "
                "by FullSyncStaleDataItemsExpunger and kept its guid");
        }

        if (!tag.isLocallyModified()) {
            QFAIL(
                "Found a non-synced and non-dirty tag which survived "
                "the purge performed by FullSyncStaleDataItemsExpunger");
        }

        auto extraTagIt = std::find_if(
            nonSyncedTags.constBegin(), nonSyncedTags.constEnd(),
            CompareItemByLocalId<qevercloud::Tag>(tag.localId()));

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
            CompareItemByLocalId<qevercloud::Tag>(tag.localId()));

        if ((remainingTagIt == remainingTags.constEnd()) &&
            tag.isLocallyModified()) {
            QFAIL(
                "One of extra tags which was dirty has not survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be preserved");
        }
        else if (
            (remainingTagIt != remainingTags.constEnd()) &&
            !tag.isLocallyModified())
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
            CompareItemByGuid<qevercloud::Tag>(syncedGuid));

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
        if (search.guid()) {
            auto guidIt =
                m_syncedGuids.m_syncedSavedSearchGuids.find(*search.guid());

            if (guidIt != m_syncedGuids.m_syncedSavedSearchGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced saved search which survived the purge "
                "performed by FullSyncStaleDataItemsExpunger and kept its "
                "guid");
        }

        if (!search.isLocallyModified()) {
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
            CompareItemByLocalId<qevercloud::SavedSearch>(search.localId()));

        if ((remainingSavedSearchIt == remainingSavedSearches.constEnd()) &&
            search.isLocallyModified())
        {
            QFAIL(
                "One of extra saved searches which was dirty has not "
                "survived the purge performed by "
                "FullSyncStaleDataItemsExpunger even though it was intended "
                "to be preserved");
        }
        else if (
            (remainingSavedSearchIt != remainingSavedSearches.constEnd()) &&
            !search.isLocallyModified())
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
            CompareItemByGuid<qevercloud::SavedSearch>(syncedGuid));

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
        if (!note.notebookGuid() && note.notebookLocalId().isEmpty()) {
            QFAIL("Found a note without notebook guid and notebook local uid");
        }

        auto remainingNotebookIt = remainingNotebooks.constEnd();
        if (note.notebookGuid()) {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByGuid<qevercloud::Notebook>(*note.notebookGuid()));
        }
        else {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByLocalId<qevercloud::Notebook>(
                    note.notebookLocalId()));
        }

        if (Q_UNLIKELY(remainingNotebookIt == remainingNotebooks.constEnd())) {
            QFAIL(
                "Found a note which corresponding notebook has been expunged "
                "but the note still exists within the local storage");
        }

        if (note.guid()) {
            auto guidIt = m_syncedGuids.m_syncedNoteGuids.find(*note.guid());
            if (guidIt != m_syncedGuids.m_syncedNoteGuids.end()) {
                continue;
            }

            QFAIL(
                "Found a non-synced note which survived the purge performed "
                "by FullSyncStaleDataItemsExpunger and kept its guid");
        }

        if (!note.isLocallyModified()) {
            QFAIL(
                "Found a non-synced and non-dirty note which survived "
                "the purge performed by FullSyncStaleDataItemsExpunger");
        }
    }

    for (const auto & note: qAsConst(nonSyncedNotes)) {
        if (!note.notebookGuid() && note.notebookLocalId().isEmpty()) {
            QFAIL(
                "One of non-synced notes has no notebook guid and no "
                "notebook local uid");
        }

        auto remainingNotebookIt = remainingNotebooks.constEnd();
        if (note.notebookGuid()) {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByGuid<qevercloud::Notebook>(*note.notebookGuid()));
        }
        else {
            remainingNotebookIt = std::find_if(
                remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                CompareItemByLocalId<qevercloud::Notebook>(
                    note.notebookLocalId()));
        }

        auto remainingNoteIt = std::find_if(
            remainingNotes.constBegin(), remainingNotes.constEnd(),
            CompareItemByLocalId<qevercloud::Note>(note.localId()));

        if ((remainingNoteIt == remainingNotes.constEnd()) &&
            note.isLocallyModified() &&
            (remainingNotebookIt != remainingNotebooks.constEnd()))
        {
            QFAIL(
                "One of extra notes which was dirty has not survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be preserved");
        }
        else if (
            (remainingNoteIt != remainingNotes.constEnd()) &&
            !note.isLocallyModified())
        {
            QFAIL(
                "One of extra notes which was not dirty has survived "
                "the purge performed by FullSyncStaleDataItemsExpunger even "
                "though it was intended to be expunged");
        }
    }

    for (const auto & syncedGuid: qAsConst(m_syncedGuids.m_syncedNoteGuids)) {
        auto remainingNoteIt = std::find_if(
            remainingNotes.constBegin(), remainingNotes.constEnd(),
            CompareItemByGuid<qevercloud::Note>(syncedGuid));

        if (remainingNoteIt == remainingNotes.constEnd()) {
            QFAIL(
                "Could not find a note within the remaining ones which guid "
                "was marked as synced");
        }
    }
}

} // namespace quentier::test
