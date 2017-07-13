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

#include "FullSyncStaleDataItemsExpungerTester.h"
#include "../synchronization/NotebookSyncCache.h"
#include "../synchronization/TagSyncCache.h"
#include "../synchronization/SavedSearchSyncCache.h"
#include <quentier/utility/UidGenerator.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <QtTest/QTest>
#include <QTimer>

// 10 minutes should be enough
#define MAX_ALLOWED_MILLISECONDS 600000

namespace quentier {
namespace test {

template <class T>
struct CompareItemByLocalUid
{
    CompareItemByLocalUid(const QString & targetLocalUid) :
        m_targetLocalUid(targetLocalUid)
    {}

    bool operator()(const T & item) const { return item.localUid() == m_targetLocalUid; }

    QString     m_targetLocalUid;
};

template <class T>
struct CompareItemByGuid
{
    CompareItemByGuid(const QString & targetGuid) :
        m_targetGuid(targetGuid)
    {}

    bool operator()(const T & item) const { return (item.hasGuid() && (item.guid() == m_targetGuid)); }

    QString     m_targetGuid;
};

FullSyncStaleDataItemsExpungerTester::FullSyncStaleDataItemsExpungerTester(QObject * parent) :
    QObject(parent),
    m_testAccount(QStringLiteral("FullSyncStaleDataItemsExpungerTesterFakeUser"),
                  Account::Type::Evernote, qevercloud::UserID(1)),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_syncedGuids(),
    m_notebookSyncCaches(),
    m_tagSyncCaches(),
    m_pSavedSearchSyncCache(Q_NULLPTR)
{}

FullSyncStaleDataItemsExpungerTester::~FullSyncStaleDataItemsExpungerTester()
{}

void FullSyncStaleDataItemsExpungerTester::init()
{
    m_testAccount = Account(m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1);
    m_pLocalStorageManagerAsync = new LocalStorageManagerAsync(m_testAccount, /* start from scratch = */ true,
                                                               /* override lock = */ false, this);
    m_pLocalStorageManagerAsync->init();

    NotebookSyncCache * pNotebookSyncCache = new NotebookSyncCache(*m_pLocalStorageManagerAsync, QString(), this);
    m_notebookSyncCaches << pNotebookSyncCache;

    TagSyncCache * pTagSyncCache = new TagSyncCache(*m_pLocalStorageManagerAsync, QString(), this);
    m_tagSyncCaches << pTagSyncCache;

    m_pSavedSearchSyncCache = new SavedSearchSyncCache(*m_pLocalStorageManagerAsync, this);
}

void FullSyncStaleDataItemsExpungerTester::cleanup()
{
    if (m_pLocalStorageManagerAsync) {
        m_pLocalStorageManagerAsync->deleteLater();
        m_pLocalStorageManagerAsync = Q_NULLPTR;
    }

    for(auto it = m_notebookSyncCaches.begin(),
        end = m_notebookSyncCaches.end(); it != end; ++it)
    {
        (*it)->deleteLater();
    }

    m_notebookSyncCaches.clear();

    for(auto it = m_tagSyncCaches.begin(),
        end = m_tagSyncCaches.end(); it != end; ++it)
    {
        (*it)->deleteLater();
    }

    m_tagSyncCaches.clear();

    if (m_pSavedSearchSyncCache) {
        m_pSavedSearchSyncCache->deleteLater();
        m_pSavedSearchSyncCache = Q_NULLPTR;
    }

    m_syncedGuids.m_syncedNotebookGuids.clear();
    m_syncedGuids.m_syncedTagGuids.clear();
    m_syncedGuids.m_syncedNoteGuids.clear();
    m_syncedGuids.m_syncedSavedSearchGuids.clear();
}

void FullSyncStaleDataItemsExpungerTester::testEmpty()
{
    doTest(/* use base data items = */ false, QList<Notebook>(), QList<Tag>(), QList<SavedSearch>(), QList<Note>());
}

void FullSyncStaleDataItemsExpungerTester::setupBaseDataItems()
{
    if (Q_UNLIKELY(!m_pLocalStorageManagerAsync)) {
        QFAIL("Detected null pointer to LocalStorageManagerAsync while trying to set up the base data items");
        return;
    }

    LocalStorageManager * pLocalStorageManager = m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Detected null pointer to LocalStorageManager while trying to set up the base data items");
        return;
    }

    Notebook firstNotebook;
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setName(QStringLiteral("First notebook"));
    firstNotebook.setUpdateSequenceNumber(42);
    firstNotebook.setLocal(false);
    firstNotebook.setDirty(false);

    Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second notebook"));
    secondNotebook.setUpdateSequenceNumber(43);
    secondNotebook.setLocal(false);
    secondNotebook.setDirty(false);

