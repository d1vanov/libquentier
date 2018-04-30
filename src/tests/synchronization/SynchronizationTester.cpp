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
        QFAIL("SynchronizationManagerSignalsCatcher::expected unexpectedly returned false"); \
    }

#define CHECK_UNEXPECTED(unexpected) \
    if (catcher.unexpected()) { \
        QFAIL("SynchronizationManagerSignalsCatcher::unexpected unexpectedly returned true"); \
    }

namespace quentier {
namespace test {

void checkOrder(const SynchronizationManagerSignalsCatcher & catcher)
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
    m_testAccount = Account(m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1);
    m_pLocalStorageManagerAsync = new LocalStorageManagerAsync(m_testAccount, /* start from scratch = */ true,
                                                               /* override lock = */ false, this);
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
    m_pFakeUserStore->setUser(m_testAccount.id(), user);

    qevercloud::AccountLimits limits;
    m_pFakeUserStore->setAccountLimits(qevercloud::ServiceLevel::BASIC, limits);

    m_pFakeNoteStore = new FakeNoteStore(this);
    m_pFakeAuthenticationManager = new FakeAuthenticationManager(this);

    m_pSynchronizationManager = new SynchronizationManager(QStringLiteral("https://www.evernote.com"),
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

    m_pLocalStorageManagerAsync->disconnect();
    m_pLocalStorageManagerAsync->deleteLater();
    m_pLocalStorageManagerAsync = Q_NULLPTR;
}

void SynchronizationTester::initTestCase()
{
    m_pLocalStorageManagerThread = new QThread;
    m_pLocalStorageManagerThread->start();

#if QT_VERSION >= 0x050000
    qInstallMessageHandler(nullMessageHandler);
#else
    qInstallMsgHandler(nullMessageHandler);
#endif
}

void SynchronizationTester::cleanupTestCase()
{
    if (!m_pLocalStorageManagerThread->isFinished()) {
        QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,finished),
                         m_pLocalStorageManagerThread, QNSLOT(QThread,deleteLater));
    }
    else {
        delete m_pLocalStorageManagerThread;
    }

    m_pLocalStorageManagerThread = Q_NULLPTR;
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
    CHECK_EXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)

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

    checkOrder(catcher);

    // TODO: continue here: check the coincidence of items from remote and local storages
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
    res = m_pFakeNoteStore->setNotebook(firstNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second notebook"));
    res = m_pFakeNoteStore->setNotebook(secondNotebook, errorDescription);
    QVERIFY2(res == true, qPrintable(errorDescription.nonLocalizedString()));

    Notebook thirdNotebook;
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setName(QStringLiteral("Third notebook"));
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

} // namespace test
} // namespace quentier
