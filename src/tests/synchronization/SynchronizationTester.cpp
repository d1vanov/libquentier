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

#include "SynchronizationTester.h"
#include "SynchronizationManagerSignalsCatcher.h"
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/logging/QuentierLogger.h>
#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <QDateTime>
#include <QTimer>

// 10 minutes should be enough
#define MAX_ALLOWED_TEST_DURATION_MSEC 600000

#define CATCH_EXCEPTION() \
    catch(const std::exception & exception) { \
        SysInfo sysInfo; \
        QFAIL(qPrintable(QStringLiteral("Caught exception: ") + QString::fromUtf8(exception.what()) + \
                         QStringLiteral(", backtrace: ") + sysInfo.stackTrace())); \
    }

#if QT_VERSION >= 0x050000
inline void nullMessageHandler(QtMsgType type, const QMessageLogContext &, const QString & message) {
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#else
inline void nullMessageHandler(QtMsgType type, const char * message) {
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << QStringLiteral("\n");
    }
}
#endif

#define CHECK_EXPECTED(expected) \
    if (!catcher.expected()) { \
        QFAIL(qPrintable(QString::fromUtf8("SynchronizationManagerSignalsCatcher::") + \
                         QString::fromUtf8(#expected) + \
                         QString::fromUtf8(" unexpectedly returned false"))); \
    }

#define CHECK_UNEXPECTED(unexpected) \
    if (catcher.unexpected()) { \
        QFAIL(qPrintable(QString::fromUtf8("SynchronizationManagerSignalsCatcher::") + \
                         QString::fromUtf8(#unexpected) + \
                         QString::fromUtf8(" unexpectedly returned true"))); \
    }

namespace quentier {
namespace test {

SynchronizationTester::SynchronizationTester(QObject * parent) :
    QObject(parent),
    m_testAccount(QStringLiteral("SynchronizationTesterFakeUser"),
                  Account::Type::Evernote, qevercloud::UserID(1)),
    m_pLocalStorageManagerThread(Q_NULLPTR),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_pFakeNoteStore(Q_NULLPTR),
    m_pFakeUserStore(Q_NULLPTR),
    m_pFakeAuthenticationManager(Q_NULLPTR),
    m_pSynchronizationManager(Q_NULLPTR),
    m_detectedTestFailure(false)
{}

SynchronizationTester::~SynchronizationTester()
{}

void SynchronizationTester::init()
{
    m_pLocalStorageManagerThread = new QThread;
    m_pLocalStorageManagerThread->start();

    m_testAccount = Account(m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1);
    m_pLocalStorageManagerAsync = new LocalStorageManagerAsync(m_testAccount, /* start from scratch = */ true,
                                                               /* override lock = */ true);
    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pFakeUserStore = new FakeUserStore;
    m_pFakeUserStore->setEdamVersionMajor(qevercloud::EDAM_VERSION_MAJOR);
    m_pFakeUserStore->setEdamVersionMinor(qevercloud::EDAM_VERSION_MINOR);

    User user;
    user.setId(m_testAccount.id());
    user.setUsername(m_testAccount.name());
    user.setName(m_testAccount.displayName());
    user.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    user.setModificationTimestamp(user.creationTimestamp());
    user.setServiceLevel(qevercloud::ServiceLevel::BASIC);
    m_pFakeUserStore->setUser(m_testAccount.id(), user);

    qevercloud::AccountLimits limits;
    m_pFakeUserStore->setAccountLimits(qevercloud::ServiceLevel::BASIC, limits);

    QString authToken = UidGenerator::Generate();

    m_pFakeNoteStore = new FakeNoteStore(this);
    m_pFakeNoteStore->setAuthToken(authToken);

    m_pFakeAuthenticationManager = new FakeAuthenticationManager(this);
    m_pFakeAuthenticationManager->setUserId(m_testAccount.id());
    m_pFakeAuthenticationManager->setAuthToken(authToken);

    m_pSynchronizationManager = new SynchronizationManager(QStringLiteral("www.evernote.com"),
                                                           *m_pLocalStorageManagerAsync,
                                                           *m_pFakeAuthenticationManager,
                                                           m_pFakeNoteStore, m_pFakeUserStore);
    m_pSynchronizationManager->setAccount(m_testAccount);
}

void SynchronizationTester::cleanup()
{
    m_pSynchronizationManager->disconnect();
    m_pSynchronizationManager->deleteLater();
    m_pSynchronizationManager = Q_NULLPTR;

    m_pFakeNoteStore->disconnect();
    m_pFakeNoteStore->deleteLater();
    m_pFakeNoteStore = Q_NULLPTR;

    // NOTE: not deleting FakeUserStore intentionally, it is owned by SynchronizationManager
    m_pFakeUserStore = Q_NULLPTR;

    m_pFakeAuthenticationManager->disconnect();
    m_pFakeAuthenticationManager->deleteLater();
    m_pFakeAuthenticationManager = Q_NULLPTR;

    if (!m_pLocalStorageManagerThread->isFinished()) {
        m_pLocalStorageManagerThread->quit();
    }

    delete m_pLocalStorageManagerAsync;
    m_pLocalStorageManagerAsync = Q_NULLPTR;

    delete m_pLocalStorageManagerThread;
    m_pLocalStorageManagerThread = Q_NULLPTR;
}

void SynchronizationTester::initTestCase()
{
#if QT_VERSION >= 0x050000
    qInstallMessageHandler(nullMessageHandler);
#else
    qInstallMsgHandler(nullMessageHandler);
#endif
}

void SynchronizationTester::cleanupTestCase()
{
}

void SynchronizationTester::testSimpleRemoteToLocalFullSync()
{
    setUserOwnItemsToRemoteStorage();

    int testAsyncResult = -1;
    SynchronizationManagerSignalsCatcher catcher(*m_pSynchronizationManager);
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&catcher, QNSIGNAL(SynchronizationManagerSignalsCatcher,ready), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, m_pSynchronizationManager, QNSLOT(SynchronizationManager,synchronize));
        testAsyncResult = loop.exec();
    }

    if (testAsyncResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Synchronization test failed to finish in time");
    }
    else if (testAsyncResult != EventLoopWithExitStatus::ExitStatus::Success) {
        QFAIL("Internal error: incorrect return status from synchronization test");
    }

    if (catcher.receivedFailedSignal()) {
        QFAIL(qPrintable(QString::fromUtf8("Detected failure during the asynchronous synchronization loop: ") +
                         catcher.failureErrorDescription().nonLocalizedString()));
    }

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkEventsOrder(catcher);
    checkIdentityOfLocalAndRemoteItems();
}

void SynchronizationTester::testRemoteToLocalFullSyncWithLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();

    int testAsyncResult = -1;
    SynchronizationManagerSignalsCatcher catcher(*m_pSynchronizationManager);
    {
        QTimer timer;
        timer.setInterval(MAX_ALLOWED_TEST_DURATION_MSEC);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;
        QObject::connect(&timer, QNSIGNAL(QTimer,timeout), &loop, QNSLOT(EventLoopWithExitStatus,exitAsTimeout));
        QObject::connect(&catcher, QNSIGNAL(SynchronizationManagerSignalsCatcher,ready), &loop, QNSLOT(EventLoopWithExitStatus,exitAsSuccess));

        QTimer slotInvokingTimer;
        slotInvokingTimer.setInterval(500);
        slotInvokingTimer.setSingleShot(true);

        timer.start();
        slotInvokingTimer.singleShot(0, m_pSynchronizationManager, QNSLOT(SynchronizationManager,synchronize));
        testAsyncResult = loop.exec();
    }

    if (testAsyncResult == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Synchronization test failed to finish in time");
    }
    else if (testAsyncResult != EventLoopWithExitStatus::ExitStatus::Success) {
        QFAIL("Internal error: incorrect return status from synchronization test");
    }

    if (catcher.receivedFailedSignal()) {
        QFAIL(qPrintable(QString::fromUtf8("Detected failure during the asynchronous synchronization loop: ") +
                         catcher.failureErrorDescription().nonLocalizedString()));
    }

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
}

void SynchronizationTester::setUserOwnItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    SavedSearch firstSearch;
    firstSearch.setGuid(UidGenerator::Generate());
    firstSearch.setName(QStringLiteral("First saved search"));
    firstSearch.setQuery(QStringLiteral("First saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(firstSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    SavedSearch secondSearch;
    secondSearch.setGuid(UidGenerator::Generate());
    secondSearch.setName(QStringLiteral("Second saved search"));
    secondSearch.setQuery(QStringLiteral("Second saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(secondSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    SavedSearch thirdSearch;
    thirdSearch.setGuid(UidGenerator::Generate());
    thirdSearch.setName(QStringLiteral("Third saved search"));
    thirdSearch.setQuery(QStringLiteral("Third saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(thirdSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First tag"));
    res = m_pFakeNoteStore->setTag(firstTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second tag"));
    res = m_pFakeNoteStore->setTag(secondTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setParentGuid(secondTag.guid());
    thirdTag.setName(QStringLiteral("Third tag"));
    res = m_pFakeNoteStore->setTag(thirdTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook firstNotebook;
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setName(QStringLiteral("First notebook"));
    firstNotebook.setDefaultNotebook(true);
    res = m_pFakeNoteStore->setNotebook(firstNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second notebook"));
    secondNotebook.setDefaultNotebook(false);
    res = m_pFakeNoteStore->setNotebook(secondNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook thirdNotebook;
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setName(QStringLiteral("Third notebook"));
    thirdNotebook.setDefaultNotebook(false);
    res = m_pFakeNoteStore->setNotebook(thirdNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(QStringLiteral("<en-note><div>First note</div></en-note>"));
    firstNote.setContentLength(firstNote.content().size());
    firstNote.setContentHash(QCryptographicHash::hash(firstNote.content().toUtf8(), QCryptographicHash::Md5));
    firstNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    firstNote.setModificationTimestamp(firstNote.creationTimestamp());
    res = m_pFakeNoteStore->setNote(firstNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(firstNotebook.guid());
    secondNote.setTitle(QStringLiteral("Second note"));
    secondNote.setContent(QStringLiteral("<en-note><div>Second note</div></en-note>"));
    secondNote.setContentLength(secondNote.content().size());
    secondNote.setContentHash(QCryptographicHash::hash(secondNote.content().toUtf8(), QCryptographicHash::Md5));
    secondNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    secondNote.setModificationTimestamp(secondNote.creationTimestamp());
    secondNote.addTagGuid(firstTag.guid());
    secondNote.addTagGuid(secondTag.guid());
    res = m_pFakeNoteStore->setNote(secondNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setNotebookGuid(firstNotebook.guid());
    thirdNote.setTitle(QStringLiteral("Third note"));
    thirdNote.setContent(QStringLiteral("<en-note><div>Third note</div></en-note>"));
    thirdNote.setContentLength(thirdNote.content().size());
    thirdNote.setContentHash(QCryptographicHash::hash(thirdNote.content().toUtf8(), QCryptographicHash::Md5));
    thirdNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    thirdNote.setModificationTimestamp(thirdNote.creationTimestamp());
    thirdNote.addTagGuid(thirdTag.guid());

    Resource thirdNoteFirstResource;
    thirdNoteFirstResource.setGuid(UidGenerator::Generate());
    thirdNoteFirstResource.setNoteGuid(thirdNote.guid());
    thirdNoteFirstResource.setMime(QStringLiteral("text/plain"));
    thirdNoteFirstResource.setDataBody(QByteArray("Third note first resource data body"));
    thirdNoteFirstResource.setDataSize(thirdNoteFirstResource.dataBody().size());
    thirdNoteFirstResource.setDataHash(QCryptographicHash::hash(thirdNoteFirstResource.dataBody(), QCryptographicHash::Md5));
    thirdNote.addResource(thirdNoteFirstResource);

    res = m_pFakeNoteStore->setNote(thirdNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note fourthNote;
    fourthNote.setGuid(UidGenerator::Generate());
    fourthNote.setNotebookGuid(secondNotebook.guid());
    fourthNote.setTitle(QStringLiteral("Fourth note"));
    fourthNote.setContent(QStringLiteral("<en-note><div>Fourth note</div></en-note>"));
    fourthNote.setContentLength(fourthNote.content().size());
    fourthNote.setContentHash(QCryptographicHash::hash(fourthNote.content().toUtf8(), QCryptographicHash::Md5));
    fourthNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    fourthNote.setModificationTimestamp(fourthNote.creationTimestamp());
    res = m_pFakeNoteStore->setNote(fourthNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note fifthNote;
    fifthNote.setGuid(UidGenerator::Generate());
    fifthNote.setNotebookGuid(thirdNotebook.guid());
    fifthNote.setTitle(QStringLiteral("Fifth note"));
    fifthNote.setContent(QStringLiteral("<en-note><div>Fifth note</div></en-note>"));
    fifthNote.setContentLength(fifthNote.content().size());
    fifthNote.setContentHash(QCryptographicHash::hash(fifthNote.content().toUtf8(), QCryptographicHash::Md5));
    fifthNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    fifthNote.setModificationTimestamp(fifthNote.creationTimestamp());
    res = m_pFakeNoteStore->setNote(fifthNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));
}

void SynchronizationTester::setLinkedNotebookItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    LinkedNotebook firstLinkedNotebook;
    firstLinkedNotebook.setGuid(UidGenerator::Generate());
    firstLinkedNotebook.setUsername(QStringLiteral("First linked notebook owner"));
    firstLinkedNotebook.setShareName(QStringLiteral("First linked notebook share name"));
    firstLinkedNotebook.setShardId(UidGenerator::Generate());
    firstLinkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());
    firstLinkedNotebook.setNoteStoreUrl(QStringLiteral("First linked notebook fake note store URL"));
    firstLinkedNotebook.setWebApiUrlPrefix(QStringLiteral("First linked notebook fake web API URL prefix"));
    res = m_pFakeNoteStore->setLinkedNotebook(firstLinkedNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));
    m_pFakeNoteStore->setLinkedNotebookAuthToken(firstLinkedNotebook.username(), UidGenerator::Generate());

    LinkedNotebook secondLinkedNotebook;
    secondLinkedNotebook.setGuid(UidGenerator::Generate());
    secondLinkedNotebook.setUsername(QStringLiteral("Second linked notebook owner"));
    secondLinkedNotebook.setShareName(QStringLiteral("Second linked notebook share name"));
    secondLinkedNotebook.setShardId(UidGenerator::Generate());
    secondLinkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());
    secondLinkedNotebook.setNoteStoreUrl(QStringLiteral("Second linked notebook fake note store URL"));
    secondLinkedNotebook.setWebApiUrlPrefix(QStringLiteral("Second linked notebook fake web API URL prefix"));
    res = m_pFakeNoteStore->setLinkedNotebook(secondLinkedNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));
    m_pFakeNoteStore->setLinkedNotebookAuthToken(secondLinkedNotebook.username(), UidGenerator::Generate());

    LinkedNotebook thirdLinkedNotebook;
    thirdLinkedNotebook.setGuid(UidGenerator::Generate());
    thirdLinkedNotebook.setUsername(QStringLiteral("Third linked notebook owner"));
    thirdLinkedNotebook.setShareName(QStringLiteral("Third linked notebook share name"));
    thirdLinkedNotebook.setShardId(UidGenerator::Generate());
    thirdLinkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());
    thirdLinkedNotebook.setNoteStoreUrl(QStringLiteral("Third linked notebook fake note store URL"));
    thirdLinkedNotebook.setWebApiUrlPrefix(QStringLiteral("Third linked notebook fake web API URL prefix"));
    res = m_pFakeNoteStore->setLinkedNotebook(thirdLinkedNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));
    m_pFakeNoteStore->setLinkedNotebookAuthToken(thirdLinkedNotebook.username(), UidGenerator::Generate());

    Tag firstLinkedNotebookFirstTag;
    firstLinkedNotebookFirstTag.setGuid(UidGenerator::Generate());
    firstLinkedNotebookFirstTag.setName(QStringLiteral("First linked notebook first tag"));
    firstLinkedNotebookFirstTag.setLinkedNotebookGuid(firstLinkedNotebook.guid());
    res = m_pFakeNoteStore->setTag(firstLinkedNotebookFirstTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag firstLinkedNotebookSecondTag;
    firstLinkedNotebookSecondTag.setGuid(UidGenerator::Generate());
    firstLinkedNotebookSecondTag.setName(QStringLiteral("First linked notebook second tag"));
    firstLinkedNotebookSecondTag.setLinkedNotebookGuid(firstLinkedNotebook.guid());
    res = m_pFakeNoteStore->setTag(firstLinkedNotebookSecondTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag firstLinkedNotebookThirdTag;
    firstLinkedNotebookThirdTag.setGuid(UidGenerator::Generate());
    firstLinkedNotebookThirdTag.setName(QStringLiteral("First linked notebook third tag"));
    firstLinkedNotebookThirdTag.setLinkedNotebookGuid(firstLinkedNotebook.guid());
    firstLinkedNotebookThirdTag.setParentGuid(firstLinkedNotebookSecondTag.guid());
    res = m_pFakeNoteStore->setTag(firstLinkedNotebookThirdTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag secondLinkedNotebookFirstTag;
    secondLinkedNotebookFirstTag.setGuid(UidGenerator::Generate());
    secondLinkedNotebookFirstTag.setName(QStringLiteral("Second linked notebook first tag"));
    secondLinkedNotebookFirstTag.setLinkedNotebookGuid(secondLinkedNotebook.guid());
    res = m_pFakeNoteStore->setTag(secondLinkedNotebookFirstTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag secondLinkedNotebookSecondTag;
    secondLinkedNotebookSecondTag.setGuid(UidGenerator::Generate());
    secondLinkedNotebookSecondTag.setName(QStringLiteral("Second linked notebook second tag"));
    secondLinkedNotebookSecondTag.setLinkedNotebookGuid(secondLinkedNotebook.guid());
    res = m_pFakeNoteStore->setTag(secondLinkedNotebookSecondTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag thirdLinkedNotebookFirstTag;
    thirdLinkedNotebookFirstTag.setGuid(UidGenerator::Generate());
    thirdLinkedNotebookFirstTag.setName(QStringLiteral("Third linked notebook first tag"));
    thirdLinkedNotebookFirstTag.setLinkedNotebookGuid(thirdLinkedNotebook.guid());
    res = m_pFakeNoteStore->setTag(thirdLinkedNotebookFirstTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook firstNotebook;
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setName(QStringLiteral("First linked notebook"));
    firstNotebook.setDefaultNotebook(false);
    firstNotebook.setLinkedNotebookGuid(firstLinkedNotebook.guid());
    res = m_pFakeNoteStore->setNotebook(firstNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second linked notebook"));
    secondNotebook.setDefaultNotebook(false);
    secondNotebook.setLinkedNotebookGuid(secondLinkedNotebook.guid());
    res = m_pFakeNoteStore->setNotebook(secondNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook thirdNotebook;
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setName(QStringLiteral("Third linked notebook"));
    thirdNotebook.setDefaultNotebook(false);
    thirdNotebook.setLinkedNotebookGuid(thirdLinkedNotebook.guid());
    res = m_pFakeNoteStore->setNotebook(thirdNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setTitle(QStringLiteral("First linked notebook first note"));
    firstNote.setContent(QStringLiteral("<en-note><div>First linked notebook first note</div></en-note>"));
    firstNote.setContentLength(firstNote.content().size());
    firstNote.setContentHash(QCryptographicHash::hash(firstNote.content().toUtf8(), QCryptographicHash::Md5));
    firstNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    firstNote.setModificationTimestamp(firstNote.creationTimestamp());
    res = m_pFakeNoteStore->setNote(firstNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(firstNotebook.guid());
    secondNote.setTitle(QStringLiteral("First linked notebook second note"));
    secondNote.setContent(QStringLiteral("<en-note><div>First linked notebook second note</div></en-note>"));
    secondNote.setContentLength(secondNote.content().size());
    secondNote.setContentHash(QCryptographicHash::hash(secondNote.content().toUtf8(), QCryptographicHash::Md5));
    secondNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    secondNote.setModificationTimestamp(secondNote.creationTimestamp());
    secondNote.addTagGuid(firstLinkedNotebookFirstTag.guid());
    secondNote.addTagGuid(firstLinkedNotebookSecondTag.guid());
    res = m_pFakeNoteStore->setNote(secondNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setNotebookGuid(secondNotebook.guid());
    thirdNote.setTitle(QStringLiteral("Second linked notebook first note"));
    thirdNote.setContent(QStringLiteral("<en-note><div>Second linked notebook first note</div></en-note>"));
    thirdNote.setContentLength(thirdNote.content().size());
    thirdNote.setContentHash(QCryptographicHash::hash(thirdNote.content().toUtf8(), QCryptographicHash::Md5));
    thirdNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    thirdNote.setModificationTimestamp(thirdNote.creationTimestamp());
    thirdNote.addTagGuid(secondLinkedNotebookFirstTag.guid());

    Resource thirdNoteFirstResource;
    thirdNoteFirstResource.setGuid(UidGenerator::Generate());
    thirdNoteFirstResource.setNoteGuid(thirdNote.guid());
    thirdNoteFirstResource.setMime(QStringLiteral("text/plain"));
    thirdNoteFirstResource.setDataBody(QByteArray("Second linked notebook first note resource data body"));
    thirdNoteFirstResource.setDataSize(thirdNoteFirstResource.dataBody().size());
    thirdNoteFirstResource.setDataHash(QCryptographicHash::hash(thirdNoteFirstResource.dataBody(), QCryptographicHash::Md5));
    thirdNote.addResource(thirdNoteFirstResource);

    res = m_pFakeNoteStore->setNote(thirdNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note fourthNote;
    fourthNote.setGuid(UidGenerator::Generate());
    fourthNote.setNotebookGuid(secondNotebook.guid());
    fourthNote.setTitle(QStringLiteral("Second linked notebook second note"));
    fourthNote.setContent(QStringLiteral("<en-note><div>Second linked notebook second note</div></en-note>"));
    fourthNote.setContentLength(fourthNote.content().size());
    fourthNote.setContentHash(QCryptographicHash::hash(fourthNote.content().toUtf8(), QCryptographicHash::Md5));
    fourthNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    fourthNote.setModificationTimestamp(fourthNote.creationTimestamp());
    res = m_pFakeNoteStore->setNote(fourthNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note fifthNote;
    fifthNote.setGuid(UidGenerator::Generate());
    fifthNote.setNotebookGuid(thirdNotebook.guid());
    fifthNote.setTitle(QStringLiteral("Third linked notebook first note"));
    fifthNote.setContent(QStringLiteral("<en-note><div>Third linked notebook first note</div></en-note>"));
    fifthNote.setContentLength(fifthNote.content().size());
    fifthNote.setContentHash(QCryptographicHash::hash(fifthNote.content().toUtf8(), QCryptographicHash::Md5));
    fifthNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    fifthNote.setModificationTimestamp(fifthNote.creationTimestamp());
    res = m_pFakeNoteStore->setNote(fifthNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));
}

void SynchronizationTester::setNewItemsToLocalStorage()
{
    ErrorString errorDescription;
    bool res = false;

    SavedSearch firstLocalSavedSearch;
    firstLocalSavedSearch.setName(QStringLiteral("First local saved search"));
    firstLocalSavedSearch.setQuery(QStringLiteral("First local saved search query"));
    firstLocalSavedSearch.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addSavedSearch(firstLocalSavedSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    SavedSearch secondLocalSavedSearch;
    secondLocalSavedSearch.setName(QStringLiteral("Second local saved search"));
    secondLocalSavedSearch.setQuery(QStringLiteral("Second local saved search query"));
    secondLocalSavedSearch.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addSavedSearch(secondLocalSavedSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    SavedSearch thirdLocalSavedSearch;
    thirdLocalSavedSearch.setName(QStringLiteral("Third local saved search"));
    thirdLocalSavedSearch.setQuery(QStringLiteral("Third local saved searcg query"));
    thirdLocalSavedSearch.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addSavedSearch(secondLocalSavedSearch, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag firstLocalTag;
    firstLocalTag.setName(QStringLiteral("First local tag"));
    firstLocalTag.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(firstLocalTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag secondLocalTag;
    secondLocalTag.setName(QStringLiteral("Second local tag"));
    secondLocalTag.setParentLocalUid(firstLocalTag.localUid());
    secondLocalTag.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(secondLocalTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Tag thirdLocalTag;
    thirdLocalTag.setName(QStringLiteral("Third local tag"));
    thirdLocalTag.setParentLocalUid(secondLocalTag.localUid());
    thirdLocalTag.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(thirdLocalTag, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook firstLocalNotebook;
    firstLocalNotebook.setName(QStringLiteral("First local notebook"));
    firstLocalNotebook.setDefaultNotebook(false);
    firstLocalNotebook.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addNotebook(firstLocalNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook secondLocalNotebook;
    secondLocalNotebook.setName(QStringLiteral("Second local notebook"));
    secondLocalNotebook.setDefaultNotebook(false);
    secondLocalNotebook.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addNotebook(secondLocalNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook thirdLocalNotebook;
    thirdLocalNotebook.setName(QStringLiteral("Third local notebook"));
    thirdLocalNotebook.setDefaultNotebook(false);
    thirdLocalNotebook.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addNotebook(thirdLocalNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note firstLocalNote;
    firstLocalNote.setNotebookLocalUid(firstLocalNotebook.localUid());
    firstLocalNote.setTitle(QStringLiteral("First local note"));
    firstLocalNote.setContent(QStringLiteral("<en-note><div>First local note</div></en-note>"));
    firstLocalNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    firstLocalNote.setModificationTimestamp(firstLocalNote.modificationTimestamp());
    firstLocalNote.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(firstLocalNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note secondLocalNote;
    secondLocalNote.setNotebookLocalUid(firstLocalNotebook.localUid());
    secondLocalNote.setTitle(QStringLiteral("Second local note"));
    secondLocalNote.setContent(QStringLiteral("<en-note><div>Second local note</div></en-note>"));
    secondLocalNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    secondLocalNote.setModificationTimestamp(secondLocalNote.creationTimestamp());
    secondLocalNote.addTagLocalUid(firstLocalTag.localUid());
    secondLocalNote.addTagLocalUid(secondLocalTag.localUid());
    secondLocalNote.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(secondLocalNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note thirdLocalNote;
    thirdLocalNote.setNotebookLocalUid(secondLocalNotebook.localUid());
    thirdLocalNote.setTitle(QStringLiteral("Third local note"));
    thirdLocalNote.setContent(QStringLiteral("<en-note><div>Third local note</div></en-note>"));
    thirdLocalNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    thirdLocalNote.setModificationTimestamp(thirdLocalNote.creationTimestamp());
    thirdLocalNote.addTagLocalUid(thirdLocalTag.localUid());
    thirdLocalNote.setDirty(true);

    Resource thirdLocalNoteResource;
    thirdLocalNoteResource.setNoteLocalUid(thirdLocalNote.localUid());
    thirdLocalNoteResource.setMime(QStringLiteral("text/plain"));
    thirdLocalNoteResource.setDataBody(QByteArray("Third note first resource data body"));
    thirdLocalNote.addResource(thirdLocalNoteResource);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(thirdLocalNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Note fourthLocalNote;
    fourthLocalNote.setNotebookLocalUid(thirdLocalNotebook.localUid());
    fourthLocalNote.setTitle(QStringLiteral("Fourth local note"));
    fourthLocalNote.setContent(QStringLiteral("<en-note><div>Fourth local note</div></en-note>"));
    fourthLocalNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    fourthLocalNote.setModificationTimestamp(fourthLocalNote.creationTimestamp());
    fourthLocalNote.addTagLocalUid(secondLocalTag.localUid());
    fourthLocalNote.addTagLocalUid(thirdLocalTag.localUid());
    fourthLocalNote.setDirty(true);
    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(fourthLocalNote, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));
}

void SynchronizationTester::checkEventsOrder(const SynchronizationManagerSignalsCatcher & catcher)
{
    ErrorString errorDescription;
    if (!catcher.checkSyncChunkDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(QString::fromUtf8("Wrong sync chunk download progress order: ") + errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkLinkedNotebookSyncChunkDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(QString::fromUtf8("Wrong linked notebook sync chunk download progress order: ") + errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkNoteDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(QString::fromUtf8("Wrong note download progress order: ") + errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkLinkedNotebookNoteDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(QString::fromUtf8("Wrong linked notebook note download progress order: ") + errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkResourceDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(QString::fromUtf8("Wrong resource download progress order: ") + errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkLinkedNotebookResourceDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(QString::fromUtf8("Wrong linked notebook resource download progress order: ") + errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::checkIdentityOfLocalAndRemoteItems()
{
    // List stuff from local storage

    QHash<QString, qevercloud::SavedSearch> localSavedSearches;
    listSavedSearchesFromLocalStorage(0, localSavedSearches);

    QHash<QString, qevercloud::LinkedNotebook> localLinkedNotebooks;
    listLinkedNotebooksFromLocalStorage(0, localLinkedNotebooks);

    QStringList linkedNotebookGuids;
    linkedNotebookGuids.reserve(localLinkedNotebooks.size() + 1);
    linkedNotebookGuids << QString();
    for(auto it = localLinkedNotebooks.constBegin(), end = localLinkedNotebooks.constEnd(); it != end; ++it) {
        linkedNotebookGuids << it->guid.ref();
    }

    QHash<QString, qevercloud::Tag> localTags;
    QHash<QString, qevercloud::Notebook> localNotebooks;
    QHash<QString, qevercloud::Note> localNotes;
    for(auto it = linkedNotebookGuids.constBegin(), end = linkedNotebookGuids.constEnd(); it != end; ++it)
    {
        QHash<QString, qevercloud::Tag> currentLocalTags;
        listTagsFromLocalStorage(0, *it, currentLocalTags);
        for(auto cit = currentLocalTags.constBegin(), cend = currentLocalTags.constEnd(); cit != cend; ++cit) {
            localTags[cit.key()] = cit.value();
        }

        QHash<QString, qevercloud::Notebook> currentLocalNotebooks;
        listNotebooksFromLocalStorage(0, *it, currentLocalNotebooks);
        for(auto cit = currentLocalNotebooks.constBegin(), cend = currentLocalNotebooks.constEnd(); cit != cend; ++cit) {
            localNotebooks[cit.key()] = cit.value();
        }

        QHash<QString, qevercloud::Note> currentLocalNotes;
        listNotesFromLocalStorage(0, *it, currentLocalNotes);
        for(auto cit = currentLocalNotes.constBegin(), cend = currentLocalNotes.constEnd(); cit != cend; ++cit) {
            localNotes[cit.key()] = cit.value();
        }
    }

    // List stuff from remote storage

    QHash<QString,qevercloud::SavedSearch> remoteSavedSearches = m_pFakeNoteStore->savedSearches();
    QHash<QString,qevercloud::LinkedNotebook> remoteLinkedNotebooks = m_pFakeNoteStore->linkedNotebooks();
    QHash<QString,qevercloud::Tag> remoteTags = m_pFakeNoteStore->tags();
    QHash<QString,qevercloud::Notebook> remoteNotebooks = m_pFakeNoteStore->notebooks();
    QHash<QString,qevercloud::Note> remoteNotes = m_pFakeNoteStore->notes();

    QVERIFY2(localSavedSearches.size() == remoteSavedSearches.size(),
             qPrintable(QString::fromUtf8("The number of saved searches in local and remote storages doesn't match: ") +
                        QString::number(localSavedSearches.size()) + QString::fromUtf8(" local ones vs ") +
                        QString::number(remoteSavedSearches.size()) + QString::fromUtf8(" remote ones")));
    for(auto it = localSavedSearches.constBegin(), end = localSavedSearches.constEnd(); it != end; ++it)
    {
        auto rit = remoteSavedSearches.find(it.key());
        QVERIFY2(rit != remoteSavedSearches.end(),
                 qPrintable(QString::fromUtf8("Couldn't find one of local saved searches within the remote storage: ") +
                            ToString(it.value())));
        QVERIFY2(rit.value() == it.value(),
                 qPrintable(QString::fromUtf8("Found mismatch between local and remote saved searches: local one: ") +
                            ToString(it.value()) + QString::fromUtf8("\nRemote one: ") + ToString(rit.value())));
    }

    QVERIFY2(localLinkedNotebooks.size() == remoteLinkedNotebooks.size(),
             qPrintable(QString::fromUtf8("The number of linked notebooks in local and remote storages doesn't match: ") +
                        QString::number(localLinkedNotebooks.size()) + QString::fromUtf8(" local ones vs ") +
                        QString::number(remoteLinkedNotebooks.size()) + QString::fromUtf8(" remote ones")));
    for(auto it = localLinkedNotebooks.constBegin(), end = localLinkedNotebooks.constEnd(); it != end; ++it)
    {
        auto rit = remoteLinkedNotebooks.find(it.key());
        QVERIFY2(rit != remoteLinkedNotebooks.end(),
                 qPrintable(QString::fromUtf8("Couldn't find one of local linked notebooks within the remote storage: ") +
                            ToString(it.value())));
        QVERIFY2(rit.value() == it.value(),
                 qPrintable(QString::fromUtf8("Found mismatch between local and remote linked notebooks: local one: ") +
                            ToString(it.value()) + QString::fromUtf8("\nRemote one: ") + ToString(rit.value())));
    }

    QVERIFY2(localTags.size() == remoteTags.size(),
             qPrintable(QString::fromUtf8("The number of tags in local and remote storages doesn't match: ") +
                        QString::number(localTags.size()) + QString::fromUtf8(" local ones vs ") +
                        QString::number(remoteTags.size()) + QString::fromUtf8(" remote ones")));
    for(auto it = localTags.constBegin(), end = localTags.constEnd(); it != end; ++it)
    {
        auto rit = remoteTags.find(it.key());
        QVERIFY2(rit != remoteTags.end(),
                 qPrintable(QString::fromUtf8("Couldn't find one of local tags within the remote storage: ") +
                            ToString(it.value())));
        QVERIFY2(rit.value() == it.value(),
                 qPrintable(QString::fromUtf8("Found mismatch between local and remote tags: local one: ") +
                            ToString(it.value()) + QString::fromUtf8("\nRemote one: ") + ToString(rit.value())));
    }

    QVERIFY2(localNotebooks.size() == remoteNotebooks.size(),
             qPrintable(QString::fromUtf8("The number of notebooks in local and remote storages doesn't match: ") +
                        QString::number(localNotebooks.size()) + QString::fromUtf8(" local ones vs ") +
                        QString::number(remoteNotebooks.size()) + QString::fromUtf8(" remote ones")));
    for(auto it = localNotebooks.constBegin(), end = localNotebooks.constEnd(); it != end; ++it)
    {
        auto rit = remoteNotebooks.find(it.key());
        QVERIFY2(rit != remoteNotebooks.end(),
                 qPrintable(QString::fromUtf8("Couldn't find one of local notebooks within the remote storage: ") +
                            ToString(it.value())));
        QVERIFY2(rit.value() == it.value(),
                 qPrintable(QString::fromUtf8("Found mismatch between local and remote notebooks: local one: ") +
                            ToString(it.value()) + QString::fromUtf8("\nRemote one: ") + ToString(rit.value())));
    }

    QVERIFY2(localNotes.size() == remoteNotes.size(),
             qPrintable(QString::fromUtf8("The number of notes in local and remote storages doesn't match: ") +
                        QString::number(localNotes.size()) + QString::fromUtf8(" local ones vs ") +
                        QString::number(remoteNotes.size()) + QString::fromUtf8(" remote ones")));
    for(auto it = localNotes.constBegin(), end = localNotes.constEnd(); it != end; ++it)
    {
        auto rit = remoteNotes.find(it.key());
        QVERIFY2(rit != remoteNotes.end(),
                 qPrintable(QString::fromUtf8("Couldn't find one of local notes within the remote storage: ") +
                            ToString(it.value())));

        // NOTE: remote notes lack resource bodies, need to set these manually
        qevercloud::Note & remoteNote = rit.value();
        if (remoteNote.resources.isSet())
        {
            QList<qevercloud::Resource> resources = remoteNote.resources.ref();
            for(auto resIt = resources.begin(), resEnd = resources.end(); resIt != resEnd; ++resIt)
            {
                QString resourceGuid = resIt->guid.ref();
                const Resource * pResource = m_pFakeNoteStore->findResource(resourceGuid);
                QVERIFY2(pResource != Q_NULLPTR, "One of remote note's resources was not found");
                if (resIt->data.isSet()) {
                    resIt->data->body = pResource->dataBody();
                }
                if (resIt->recognition.isSet()) {
                    resIt->recognition->body = pResource->recognitionDataBody();
                }
                if (resIt->alternateData.isSet()) {
                    resIt->alternateData->body = pResource->alternateDataBody();
                }
            }

            remoteNote.resources = resources;
        }

        QVERIFY2(rit.value() == it.value(),
                 qPrintable(QString::fromUtf8("Found mismatch between local and remote notes: local one: ") +
                            ToString(it.value()) + QString::fromUtf8("\nRemote one: ") + ToString(rit.value())));
    }
}

void SynchronizationTester::listSavedSearchesFromLocalStorage(const qint32 afterUSN,
                                                              QHash<QString, qevercloud::SavedSearch> & savedSearches) const
{
    savedSearches.clear();

    const LocalStorageManager * pLocalStorageManager = m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    ErrorString errorDescription;
    QList<SavedSearch> searches = pLocalStorageManager->listSavedSearches(LocalStorageManager::ListAll, errorDescription);
    if (searches.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    savedSearches.reserve(searches.size());
    for(auto it = searches.constBegin(), end = searches.constEnd(); it != end; ++it)
    {
        const SavedSearch & search = *it;
        if (Q_UNLIKELY(!search.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!search.hasUpdateSequenceNumber() || (search.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        savedSearches[search.guid()] = search.qevercloudSavedSearch();
    }
}

void SynchronizationTester::listTagsFromLocalStorage(const qint32 afterUSN, const QString & linkedNotebookGuid,
                                                     QHash<QString, qevercloud::Tag> & tags) const
{
    tags.clear();

    const LocalStorageManager * pLocalStorageManager = m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    QString localLinkedNotebookGuid = QStringLiteral("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    ErrorString errorDescription;
    QList<Tag> localTags = pLocalStorageManager->listTags(LocalStorageManager::ListAll,
                                                          errorDescription, 0, 0, LocalStorageManager::ListTagsOrder::NoOrder,
                                                          LocalStorageManager::OrderDirection::Ascending,
                                                          localLinkedNotebookGuid);
    if (localTags.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    tags.reserve(localTags.size());
    for(auto it = localTags.constBegin(), end = localTags.constEnd(); it != end; ++it)
    {
        const Tag & tag = *it;
        if (Q_UNLIKELY(!tag.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!tag.hasUpdateSequenceNumber() || (tag.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        tags[tag.guid()] = tag.qevercloudTag();
    }
}

void SynchronizationTester::listNotebooksFromLocalStorage(const qint32 afterUSN, const QString & linkedNotebookGuid,
                                                          QHash<QString, qevercloud::Notebook> & notebooks) const
{
    notebooks.clear();

    const LocalStorageManager * pLocalStorageManager = m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    QString localLinkedNotebookGuid = QStringLiteral("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    ErrorString errorDescription;
    QList<Notebook> localNotebooks = pLocalStorageManager->listNotebooks(LocalStorageManager::ListAll,
                                                                         errorDescription, 0, 0, LocalStorageManager::ListNotebooksOrder::NoOrder,
                                                                         LocalStorageManager::OrderDirection::Ascending,
                                                                         localLinkedNotebookGuid);
    if (localNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    notebooks.reserve(localNotebooks.size());
    for(auto it = localNotebooks.constBegin(), end = localNotebooks.constEnd(); it != end; ++it)
    {
        const Notebook & notebook = *it;
        if (Q_UNLIKELY(!notebook.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!notebook.hasUpdateSequenceNumber() || (notebook.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        notebooks[notebook.guid()] = notebook.qevercloudNotebook();
    }
}

void SynchronizationTester::listNotesFromLocalStorage(const qint32 afterUSN, const QString & linkedNotebookGuid,
                                                      QHash<QString, qevercloud::Note> & notes) const
{
    notes.clear();

    const LocalStorageManager * pLocalStorageManager = m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    QString localLinkedNotebookGuid = QStringLiteral("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    ErrorString errorDescription;
    QList<Note> localNotes = pLocalStorageManager->listNotes(LocalStorageManager::ListAll,
                                                             errorDescription, true, 0, 0, LocalStorageManager::ListNotesOrder::NoOrder,
                                                             LocalStorageManager::OrderDirection::Ascending,
                                                             localLinkedNotebookGuid);
    if (localNotes.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    notes.reserve(localNotes.size());
    for(auto it = localNotes.constBegin(), end = localNotes.constEnd(); it != end; ++it)
    {
        const Note & note = *it;
        if (Q_UNLIKELY(!note.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!note.hasUpdateSequenceNumber() || (note.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        notes[note.guid()] = note.qevercloudNote();
    }
}

void SynchronizationTester::listLinkedNotebooksFromLocalStorage(const qint32 afterUSN,
                                                                QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks) const
{
    linkedNotebooks.clear();

    const LocalStorageManager * pLocalStorageManager = m_pLocalStorageManagerAsync->localStorageManager();
    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    ErrorString errorDescription;
    QList<LinkedNotebook> localLinkedNotebooks = pLocalStorageManager->listLinkedNotebooks(LocalStorageManager::ListAll,
                                                                                           errorDescription, 0, 0, LocalStorageManager::ListLinkedNotebooksOrder::NoOrder,
                                                                                           LocalStorageManager::OrderDirection::Ascending);
    if (localLinkedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    linkedNotebooks.reserve(localLinkedNotebooks.size());
    for(auto it = localLinkedNotebooks.constBegin(), end = localLinkedNotebooks.constEnd(); it != end; ++it)
    {
        const LinkedNotebook & linkedNotebook = *it;
        if (Q_UNLIKELY(!linkedNotebook.hasGuid())) {
            continue;
        }

        if ((afterUSN > 0) && (!linkedNotebook.hasUpdateSequenceNumber() || (linkedNotebook.updateSequenceNumber() <= afterUSN))) {
            continue;
        }

        linkedNotebooks[linkedNotebook.guid()] = linkedNotebook.qevercloudLinkedNotebook();
    }
}

} // namespace test
} // namespace quentier