    Notebook thirdNotebook;
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
    firstNote.setContent(QStringLiteral("<en-note><h1>First note content</h1></en-note>"));
    firstNote.setUpdateSequenceNumber(51);
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setNotebookLocalUid(firstNotebook.localUid());
    firstNote.setLocal(false);
    firstNote.setDirty(false);

    Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note><h1>Second note content</h1></en-note>"));
    secondNote.setUpdateSequenceNumber(52);
    secondNote.setNotebookGuid(firstNotebook.guid());
    secondNote.setNotebookLocalUid(firstNotebook.localUid());
    secondNote.setLocal(false);
    secondNote.setDirty(false);

    Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setTitle(QStringLiteral("Third note"));
    thirdNote.setContent(QStringLiteral("<en-note><h1>Third note content</h1></en-note>"));
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
    fourthNote.setContent(QStringLiteral("<en-note><h1>Fourth note content</h1></en-note>"));
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
    fifthNote.setContent(QStringLiteral("<en-note><h1>Fifth note content</h1></en-note>"));
    fifthNote.setUpdateSequenceNumber(55);
    fifthNote.setNotebookGuid(thirdNotebook.guid());
    fifthNote.setNotebookLocalUid(thirdNotebook.localUid());
    fifthNote.setLocal(false);
    fifthNote.setDirty(false);

    ErrorString errorDescription;
    bool res = pLocalStorageManager->addNotebook(firstNotebook, errorDescription);
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

void FullSyncStaleDataItemsExpungerTester::doTest(const bool useBaseDataItems,
                                                  const QList<Notebook> & extraNotebooks,
                                                  const QList<Tag> & extraTags,
                                                  const QList<SavedSearch> & extraSavedSearches,
                                                  const QList<Note> & extraNotes)
{
    if (Q_UNLIKELY(!m_pLocalStorageManagerAsync)) {
        QFAIL("Detected null pointer to LocalStorageManagerAsync");
    }

    LocalStorageManager * pLocalStorageManager = m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Detected null pointer to LocalStorageManager");
    }

    if (Q_UNLIKELY(!m_pSavedSearchSyncCache)) {
        QFAIL("Detected null pointer to SavedSearchSyncCache");
    }

    if (useBaseDataItems) {
        setupBaseDataItems();
    }

