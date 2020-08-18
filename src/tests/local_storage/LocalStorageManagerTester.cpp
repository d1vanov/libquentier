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

#include "LocalStorageManagerTester.h"

#include "LocalStorageManagerAsyncTests.h"
#include "LocalStorageManagerBasicTests.h"
#include "LocalStorageManagerListTests.h"
#include "LocalStorageManagerNoteSearchQueryTest.h"
#include "NoteSearchQueryParsingTest.h"

#include <quentier/types/RegisterMetatypes.h>
#include <quentier/utility/SysInfo.h>

#include <QTextStream>
#include <QtTest/QTest>

// 10 minutes should be enough
#define MAX_ALLOWED_TEST_DURATION_MSEC 600000

#define CATCH_EXCEPTION()                                                      \
    catch (const std::exception & exception) {                                 \
        SysInfo sysInfo;                                                       \
        QFAIL(qPrintable(                                                      \
            QStringLiteral("Caught exception: ") +                             \
            QString::fromUtf8(exception.what()) +                              \
            QStringLiteral(", backtrace: ") + sysInfo.stackTrace()));          \
    }

inline void messageHandler(
    QtMsgType type, const QMessageLogContext &, const QString & message)
{
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}

namespace quentier {
namespace test {

LocalStorageManagerTester::LocalStorageManagerTester(QObject * parent) :
    QObject(parent)
{}

LocalStorageManagerTester::~LocalStorageManagerTester() {}

void LocalStorageManagerTester::init()
{
    registerMetatypes();
    qInstallMessageHandler(messageHandler);
}

void LocalStorageManagerTester::noteSearchQueryTest()
{
    try {
        QString error;
        bool res = NoteSearchQueryParsingTest(error);
        QVERIFY2(res == true, qPrintable(error));
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerNoteSearchQueryTest()
{
    try {
        QString error;
        bool res = LocalStorageManagerNoteSearchQueryTest(error);
        if (!res) {
            QFAIL(qPrintable(error));
        }
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualSavedSearchTest()
{
    try {
        TestSavedSearchAddFindUpdateExpungeInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::
    localStorageManagerFindSavedSearchByNameWithDiacriticsTest()
{
    try {
        TestFindSavedSearchByNameWithDiacritics();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::
    localStorageManagerIndividualLinkedNotebookTest()
{
    try {
        TestLinkedNotebookAddFindUpdateExpungeInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualTagTest()
{
    try {
        TestTagAddFindUpdateExpungeInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::
    localStorageManagerFindTagByNameWithDiacriticsTest()
{
    try {
        TestFindTagByNameWithDiacritics();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualResourceTest()
{
    try {
        TestResourceAddFindUpdateExpungeInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagedIndividualNoteTest()
{
    try {
        TestNoteAddFindUpdateDeleteExpungeInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerIndividualNotebookTest()
{
    try {
        TestNotebookAddFindUpdateDeleteExpungeInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::
    localStorageManagerFindNotebookByNameWithDiacriticsTest()
{
    try {
        TestFindNotebookByNameWithDiacritics();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagedIndividualUserTest()
{
    try {
        TestUserAddFindUpdateDeleteExpungeInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerSequentialUpdatesTest()
{
    try {
        TestSequentialUpdatesInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAccountHighUsnTest()
{
    try {
        TestAccountHighUsnInLocalStorage();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAddNoteWithoutLocalUidTest()
{
    try {
        TestAddingNoteWithoutLocalUid();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerNoteTagIdsComplementTest()
{
    try {
        TestNoteTagIdsComplementWhenAddingAndUpdatingNote();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListSavedSearchesTest()
{
    try {
        TestListSavedSearches();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListLinkedNotebooksTest()
{
    try {
        TestListLinkedNotebooks();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListTagsTest()
{
    try {
        TestListTags();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::
    localStorageManagerListTagsWithNoteLocalUidsTest()
{
    try {
        TestListTagsWithNoteLocalUids();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListAllSharedNotebooksTest()
{
    try {
        TestListAllSharedNotebooks();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListAllTagsPerNoteTest()
{
    try {
        TestListAllTagsPerNote();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListNotesTest()
{
    try {
        TestListNotes();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerListNotebooksTest()
{
    try {
        TestListNotebooks();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::
    localStorageManagerExpungeNotelessTagsFromLinkedNotebooksTest()
{
    try {
        TestExpungeNotelessTagsFromLinkedNotebooks();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncSavedSearchesTest()
{
    try {
        TestSavedSearhAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncLinkedNotebooksTest()
{
    try {
        TestLinkedNotebookAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncTagsTest()
{
    try {
        TestTagAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncUsersTest()
{
    try {
        TestUserAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncNotebooksTest()
{
    try {
        TestNotebookAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncNotesTest()
{
    try {
        TestNoteAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageManagerAsyncResourceTest()
{
    try {
        TestResourceAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::
    localStorageManagerAsyncNoteNotebookAndTagListTrackingTest()
{
    try {
        TestNoteNotebookAndTagListTrackingAsync();
    }
    CATCH_EXCEPTION();
}

void LocalStorageManagerTester::localStorageCacheManagerTest()
{
    try {
        TestCacheAsync();
    }
    CATCH_EXCEPTION();
}

} // namespace test
} // namespace quentier
