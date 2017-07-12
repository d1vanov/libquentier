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
#include <quentier/utility/UidGenerator.h>
#include <QtTest/QTest>

namespace quentier {
namespace test {

FullSyncStaleDataItemsExpungerTester::FullSyncStaleDataItemsExpungerTester(QObject * parent) :
    QObject(parent),
    m_testAccount(QStringLiteral("FullSyncStaleDataItemsExpungerTesterFakeUser"),
                  Account::Type::Evernote, qevercloud::UserID(1)),
    m_pLocalStorageManagerAsync(Q_NULLPTR)
{}

FullSyncStaleDataItemsExpungerTester::~FullSyncStaleDataItemsExpungerTester()
{}

void FullSyncStaleDataItemsExpungerTester::init()
{
    m_testAccount = Account(m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1);
    m_pLocalStorageManagerAsync = new LocalStorageManagerAsync(m_testAccount, /* start from scratch = */ true,
                                                               /* override lock = */ false, this);
    m_pLocalStorageManagerAsync->init();
}

void FullSyncStaleDataItemsExpungerTester::cleanup()
{
    if (m_pLocalStorageManagerAsync) {
        m_pLocalStorageManagerAsync->deleteLater();
        m_pLocalStorageManagerAsync = Q_NULLPTR;
    }
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
}

} // namespace test
} // namespace quentier