    for(auto it = extraNotebooks.constBegin(), end = extraNotebooks.constEnd(); it != end; ++it)
    {
        Notebook notebook = *it;

        ErrorString errorDescription;
        bool res = pLocalStorageManager->addNotebook(notebook, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    for(auto it = extraTags.constBegin(), end = extraTags.constEnd(); it != end; ++it)
    {
        Tag tag = *it;

        ErrorString errorDescription;
        bool res = pLocalStorageManager->addTag(tag, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    for(auto it = extraSavedSearches.constBegin(), end = extraSavedSearches.constEnd(); it != end; ++it)
    {
        SavedSearch search = *it;

        ErrorString errorDescription;
        bool res = pLocalStorageManager->addSavedSearch(search, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    for(auto it = extraNotes.constBegin(), end = extraNotes.constEnd(); it != end; ++it)
    {
        Note note = *it;

        ErrorString errorDescription;
        bool res = pLocalStorageManager->addNote(note, errorDescription);
        if (!res) {
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }
    }

    FullSyncStaleDataItemsExpunger::Caches caches(m_notebookSyncCaches, m_tagSyncCaches, *m_pSavedSearchSyncCache);
    FullSyncStaleDataItemsExpunger expunger(*m_pLocalStorageManagerAsync, caches, m_syncedGuids);

    int expungerTestResult = -1;
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_MILLISECONDS);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&expunger, QNSIGNAL(FullSyncStaleDataItemsExpunger,finished), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));
        QObject::connect(&expunger, SIGNAL(failure(ErrorString)), &loop, SLOT(exitAsFailure()));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, &expunger, SLOT(start()));
        expungerTestResult = loop.exec();
    }

    if (expungerTestResult == -1) {
        QFAIL("Internal error: incorrect return status from FullSyncStaleDataItemsExpunger");
    }
    else if (expungerTestResult == EventLoopWithExitStatus::ExitStatus::Failure) {
        QFAIL("Detected failure during the asynchronous loop processing in FullSyncStaleDataItemsExpunger");
    }
    else if (expungerTestResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("FullSyncStaleDataItemsExpunger failed to finish in time");
    }

    // ====== Check remaining notebooks, verify each of them was intended to be preserved + verify all of notebooks
    // intended to be preserved were actually preserved ======

    ErrorString errorDescription;
    QList<Notebook> remainingNotebooks = pLocalStorageManager->listNotebooks(LocalStorageManager::ListAll, errorDescription);
    if (remainingNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    for(auto it = remainingNotebooks.constBegin(), end = remainingNotebooks.constEnd(); it != end; ++it)
    {
        const Notebook & notebook = *it;

        if (notebook.hasGuid())
        {
            auto guidIt = m_syncedGuids.m_syncedNotebookGuids.find(notebook.guid());
            if (guidIt != m_syncedGuids.m_syncedNotebookGuids.end()) {
                continue;
            }

            QFAIL("Found a non-synced notebook which survived the purge performed by FullSyncStaleDataItemsExpunger and kept its guid");
        }

        if (!notebook.isDirty()) {
            QFAIL("Found a non-synced and non-dirty notebook which survived the purge performed by FullSyncStaleDataItemsExpunger");
        }

        auto extraNotebookIt = std::find_if(extraNotebooks.constBegin(), extraNotebooks.constEnd(),
                                            CompareItemByLocalUid<Notebook>(notebook.localUid()));
        if (extraNotebookIt == extraNotebooks.constEnd()) {
            QFAIL("Found a notebook which survived the purge performed by FullSyncStaleDataItemsExpunger but has no guid "
                  "and is not contained within the list of extra notebooks");
        }
    }

    for(auto it = extraNotebooks.constBegin(), end = extraNotebooks.constEnd(); it != end; ++it)
    {
        const Notebook & notebook = *it;

        auto remainingNotebookIt = std::find_if(remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                                                CompareItemByLocalUid<Notebook>(notebook.localUid()));
        if ((remainingNotebookIt == remainingNotebooks.constEnd()) && notebook.isDirty()) {
            QFAIL("One of extra notebooks which was dirty has not survived the purge performed "
                  "by FullSyncStaleDataItemsExpunger even though it was intended to be preserved");
        }
        else if ((remainingNotebookIt != remainingNotebooks.constEnd()) && !notebook.isDirty()) {
            QFAIL("One of etxra notebooks which was not dirty has survived the purge performed "
                  "by FullSyncStaleDataItemsExpunger even though it was intended to be expunged");
        }
    }

    for(auto it = m_syncedGuids.m_syncedNotebookGuids.constBegin(),
        end = m_syncedGuids.m_syncedNotebookGuids.constEnd(); it != end; ++it)
    {
        const QString & syncedGuid = *it;

        auto remainingNotebookIt = std::find_if(remainingNotebooks.constBegin(), remainingNotebooks.constEnd(),
                                                CompareItemByGuid<Notebook>(syncedGuid));
        if (remainingNotebookIt == remainingNotebooks.constEnd()) {
            QFAIL("Could not find a notebook within the remaining ones which guid was marked as synced");
        }
    }

    // ====== Check remaining tags, verify each of them was intended to be preserved + verify all of tags intended
    // to be preserved were actually preserved ======

    errorDescription.clear();
    QList<Tag> remainingTags = pLocalStorageManager->listTags(LocalStorageManager::ListAll, errorDescription);
    if (remainingTags.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    for(auto it = remainingTags.constBegin(), end = remainingTags.constEnd(); it != end; ++it)
    {
        const Tag & tag = *it;

        if (tag.hasGuid())
        {
            auto guidIt = m_syncedGuids.m_syncedTagGuids.find(tag.guid());
            if (guidIt != m_syncedGuids.m_syncedTagGuids.end()) {
                continue;
            }

            QFAIL("Found a non-synced tag which survived the purge performed by FullSyncStaleDataItemsExpunger and kept its guid");
        }

        if (!tag.isDirty()) {
            QFAIL("Found a non-synced and non-dirty tag which survived the purge performed by FullSyncStaleDataItemsExpunger");
        }

        auto extraTagIt = std::find_if(extraTags.constBegin(), extraTags.constEnd(),
                                       CompareItemByLocalUid<Tag>(tag.localUid()));
        if (extraTagIt == extraTags.constEnd()) {
            QFAIL("Found a tag which survived the purge performed by FullSyncStaleDataItemsExpunger but has no guid "
                  "and is not contained within the list of extra tags");
        }
    }

    for(auto it = extraTags.constBegin(), end = extraTags.constEnd(); it != end; ++it)
    {
        const Tag & tag = *it;

        auto remainingTagIt = std::find_if(remainingTags.constBegin(), remainingTags.constEnd(),
                                           CompareItemByLocalUid<Tag>(tag.localUid()));
        if ((remainingTagIt == remainingTags.constEnd()) && tag.isDirty()) {
            QFAIL("One of extra tags which was dirty has not survived the purge performed "
                  "by FullSyncStaleDataItemsExpunger even though it was intended to be preserved");
        }
        else if ((remainingTagIt != remainingTags.constEnd()) && !tag.isDirty()) {
            QFAIL("One of extra tags which was not dirty has survived the purge performed "
                  "by FullSyncStaleDataItemsExpunger even though it was intended to be expunged");
        }
    }

    for(auto it = m_syncedGuids.m_syncedTagGuids.constBegin(),
        end = m_syncedGuids.m_syncedTagGuids.constEnd(); it != end; ++it)
    {
        const QString & syncedGuid = *it;

        auto remainingTagIt = std::find_if(remainingTags.constBegin(), remainingTags.constEnd(),
                                           CompareItemByGuid<Tag>(syncedGuid));
        if (remainingTagIt == remainingTags.constEnd()) {
            QFAIL("Could not find a tag within the remaining ones which guid was marked as synced");
        }
    }

    // TODO: implement similar checks for saved searches and notes
}

} // namespace test
} // namespace quentier
