/*
 * Copyright 2018-2021 Dmitry Ivanov
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

#include "../../synchronization/SynchronizationShared.h"

#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/DateTime.h>
#include <quentier/utility/EventLoopWithExitStatus.h>
#include <quentier/utility/TagSortByParentChildRelations.h>
#include <quentier/utility/UidGenerator.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QTextStream>
#include <QTimer>
#include <QtTest/QtTest>

#include <iostream>

// 10 minutes should be enough
constexpr int gTestMaxMilliseconds = 600000;

static const QString gModifiedLocallySuffix =
    QStringLiteral("_modified_locally");

static const QString gModifiedRemotelySuffix =
    QStringLiteral("_modified_remotely");

#define CATCH_EXCEPTION()                                                      \
    catch (const std::exception & exception) {                                 \
        SysInfo sysInfo;                                                       \
        QFAIL(qPrintable(                                                      \
            QStringLiteral("Caught exception: ") +                             \
            QString::fromUtf8(exception.what()) +                              \
            QStringLiteral(", backtrace: ") + sysInfo.stackTrace()));          \
    }

inline void messageHandler(
    QtMsgType type, const QMessageLogContext & /* context */,
    const QString & message)
{
    if (type != QtDebugMsg) {
        QTextStream(stdout) << message << "\n";
    }
}

#define CHECK_EXPECTED(expected)                                               \
    if (!catcher.expected()) {                                                 \
        QFAIL(qPrintable(                                                      \
            QString::fromUtf8("SynchronizationManagerSignalsCatcher::") +      \
            QString::fromUtf8(#expected) +                                     \
            QString::fromUtf8(" unexpectedly returned false")));               \
    }

#define CHECK_UNEXPECTED(unexpected)                                           \
    if (catcher.unexpected()) {                                                \
        QFAIL(qPrintable(                                                      \
            QString::fromUtf8("SynchronizationManagerSignalsCatcher::") +      \
            QString::fromUtf8(#unexpected) +                                   \
            QString::fromUtf8(" unexpectedly returned true")));                \
    }

namespace quentier::test {

SynchronizationTester::SynchronizationTester(QObject * parent) :
    QObject(parent), m_testAccount(
                         QStringLiteral("SynchronizationTesterFakeUser"),
                         Account::Type::Evernote, qevercloud::UserID(1))
{}

SynchronizationTester::~SynchronizationTester() = default;

void SynchronizationTester::init()
{
    QuentierRestartLogging();

    m_testAccount = Account(
        m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1,
        Account::EvernoteAccountType::Free, QStringLiteral("www.evernote.com"));

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase |
        LocalStorageManager::StartupOption::OverrideLock);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(m_testAccount, startupOptions);

    m_pLocalStorageManagerAsync->init();

    m_pFakeUserStore = std::make_shared<FakeUserStore>();
    m_pFakeUserStore->setEdamVersionMajor(qevercloud::EDAM_VERSION_MAJOR);
    m_pFakeUserStore->setEdamVersionMinor(qevercloud::EDAM_VERSION_MINOR);

    qevercloud::User user;
    user.setId(m_testAccount.id());
    user.setUsername(m_testAccount.name());
    user.setName(m_testAccount.displayName());
    user.setCreated(QDateTime::currentMSecsSinceEpoch());
    user.setUpdated(*user.created());
    user.setServiceLevel(qevercloud::ServiceLevel::BASIC);
    m_pFakeUserStore->setUser(m_testAccount.id(), user);

    qevercloud::AccountLimits limits;
    m_pFakeUserStore->setAccountLimits(qevercloud::ServiceLevel::BASIC, limits);

    const QString authToken = UidGenerator::Generate();

    m_pFakeNoteStore = std::make_shared<FakeNoteStore>(this);
    m_pFakeNoteStore->setAuthToken(authToken);

    m_pFakeAuthenticationManager =
        std::make_shared<FakeAuthenticationManager>(this);

    m_pFakeAuthenticationManager->setUserId(m_testAccount.id());
    m_pFakeAuthenticationManager->setAuthToken(authToken);

    m_pFakeKeychainService = std::make_shared<FakeKeychainService>(this);

    m_pSyncStateStorage = newSyncStateStorage(this);

    m_pSynchronizationManager = new SynchronizationManager(
        QStringLiteral("www.evernote.com"), *m_pLocalStorageManagerAsync,
        *m_pFakeAuthenticationManager, this, m_pFakeNoteStore, m_pFakeUserStore,
        m_pFakeKeychainService, m_pSyncStateStorage);

    m_pSynchronizationManager->setAccount(m_testAccount);
}

void SynchronizationTester::cleanup()
{
    m_pSynchronizationManager->disconnect();
    m_pSynchronizationManager->deleteLater();
    m_pSynchronizationManager = nullptr;

    m_pFakeNoteStore.reset();
    m_pFakeUserStore.reset();
    m_pFakeAuthenticationManager.reset();
    m_pFakeKeychainService.reset();
    m_pSyncStateStorage.reset();

    delete m_pLocalStorageManagerAsync;
    m_pLocalStorageManagerAsync = nullptr;

    m_expectedSavedSearchNamesByGuid.clear();
    m_expectedTagNamesByGuid.clear();
    m_expectedNotebookNamesByGuid.clear();
    m_expectedNoteTitlesByGuid.clear();

    m_guidsOfLinkedNotebookNotesToExpungeToProduceNotelessLinkedNotebookTags
        .clear();

    m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged.clear();
}

void SynchronizationTester::initTestCase()
{
    qInstallMessageHandler(messageHandler);
}

void SynchronizationTester::cleanupTestCase() {}

void SynchronizationTester::testRemoteToLocalFullSyncWithUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::testRemoteToLocalFullSyncWithLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewRemoteItemsFromUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    QVERIFY(catcher.syncChunksDataCounters().size() == 1);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewRemoteItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);
    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedRemoteItemsFromUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources
    // as dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);
    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources
    // as dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedRemoteItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources as
    // dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedAndNewRemoteItemsFromUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToRemoteStorage();
    setNewUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources as
    // dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedAndNewRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedLinkedNotebookItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources as
    // dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedAndNewRemoteItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();
    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources as
    // dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalItemsFromUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewLinkedNotebookItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToLocalStorage();
    setNewLinkedNotebookItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalItemsFromUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedLinkedNotebookItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();
    setModifiedLinkedNotebookItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewAndModifiedLocalItemsFromUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();
    setNewUserOwnItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewAndModifiedLocalItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedLinkedNotebookItemsToLocalStorage();
    setNewLinkedNotebookItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewAndModifiedLocalItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();
    setModifiedLinkedNotebookItemsToLocalStorage();
    setNewUserOwnItemsToLocalStorage();
    setNewLinkedNotebookItemsToLocalStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalAndNewRemoteItemsFromUsersOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToLocalStorage();
    setNewUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalAndNewRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewLinkedNotebookItemsToLocalStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    QVERIFY(catcher.syncChunksDataCounters().size() == 1);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalAndNewRemoteItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToLocalStorage();
    setNewLinkedNotebookItemsToLocalStorage();
    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromUsersOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToLocalStorage();
    setModifiedUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewLinkedNotebookItemsToLocalStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewLocalAndModifiedRemoteItemsFromUsersOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToLocalStorage();
    setNewLinkedNotebookItemsToLocalStorage();
    setModifiedUserOwnItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromUsersOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();
    setNewUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedLinkedNotebookItemsToLocalStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    QVERIFY(catcher.syncChunksDataCounters().size() == 1);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalAndNewRemoteItemsFromUsersOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();
    setModifiedLinkedNotebookItemsToLocalStorage();
    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromUsersOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();
    setModifiedUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedLinkedNotebookItemsToLocalStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithModifiedLocalAndModifiedRemoteItemsWithoutConflictsFromUsersOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setModifiedUserOwnItemsToLocalStorage();
    setModifiedLinkedNotebookItemsToLocalStorage();
    setModifiedUserOwnItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithExpungedRemoteItemsFromUsersOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setExpungedUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)

    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithExpungedRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setExpungedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithExpungedRemoteItemsFromUsersOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setExpungedUserOwnItemsToRemoteStorage();
    setExpungedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromUserOwnDataOnly()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();
    setModifiedUserOwnItemsToRemoteStorage();
    setExpungedUserOwnItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources as
    // dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromLinkedNotebooksOnly()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewLinkedNotebookItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();
    setExpungedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources as
    // dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithNewModifiedAndExpungedRemoteItemsFromUserOwnDataAndLinkedNotebooks()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();
    setModifiedUserOwnItemsToRemoteStorage();
    setExpungedUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();
    setExpungedLinkedNotebookItemsToRemoteStorage();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)

    // NOTE: these are expected because the updates of remote resources
    // intentionally trigger marking the notes owning these updated resources as
    // dirty ones because otherwise it's kinda incosistent that resource was
    // added or updated but its note still has old information about it
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingSavedSearchesFromUserOwnDataOnlyWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingSavedSearchesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingTagsFromUserOwnDataOnlyWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingTagsFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataOnlyWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotebooksFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotesFromUserOwnDataOnlyWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)

    // These are expected because local conflicting note should have been
    // created and sent back to Evernote
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
    checkLocalCopiesOfConflictingNotesWereCreated();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingSavedSearchesFromUserOwnDataOnlyWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingSavedSearchesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingTagsFromUserOwnDataOnlyWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingTagsFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataOnlyWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotebooksFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotesFromUserOwnDataOnlyWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)

    // These are expected because locally modified notes should have been sent
    // to Evernote
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
    checkNoConflictingNotesWereCreated();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingTagsFromLinkedNotebooksOnlyWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingTagsFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotebooksFromLinkedNotebooksOnlyWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotebooksFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotesFromLinkedNotebooksOnlyWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotesFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // These are expected because local conflicting note should have been
    // created and sent back to Evernote
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
    checkLocalCopiesOfConflictingNotesWereCreated();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingTagsFromLinkedNotebooksOnlyWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingTagsFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotebooksFromLinkedNotebooksOnlyWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotebooksFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotesFromLinkedNotebooksOnlyWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotesFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // These are expected because locally modified notes should have been sent
    // to Evernote
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
    checkNoConflictingNotesWereCreated();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingTagsFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingTagsFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    setConflictingTagsFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotebooksFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    setConflictingNotebooksFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotesFromUserOwnDataAndLinkedNotebooksWithLargerRemoteUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    setConflictingNotesFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::LargerRemoteUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // These are expected because local conflicting note should have been
    // created and sent back to Evernote
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
    checkLocalCopiesOfConflictingNotesWereCreated();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingTagsFromUserOwnDataAndLinkedNotebooksWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingTagsFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    setConflictingTagsFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotebooksFromUserOwnDataAndLinkedNotebooksWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotebooksFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    setConflictingNotebooksFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

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

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
}

void SynchronizationTester::
    testIncrementalSyncWithConflictingNotesFromUserOwnDataAndLinkedNotebooksWithSameUsn()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setConflictingNotesFromUserOwnDataToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    setConflictingNotesFromLinkedNotebooksToLocalAndRemoteStorages(
        ConflictingItemsUsnOption::SameUsn);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    // These are expected because locally modified notes should have been sent
    // to Evernote
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
    checkExpectedNamesOfConflictingItemsAfterSync();
    checkNoConflictingNotesWereCreated();
}

void SynchronizationTester::
    testIncrementalSyncWithExpungedRemoteLinkedNotebookNotesProducingNotelessTags()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setExpungedLinkedNotebookNotesToRemoteStorageToProduceNotelessLinkedNotebookTags();

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)

    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedRateLimitExceeded)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    expungeNotelessLinkedNotebookTagsFromRemoteStorage();
    checkIdentityOfLocalAndRemoteItems();

    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetUserOwnSyncStateAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnGetUserOwnSyncStateAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetLinkedNotebookSyncStateAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetLinkedNotebookSyncStateAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetUserOwnSyncChunkAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnGetUserOwnSyncChunkAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetLinkedNotebookSyncChunkAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();

    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetLinkedNotebookSyncChunkAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetNewNoteAfterDownloadingUserOwnSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetNoteAttemptAfterDownloadingUserOwnSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // API rate limits breach + synced user own content + synced linked
    // notebooks content + after local changes sending (although nothing is
    // actually sent here)
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 0;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedNoteAfterDownloadingUserOwnSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetNoteAttemptAfterDownloadingUserOwnSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    // These are expected because remotely modified resource lead to the locally
    // induced updates of note containing them
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    // FIXME: this one shouldn't actually be expected but it's too much trouble
    // to change this behaviour, so will keep it for now
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // API rate limits breach + synced user own content + synced linked
    // notebooks content + after local changes sending (although nothing is
    // actually sent here)
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 0;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetNewResourceAfterDownloadingUserOwnSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnResourcesInExistingNotesToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetResourceAttemptAfterDownloadingUserOwnSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    // These are expected because remotely modified resource lead to the locally
    // induced updates of note containing them
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    // FIXME: this one shouldn't actually be expected but it's too much trouble
    // to change this behaviour, so will keep it for now
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // API rate limits breach + synced user own content + synced linked
    // notebooks content + after local changes sending (although nothing is
    // actually sent here)
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 0;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedResourceAfterDownloadingUserOwnSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnResourcesOnlyToRemoteStorage();
    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetResourceAttemptAfterDownloadingUserOwnSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    // These are expected because remotely modified resource lead to the locally
    // induced updates of note containing them
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    // FIXME: this one shouldn't actually be expected but it's too much trouble
    // to change this behaviour, so will keep it for now
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // API rate limits breach + synced user own content + synced linked
    // notebooks content + after local changes sending
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 0;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetNewNoteAfterDownloadingLinkedNotebookSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnItemsToRemoteStorage();
    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetNoteAttemptAfterDownloadingLinkedNotebookSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + API rate limits breach + synced linked
    // notebooks content + after local changes sending (although nothing is
    // actually sent here)
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 1;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedNoteAfterDownloadingLinkedNotebookSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnItemsToRemoteStorage();
    setModifiedLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetNoteAttemptAfterDownloadingLinkedNotebookSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    // These are expected because remotely modified resource lead to the locally
    // induced updates of note containing them
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(finishedSomethingSent)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + API rate limits breach + synced linked
    // notebooks content + after local changes sending
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 1;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetNewResourceAfterDownloadingLinkedNotebookSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnItemsToRemoteStorage();
    setNewResourcesInExistingNotesFromLinkedNotebooksToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetResourceAttemptAfterDownloadingLinkedNotebookSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    // These are expected because remotely modified resource lead to the locally
    // induced updates of note containing them
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + API rate limits breach + synced linked
    // notebooks content + after local changes sending
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 1;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnGetModifiedResourceAfterDownloadingLinkedNotebookSyncChunksAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnItemsToRemoteStorage();
    setModifiedLinkedNotebookResourcesOnlyToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnGetResourceAttemptAfterDownloadingLinkedNotebookSyncChunks);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    // These are expected because remotely modified resource lead to the locally
    // induced updates of note containing them
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + API rate limits breach + synced linked
    // notebooks content + after local changes sending
    int numExpectedSyncStateEntries = 4;

    int rateLimitTriggeredSyncStateEntryIndex = 1;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries,
        rateLimitTriggeredSyncStateEntryIndex);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnCreateSavedSearchAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnCreateSavedSearchAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnUpdateSavedSearchAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnUpdateSavedSearchAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnCreateUserOwnTagAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnCreateTagAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnUpdateUserOwnTagAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnUpdateTagAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnCreateTagInLinkedNotebookAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewLinkedNotebookItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnCreateTagAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnUpdateTagInLinkedNotebookAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedLinkedNotebookItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnUpdateTagAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnCreateNotebookAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnCreateNotebookAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnUpdateNotebookAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnUpdateNotebookAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnCreateUserOwnNoteAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnCreateNoteAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnUpdateUserOwnNoteAttempt()
{
    setUserOwnItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedUserOwnItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnUpdateNoteAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnCreateNoteInLinkedNotebookAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewLinkedNotebookItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnCreateNoteAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnUpdateNoteInLinkedNotebookAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setModifiedLinkedNotebookItemsToLocalStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::OnUpdateNoteAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(finishedSomethingSent)
    CHECK_EXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(finishedSomethingDownloaded)
    CHECK_UNEXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressEmpty(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::
    testIncrementalSyncWithRateLimitsBreachOnAuthenticateToLinkedNotebookAttempt()
{
    setUserOwnItemsToRemoteStorage();
    setLinkedNotebookItemsToRemoteStorage();
    copyRemoteItemsToLocalStorage();
    setRemoteStorageSyncStateToPersistentSyncSettings();
    m_pFakeNoteStore->considerAllExistingDataItemsSentBeforeRateLimitBreach();

    setNewLinkedNotebookItemsToRemoteStorage();

    m_pFakeNoteStore->setAPIRateLimitsExceedingTrigger(
        FakeNoteStore::APIRateLimitsTrigger::
            OnAuthenticateToSharedNotebookAttempt);

    SynchronizationManagerSignalsCatcher catcher(
        *m_pLocalStorageManagerAsync, *m_pSynchronizationManager,
        *m_pSyncStateStorage);

    runTest(catcher);

    CHECK_EXPECTED(receivedStartedSignal)
    CHECK_EXPECTED(receivedFinishedSignal)
    CHECK_EXPECTED(finishedSomethingDownloaded)
    CHECK_EXPECTED(receivedRemoteToLocalSyncDone)
    CHECK_EXPECTED(remoteToLocalSyncDoneSomethingDownloaded)
    CHECK_EXPECTED(receivedSyncChunksDownloaded)
    CHECK_EXPECTED(receivedLinkedNotebookSyncChunksDownloaded)
    CHECK_EXPECTED(receivedRateLimitExceeded)

    CHECK_UNEXPECTED(receivedAuthenticationFinishedSignal)
    CHECK_UNEXPECTED(receivedStoppedSignal)
    CHECK_UNEXPECTED(finishedSomethingSent)
    CHECK_UNEXPECTED(receivedAuthenticationRevokedSignal)
    CHECK_UNEXPECTED(receivedRemoteToLocalSyncStopped)
    CHECK_UNEXPECTED(receivedSendLocalChangedStopped)
    CHECK_UNEXPECTED(receivedWillRepeatRemoteToLocalSyncAfterSendingChanges)
    CHECK_UNEXPECTED(receivedDetectedConflictDuringLocalChangesSending)
    CHECK_UNEXPECTED(receivedPreparedDirtyObjectsForSending)
    CHECK_UNEXPECTED(receivedPreparedLinkedNotebookDirtyObjectsForSending)

    checkProgressNotificationsOrder(catcher);
    checkSyncChunksDataProcessingProgressOrder(catcher);
    checkLinkedNotebookSyncChunksDataProcessingProgressOrder(catcher);

    checkIdentityOfLocalAndRemoteItems();
    checkPersistentSyncState();

    // synced user own content + synced linked notebooks content + after local
    // changes sending, no sync state persistence event shoudl fire on API rate
    // limit breach since all the USNs would be the same as those persisted
    // before the event
    int numExpectedSyncStateEntries = 3;

    checkSyncStatePersistedRightAfterAPIRateLimitBreach(
        catcher, numExpectedSyncStateEntries, -1);
}

void SynchronizationTester::setUserOwnItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    m_guidsOfUsersOwnRemoteItemsToModify = GuidsOfItemsUsedForSyncTest();
    m_guidsOfUserOwnLocalItemsToModify = GuidsOfItemsUsedForSyncTest();
    m_guidsOfUserOwnRemoteItemsToExpunge = GuidsOfItemsUsedForSyncTest();

    qevercloud::SavedSearch firstSearch;
    firstSearch.setGuid(UidGenerator::Generate());
    firstSearch.setName(QStringLiteral("First saved search"));
    firstSearch.setQuery(QStringLiteral("First saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(firstSearch, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::SavedSearch secondSearch;
    secondSearch.setGuid(UidGenerator::Generate());
    secondSearch.setName(QStringLiteral("Second saved search"));
    secondSearch.setQuery(QStringLiteral("Second saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(secondSearch, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::SavedSearch thirdSearch;
    thirdSearch.setGuid(UidGenerator::Generate());
    thirdSearch.setName(QStringLiteral("Third saved search"));
    thirdSearch.setQuery(QStringLiteral("Third saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(thirdSearch, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::SavedSearch fourthSearch;
    fourthSearch.setGuid(UidGenerator::Generate());
    fourthSearch.setName(QStringLiteral("Fourth saved search"));
    fourthSearch.setQuery(QStringLiteral("Fourth saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(fourthSearch, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::SavedSearch fifthSearch;
    fifthSearch.setGuid(UidGenerator::Generate());
    fifthSearch.setName(QStringLiteral("Fifth saved search"));
    fifthSearch.setQuery(QStringLiteral("Fifth saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(fifthSearch, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_guidsOfUsersOwnRemoteItemsToModify.m_savedSearchGuids
        << firstSearch.guid().value();

    m_guidsOfUsersOwnRemoteItemsToModify.m_savedSearchGuids
        << secondSearch.guid().value();

    m_guidsOfUserOwnLocalItemsToModify.m_savedSearchGuids
        << thirdSearch.guid().value();

    m_guidsOfUserOwnLocalItemsToModify.m_savedSearchGuids
        << fourthSearch.guid().value();

    m_guidsOfUserOwnRemoteItemsToExpunge.m_savedSearchGuids
        << fifthSearch.guid().value();

    qevercloud::Tag firstTag;
    firstTag.setGuid(UidGenerator::Generate());
    firstTag.setName(QStringLiteral("First tag"));
    res = m_pFakeNoteStore->setTag(firstTag, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag secondTag;
    secondTag.setGuid(UidGenerator::Generate());
    secondTag.setName(QStringLiteral("Second tag"));
    res = m_pFakeNoteStore->setTag(secondTag, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag thirdTag;
    thirdTag.setGuid(UidGenerator::Generate());
    thirdTag.setParentGuid(secondTag.guid());
    thirdTag.setParentTagLocalId(secondTag.localId());
    thirdTag.setName(QStringLiteral("Third tag"));
    res = m_pFakeNoteStore->setTag(thirdTag, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag fourthTag;
    fourthTag.setGuid(UidGenerator::Generate());
    fourthTag.setParentGuid(thirdTag.guid());
    fourthTag.setParentTagLocalId(thirdTag.localId());
    fourthTag.setName(QStringLiteral("Fourth tag"));
    res = m_pFakeNoteStore->setTag(fourthTag, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag fifthTag;
    fifthTag.setGuid(UidGenerator::Generate());
    fifthTag.setName(QStringLiteral("Fifth tag"));
    res = m_pFakeNoteStore->setTag(fifthTag, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_guidsOfUsersOwnRemoteItemsToModify.m_tagGuids << firstTag.guid().value();
    m_guidsOfUsersOwnRemoteItemsToModify.m_tagGuids << secondTag.guid().value();
    m_guidsOfUserOwnLocalItemsToModify.m_tagGuids << thirdTag.guid().value();
    m_guidsOfUserOwnLocalItemsToModify.m_tagGuids << fourthTag.guid().value();
    m_guidsOfUserOwnRemoteItemsToExpunge.m_tagGuids << fifthTag.guid().value();

    qevercloud::Notebook firstNotebook;
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setName(QStringLiteral("First notebook"));
    firstNotebook.setDefaultNotebook(true);
    res = m_pFakeNoteStore->setNotebook(firstNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second notebook"));
    secondNotebook.setDefaultNotebook(false);
    res = m_pFakeNoteStore->setNotebook(secondNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook thirdNotebook;
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setName(QStringLiteral("Third notebook"));
    thirdNotebook.setDefaultNotebook(false);
    res = m_pFakeNoteStore->setNotebook(thirdNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook fourthNotebook;
    fourthNotebook.setGuid(UidGenerator::Generate());
    fourthNotebook.setName(QStringLiteral("Fourth notebook"));
    fourthNotebook.setDefaultNotebook(false);
    res = m_pFakeNoteStore->setNotebook(fourthNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook fifthNotebook;
    fifthNotebook.setGuid(UidGenerator::Generate());
    fifthNotebook.setName(QStringLiteral("Fifth notebook"));
    fifthNotebook.setDefaultNotebook(false);
    res = m_pFakeNoteStore->setNotebook(fifthNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_guidsOfUsersOwnRemoteItemsToModify.m_notebookGuids
        << firstNotebook.guid().value();

    m_guidsOfUsersOwnRemoteItemsToModify.m_notebookGuids
        << secondNotebook.guid().value();

    m_guidsOfUserOwnLocalItemsToModify.m_notebookGuids
        << thirdNotebook.guid().value();

    m_guidsOfUserOwnLocalItemsToModify.m_notebookGuids
        << fourthNotebook.guid().value();

    m_guidsOfUserOwnRemoteItemsToExpunge.m_notebookGuids
        << fifthNotebook.guid().value();

    qevercloud::Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setNotebookLocalId(firstNotebook.localId());
    firstNote.setTitle(QStringLiteral("First note"));
    firstNote.setContent(
        QStringLiteral("<en-note><div>First note</div></en-note>"));
    firstNote.setContentLength(firstNote.content()->size());

    firstNote.setContentHash(QCryptographicHash::hash(
        firstNote.content()->toUtf8(), QCryptographicHash::Md5));

    firstNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    firstNote.setUpdated(*firstNote.created());
    res = m_pFakeNoteStore->setNote(firstNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(firstNotebook.guid());
    secondNote.setNotebookLocalId(firstNotebook.localId());
    secondNote.setTitle(QStringLiteral("Second note"));

    secondNote.setContent(
        QStringLiteral("<en-note><div>Second note</div></en-note>"));

    secondNote.setContentLength(secondNote.content()->size());

    secondNote.setContentHash(QCryptographicHash::hash(
        secondNote.content()->toUtf8(), QCryptographicHash::Md5));

    secondNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    secondNote.setUpdated(*secondNote.created());

    secondNote.setTagGuids(
        QList<qevercloud::Guid>{} << firstTag.guid().value()
        << secondTag.guid().value());

    secondNote.setTagLocalIds(
        QStringList{} << firstTag.localId() << secondTag.localId());

    res = m_pFakeNoteStore->setNote(secondNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setNotebookGuid(firstNotebook.guid());
    thirdNote.setNotebookLocalId(firstNotebook.localId());
    thirdNote.setTitle(QStringLiteral("Third note"));

    thirdNote.setContent(
        QStringLiteral("<en-note><div>Third note</div></en-note>"));

    thirdNote.setContentLength(thirdNote.content()->size());

    thirdNote.setContentHash(QCryptographicHash::hash(
        thirdNote.content()->toUtf8(), QCryptographicHash::Md5));

    thirdNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    thirdNote.setUpdated(*thirdNote.created());
    thirdNote.setTagGuids(QList<qevercloud::Guid>{} << thirdTag.guid().value());
    thirdNote.setTagLocalIds(QStringList{} << thirdTag.localId());

    qevercloud::Resource thirdNoteFirstResource;
    thirdNoteFirstResource.setGuid(UidGenerator::Generate());
    thirdNoteFirstResource.setNoteGuid(thirdNote.guid());
    thirdNoteFirstResource.setNoteLocalId(thirdNote.localId());
    thirdNoteFirstResource.setMime(QStringLiteral("text/plain"));

    thirdNoteFirstResource.setData(qevercloud::Data{});
    thirdNoteFirstResource.mutableData()->setBody(
        QByteArray("Third note first resource data body"));

    thirdNoteFirstResource.mutableData()->setSize(
        thirdNoteFirstResource.data()->body()->size());

    thirdNoteFirstResource.mutableData()->setBodyHash(QCryptographicHash::hash(
        *thirdNoteFirstResource.data()->body(), QCryptographicHash::Md5));

    thirdNote.setResources(
        QList<qevercloud::Resource>{} << thirdNoteFirstResource);

    m_guidsOfUsersOwnRemoteItemsToModify.m_resourceGuids
        << thirdNoteFirstResource.guid().value();

    res = m_pFakeNoteStore->setNote(thirdNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note fourthNote;
    fourthNote.setGuid(UidGenerator::Generate());
    fourthNote.setNotebookGuid(secondNotebook.guid());
    fourthNote.setNotebookLocalId(secondNotebook.localId());
    fourthNote.setTitle(QStringLiteral("Fourth note"));

    fourthNote.setContent(
        QStringLiteral("<en-note><div>Fourth note</div></en-note>"));

    fourthNote.setContentLength(fourthNote.content()->size());

    fourthNote.setContentHash(QCryptographicHash::hash(
        fourthNote.content()->toUtf8(), QCryptographicHash::Md5));

    fourthNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    fourthNote.setUpdated(*fourthNote.created());
    res = m_pFakeNoteStore->setNote(fourthNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note fifthNote;
    fifthNote.setGuid(UidGenerator::Generate());
    fifthNote.setNotebookGuid(thirdNotebook.guid());
    fifthNote.setNotebookLocalId(thirdNotebook.localId());
    fifthNote.setTitle(QStringLiteral("Fifth note"));

    fifthNote.setContent(
        QStringLiteral("<en-note><div>Fifth note</div></en-note>"));

    fifthNote.setContentLength(fifthNote.content()->size());

    fifthNote.setContentHash(QCryptographicHash::hash(
        fifthNote.content()->toUtf8(), QCryptographicHash::Md5));

    fifthNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    fifthNote.setUpdated(*fifthNote.created());
    res = m_pFakeNoteStore->setNote(fifthNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note sixthNote;
    sixthNote.setGuid(UidGenerator::Generate());
    sixthNote.setNotebookGuid(fourthNotebook.guid());
    sixthNote.setNotebookLocalId(fourthNotebook.localId());
    sixthNote.setTitle(QStringLiteral("Sixth note"));

    sixthNote.setContent(
        QStringLiteral("<en-note><div>Sixth note</div></en-note>"));

    sixthNote.setContentLength(sixthNote.content()->size());

    sixthNote.setContentHash(QCryptographicHash::hash(
        sixthNote.content()->toUtf8(), QCryptographicHash::Md5));

    sixthNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    sixthNote.setUpdated(*sixthNote.created());
    res = m_pFakeNoteStore->setNote(sixthNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note seventhNote;
    seventhNote.setGuid(UidGenerator::Generate());
    seventhNote.setNotebookGuid(fourthNotebook.guid());
    seventhNote.setNotebookLocalId(fourthNotebook.localId());
    seventhNote.setTitle(QStringLiteral("Seventh note"));

    seventhNote.setContent(
        QStringLiteral("<en-note><div>Seventh note</div></en-note>"));

    seventhNote.setContentLength(sixthNote.content()->size());

    seventhNote.setContentHash(QCryptographicHash::hash(
        sixthNote.content()->toUtf8(), QCryptographicHash::Md5));

    seventhNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    seventhNote.setUpdated(*sixthNote.created());

    qevercloud::Resource seventhNoteFirstResource;
    seventhNoteFirstResource.setGuid(UidGenerator::Generate());
    seventhNoteFirstResource.setNoteGuid(seventhNote.guid());
    seventhNoteFirstResource.setNoteLocalId(seventhNote.localId());
    seventhNoteFirstResource.setMime(QStringLiteral("text/plain"));

    seventhNoteFirstResource.setData(qevercloud::Data{});
    seventhNoteFirstResource.mutableData()->setBody(
        QByteArray("Seventh note first resource data body"));

    seventhNoteFirstResource.mutableData()->setSize(
        seventhNoteFirstResource.data()->body()->size());

    seventhNoteFirstResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *seventhNoteFirstResource.data()->body(), QCryptographicHash::Md5));

    seventhNote.setResources(
        QList<qevercloud::Resource>{} << seventhNoteFirstResource);

    res = m_pFakeNoteStore->setNote(seventhNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_guidsOfUsersOwnRemoteItemsToModify.m_noteGuids
        << firstNote.guid().value();

    m_guidsOfUsersOwnRemoteItemsToModify.m_noteGuids
        << secondNote.guid().value();

    m_guidsOfUserOwnLocalItemsToModify.m_noteGuids << fifthNote.guid().value();

    m_guidsOfUserOwnLocalItemsToModify.m_noteGuids
        << seventhNote.guid().value();

    m_guidsOfUserOwnRemoteItemsToExpunge.m_noteGuids
        << sixthNote.guid().value();

    // NOTE: shouldn't expunge the last added note to prevent problems due to
    // fake note store's highest USN decreasing
}

void SynchronizationTester::setLinkedNotebookItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    m_guidsOfLinkedNotebookRemoteItemsToModify = GuidsOfItemsUsedForSyncTest();
    m_guidsOfLinkedNotebookLocalItemsToModify = GuidsOfItemsUsedForSyncTest();
    m_guidsOfLinkedNotebookRemoteItemsToExpunge = GuidsOfItemsUsedForSyncTest();

    qevercloud::LinkedNotebook firstLinkedNotebook;
    firstLinkedNotebook.setGuid(UidGenerator::Generate());

    firstLinkedNotebook.setUsername(
        QStringLiteral("First linked notebook owner"));

    firstLinkedNotebook.setShareName(
        QStringLiteral("First linked notebook share name"));

    firstLinkedNotebook.setShardId(UidGenerator::Generate());
    firstLinkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());

    firstLinkedNotebook.setNoteStoreUrl(
        QStringLiteral("First linked notebook fake note store URL"));

    firstLinkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("First linked notebook fake web API URL prefix"));

    res = m_pFakeNoteStore->setLinkedNotebook(
        firstLinkedNotebook, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_pFakeNoteStore->setLinkedNotebookAuthToken(
        *firstLinkedNotebook.username(), UidGenerator::Generate());

    qevercloud::LinkedNotebook secondLinkedNotebook;
    secondLinkedNotebook.setGuid(UidGenerator::Generate());

    secondLinkedNotebook.setUsername(
        QStringLiteral("Second linked notebook owner"));

    secondLinkedNotebook.setShareName(
        QStringLiteral("Second linked notebook share name"));

    secondLinkedNotebook.setShardId(UidGenerator::Generate());
    secondLinkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());

    secondLinkedNotebook.setNoteStoreUrl(
        QStringLiteral("Second linked notebook fake note store URL"));

    secondLinkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Second linked notebook fake web API URL prefix"));

    res = m_pFakeNoteStore->setLinkedNotebook(
        secondLinkedNotebook, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_pFakeNoteStore->setLinkedNotebookAuthToken(
        *secondLinkedNotebook.username(), UidGenerator::Generate());

    qevercloud::LinkedNotebook thirdLinkedNotebook;
    thirdLinkedNotebook.setGuid(UidGenerator::Generate());

    thirdLinkedNotebook.setUsername(
        QStringLiteral("Third linked notebook owner"));

    thirdLinkedNotebook.setShareName(
        QStringLiteral("Third linked notebook share name"));

    thirdLinkedNotebook.setShardId(UidGenerator::Generate());
    thirdLinkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());

    thirdLinkedNotebook.setNoteStoreUrl(
        QStringLiteral("Third linked notebook fake note store URL"));

    thirdLinkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Third linked notebook fake web API URL prefix"));

    res = m_pFakeNoteStore->setLinkedNotebook(
        thirdLinkedNotebook, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_pFakeNoteStore->setLinkedNotebookAuthToken(
        *thirdLinkedNotebook.username(), UidGenerator::Generate());

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_linkedNotebookGuids
        << firstLinkedNotebook.guid().value();

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_linkedNotebookGuids
        << secondLinkedNotebook.guid().value();

    qevercloud::Tag firstLinkedNotebookFirstTag;
    firstLinkedNotebookFirstTag.setGuid(UidGenerator::Generate());

    firstLinkedNotebookFirstTag.setName(
        QStringLiteral("First linked notebook first tag"));

    firstLinkedNotebookFirstTag.setLinkedNotebookGuid(
        firstLinkedNotebook.guid());

    res =
        m_pFakeNoteStore->setTag(firstLinkedNotebookFirstTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag firstLinkedNotebookSecondTag;
    firstLinkedNotebookSecondTag.setGuid(UidGenerator::Generate());

    firstLinkedNotebookSecondTag.setName(
        QStringLiteral("First linked notebook second tag"));

    firstLinkedNotebookSecondTag.setLinkedNotebookGuid(
        firstLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        firstLinkedNotebookSecondTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag firstLinkedNotebookThirdTag;
    firstLinkedNotebookThirdTag.setGuid(UidGenerator::Generate());

    firstLinkedNotebookThirdTag.setName(
        QStringLiteral("First linked notebook third tag"));

    firstLinkedNotebookThirdTag.setLinkedNotebookGuid(
        firstLinkedNotebook.guid());

    firstLinkedNotebookThirdTag.setParentGuid(
        firstLinkedNotebookSecondTag.guid());

    firstLinkedNotebookThirdTag.setParentTagLocalId(
        firstLinkedNotebookSecondTag.localId());

    res =
        m_pFakeNoteStore->setTag(firstLinkedNotebookThirdTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag secondLinkedNotebookFirstTag;
    secondLinkedNotebookFirstTag.setGuid(UidGenerator::Generate());

    secondLinkedNotebookFirstTag.setName(
        QStringLiteral("Second linked notebook first tag"));

    secondLinkedNotebookFirstTag.setLinkedNotebookGuid(
        secondLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        secondLinkedNotebookFirstTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag secondLinkedNotebookSecondTag;
    secondLinkedNotebookSecondTag.setGuid(UidGenerator::Generate());

    secondLinkedNotebookSecondTag.setName(
        QStringLiteral("Second linked notebook second tag"));

    secondLinkedNotebookSecondTag.setLinkedNotebookGuid(
        secondLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        secondLinkedNotebookSecondTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag secondLinkedNotebookThirdTag;
    secondLinkedNotebookThirdTag.setGuid(UidGenerator::Generate());

    secondLinkedNotebookThirdTag.setName(
        QStringLiteral("Second linked notebook third tag"));

    secondLinkedNotebookThirdTag.setLinkedNotebookGuid(
        secondLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        secondLinkedNotebookThirdTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag thirdLinkedNotebookFirstTag;
    thirdLinkedNotebookFirstTag.setGuid(UidGenerator::Generate());

    thirdLinkedNotebookFirstTag.setName(
        QStringLiteral("Third linked notebook first tag"));

    thirdLinkedNotebookFirstTag.setLinkedNotebookGuid(
        thirdLinkedNotebook.guid());

    res =
        m_pFakeNoteStore->setTag(thirdLinkedNotebookFirstTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag thirdLinkedNotebookSecondTag;
    thirdLinkedNotebookSecondTag.setGuid(UidGenerator::Generate());

    thirdLinkedNotebookSecondTag.setName(
        QStringLiteral("Third linked notebook second tag"));

    thirdLinkedNotebookSecondTag.setLinkedNotebookGuid(
        thirdLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        thirdLinkedNotebookSecondTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_tagGuids
        << firstLinkedNotebookFirstTag.guid().value();

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_tagGuids
        << firstLinkedNotebookSecondTag.guid().value();

    m_guidsOfLinkedNotebookLocalItemsToModify.m_tagGuids
        << secondLinkedNotebookThirdTag.guid().value();

    m_guidsOfLinkedNotebookLocalItemsToModify.m_tagGuids
        << thirdLinkedNotebookFirstTag.guid().value();

    m_guidsOfLinkedNotebookRemoteItemsToExpunge.m_tagGuids
        << thirdLinkedNotebookSecondTag.guid().value();

    qevercloud::Notebook firstNotebook;
    firstNotebook.setGuid(UidGenerator::Generate());
    firstNotebook.setName(QStringLiteral("First linked notebook"));
    firstNotebook.setDefaultNotebook(false);
    firstNotebook.setLinkedNotebookGuid(firstLinkedNotebook.guid());
    res = m_pFakeNoteStore->setNotebook(firstNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook secondNotebook;
    secondNotebook.setGuid(UidGenerator::Generate());
    secondNotebook.setName(QStringLiteral("Second linked notebook"));
    secondNotebook.setDefaultNotebook(false);
    secondNotebook.setLinkedNotebookGuid(secondLinkedNotebook.guid());
    res = m_pFakeNoteStore->setNotebook(secondNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook thirdNotebook;
    thirdNotebook.setGuid(UidGenerator::Generate());
    thirdNotebook.setName(QStringLiteral("Third linked notebook"));
    thirdNotebook.setDefaultNotebook(false);
    thirdNotebook.setLinkedNotebookGuid(thirdLinkedNotebook.guid());
    res = m_pFakeNoteStore->setNotebook(thirdNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_notebookGuids
        << firstNotebook.guid().value();

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_notebookGuids
        << secondNotebook.guid().value();

    m_guidsOfLinkedNotebookLocalItemsToModify.m_notebookGuids
        << thirdNotebook.guid().value();

    qevercloud::Note firstNote;
    firstNote.setGuid(UidGenerator::Generate());
    firstNote.setNotebookGuid(firstNotebook.guid());
    firstNote.setNotebookLocalId(firstNotebook.localId());
    firstNote.setTitle(QStringLiteral("First linked notebook first note"));

    firstNote.setContent(
        QStringLiteral("<en-note><div>First linked notebook first note"
                       "</div></en-note>"));

    firstNote.setContentLength(firstNote.content()->size());

    firstNote.setContentHash(QCryptographicHash::hash(
        firstNote.content()->toUtf8(), QCryptographicHash::Md5));

    firstNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    firstNote.setUpdated(*firstNote.created());
    res = m_pFakeNoteStore->setNote(firstNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note secondNote;
    secondNote.setGuid(UidGenerator::Generate());
    secondNote.setNotebookGuid(firstNotebook.guid());
    secondNote.setNotebookLocalId(firstNotebook.localId());
    secondNote.setTitle(QStringLiteral("First linked notebook second note"));

    secondNote.setContent(
        QStringLiteral("<en-note><div>First linked notebook second note"
                       "</div></en-note>"));

    secondNote.setContentLength(secondNote.content()->size());

    secondNote.setContentHash(QCryptographicHash::hash(
        secondNote.content()->toUtf8(), QCryptographicHash::Md5));

    secondNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    secondNote.setUpdated(*secondNote.created());

    secondNote.setTagGuids(
        QList<qevercloud::Guid>{} << firstLinkedNotebookFirstTag.guid().value()
        << firstLinkedNotebookSecondTag.guid().value()
        << firstLinkedNotebookThirdTag.guid().value());

    secondNote.setTagLocalIds(
        QStringList{} << firstLinkedNotebookFirstTag.localId()
        << firstLinkedNotebookSecondTag.localId()
        << firstLinkedNotebookThirdTag.localId());

    res = m_pFakeNoteStore->setNote(secondNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note thirdNote;
    thirdNote.setGuid(UidGenerator::Generate());
    thirdNote.setNotebookGuid(secondNotebook.guid());
    thirdNote.setNotebookLocalId(secondNotebook.localId());
    thirdNote.setTitle(QStringLiteral("Second linked notebook first note"));

    thirdNote.setContent(
        QStringLiteral("<en-note><div>Second linked notebook first note"
                       "</div></en-note>"));

    thirdNote.setContentLength(thirdNote.content()->size());

    thirdNote.setContentHash(QCryptographicHash::hash(
        thirdNote.content()->toUtf8(), QCryptographicHash::Md5));

    thirdNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    thirdNote.setUpdated(*thirdNote.created());
    thirdNote.setTagGuids(
        QList<qevercloud::Guid>() << secondLinkedNotebookFirstTag.guid().value()
        << secondLinkedNotebookSecondTag.guid().value());

    thirdNote.setTagLocalIds(
        QStringList{} << secondLinkedNotebookFirstTag.localId()
        << secondLinkedNotebookSecondTag.localId());

    qevercloud::Resource thirdNoteFirstResource;
    thirdNoteFirstResource.setGuid(UidGenerator::Generate());
    thirdNoteFirstResource.setNoteGuid(thirdNote.guid());
    thirdNoteFirstResource.setNoteLocalId(thirdNote.localId());
    thirdNoteFirstResource.setMime(QStringLiteral("text/plain"));

    thirdNoteFirstResource.setData(qevercloud::Data{});

    thirdNoteFirstResource.mutableData()->setBody(
        QByteArray("Second linked notebook first note resource data body"));

    thirdNoteFirstResource.mutableData()->setSize(
        thirdNoteFirstResource.data()->body()->size());

    thirdNoteFirstResource.mutableData()->setBodyHash(QCryptographicHash::hash(
        *thirdNoteFirstResource.data()->body(), QCryptographicHash::Md5));

    thirdNote.setResources(
        QList<qevercloud::Resource>{} << thirdNoteFirstResource);

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_resourceGuids
        << thirdNoteFirstResource.guid().value();

    res = m_pFakeNoteStore->setNote(thirdNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note fourthNote;
    fourthNote.setGuid(UidGenerator::Generate());
    fourthNote.setNotebookGuid(secondNotebook.guid());
    fourthNote.setNotebookLocalId(secondNotebook.localId());
    fourthNote.setTitle(QStringLiteral("Second linked notebook second note"));

    fourthNote.setContent(
        QStringLiteral("<en-note><div>Second linked notebook second note"
                       "</div></en-note>"));

    fourthNote.setContentLength(fourthNote.content()->size());

    fourthNote.setContentHash(QCryptographicHash::hash(
        fourthNote.content()->toUtf8(), QCryptographicHash::Md5));

    fourthNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    fourthNote.setUpdated(*fourthNote.created());
    fourthNote.setTagGuids(
        QList<qevercloud::Guid>{} << secondLinkedNotebookThirdTag.guid().value());
    fourthNote.setTagLocalIds(
        QStringList{} << secondLinkedNotebookThirdTag.localId());
    res = m_pFakeNoteStore->setNote(fourthNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note fifthNote;
    fifthNote.setGuid(UidGenerator::Generate());
    fifthNote.setNotebookGuid(thirdNotebook.guid());
    fifthNote.setNotebookLocalId(thirdNotebook.localId());
    fifthNote.setTitle(QStringLiteral("Third linked notebook first note"));

    fifthNote.setContent(
        QStringLiteral("<en-note><div>Third linked notebook first note"
                       "</div></en-note>"));

    fifthNote.setContentLength(fifthNote.content()->size());

    fifthNote.setContentHash(QCryptographicHash::hash(
        fifthNote.content()->toUtf8(), QCryptographicHash::Md5));

    fifthNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    fifthNote.setUpdated(*fifthNote.created());
    fifthNote.setTagGuids(
        QList<qevercloud::Guid>{} << thirdLinkedNotebookFirstTag.guid().value()
        << thirdLinkedNotebookSecondTag.guid().value());
    fifthNote.setTagLocalIds(
        QStringList{} << thirdLinkedNotebookFirstTag.localId()
        << thirdLinkedNotebookSecondTag.localId());
    res = m_pFakeNoteStore->setNote(fifthNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note sixthNote;
    sixthNote.setGuid(UidGenerator::Generate());
    sixthNote.setNotebookGuid(thirdNotebook.guid());
    sixthNote.setNotebookLocalId(thirdNotebook.localId());
    sixthNote.setTitle(QStringLiteral("Third linked notebook second note"));

    sixthNote.setContent(
        QStringLiteral("<en-note><div>Third linked notebook second note"
                       "</div></en-note>"));

    sixthNote.setContentLength(sixthNote.content()->size());

    sixthNote.setContentHash(QCryptographicHash::hash(
        sixthNote.content()->toUtf8(), QCryptographicHash::Md5));

    sixthNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    sixthNote.setUpdated(*sixthNote.created());
    res = m_pFakeNoteStore->setNote(sixthNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note seventhNote;
    seventhNote.setGuid(UidGenerator::Generate());
    seventhNote.setNotebookGuid(thirdNotebook.guid());
    seventhNote.setNotebookLocalId(thirdNotebook.localId());
    seventhNote.setTitle(QStringLiteral("Third linked notebook third note"));

    seventhNote.setContent(
        QStringLiteral("<en-note><div>Third linked notebook third note"
                       "</div></en-note>"));

    seventhNote.setContentLength(seventhNote.content()->size());

    seventhNote.setContentHash(QCryptographicHash::hash(
        seventhNote.content()->toUtf8(), QCryptographicHash::Md5));

    seventhNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    seventhNote.setUpdated(*seventhNote.created());

    qevercloud::Resource seventhNoteFirstResource;
    seventhNoteFirstResource.setGuid(UidGenerator::Generate());
    seventhNoteFirstResource.setNoteGuid(seventhNote.guid());
    seventhNoteFirstResource.setNoteLocalId(seventhNote.localId());
    seventhNoteFirstResource.setMime(QStringLiteral("text/plain"));

    seventhNoteFirstResource.setData(qevercloud::Data{});

    seventhNoteFirstResource.mutableData()->setBody(QByteArray(
        "Third linked notebook third note first resource data body"));

    seventhNoteFirstResource.mutableData()->setSize(
        seventhNoteFirstResource.data()->body()->size());

    seventhNoteFirstResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *seventhNoteFirstResource.data()->body(), QCryptographicHash::Md5));

    seventhNote.setResources(
        QList<qevercloud::Resource>{} << seventhNoteFirstResource);

    res = m_pFakeNoteStore->setNote(seventhNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_guidsOfLinkedNotebookRemoteItemsToModify.m_noteGuids
        << firstNote.guid().value();
    m_guidsOfLinkedNotebookRemoteItemsToModify.m_noteGuids
        << fourthNote.guid().value();
    m_guidsOfLinkedNotebookLocalItemsToModify.m_noteGuids
        << secondNote.guid().value();
    m_guidsOfLinkedNotebookLocalItemsToModify.m_noteGuids
        << seventhNote.guid().value();
    m_guidsOfLinkedNotebookRemoteItemsToExpunge.m_noteGuids
        << sixthNote.guid().value();
    // NOTE: shouldn't expunge the last added note to prevent problems due to
    // fake note store's highest USN decreasing

    Q_UNUSED(
        m_guidsOfLinkedNotebookNotesToExpungeToProduceNotelessLinkedNotebookTags
            .insert(fifthNote.guid().value()))

    Q_UNUSED(
        m_guidsOfLinkedNotebookNotesToExpungeToProduceNotelessLinkedNotebookTags
            .insert(thirdNote.guid().value()))

    Q_UNUSED(m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged.insert(
        thirdLinkedNotebookFirstTag.guid().value()))

    Q_UNUSED(m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged.insert(
        thirdLinkedNotebookSecondTag.guid().value()))

    Q_UNUSED(m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged.insert(
        secondLinkedNotebookFirstTag.guid().value()))

    Q_UNUSED(m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged.insert(
        secondLinkedNotebookSecondTag.guid().value()))
}

void SynchronizationTester::
    setNewUserOwnResourcesInExistingNotesToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QVERIFY(!m_guidsOfUsersOwnRemoteItemsToModify.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfUsersOwnRemoteItemsToModify.m_noteGuids))
    {
        const auto * pNote = m_pFakeNoteStore->findNote(noteGuid);

        QVERIFY2(
            pNote != nullptr,
            "Detected unexpectedly missing note in fake note store");

        qevercloud::Resource newResource;
        newResource.setGuid(UidGenerator::Generate());
        newResource.setData(qevercloud::Data{});
        newResource.mutableData()->setBody(QByteArray("New resource"));
        newResource.mutableData()->setSize(newResource.data()->body()->size());

        newResource.mutableData()->setBodyHash(QCryptographicHash::hash(
            *newResource.data()->body(), QCryptographicHash::Md5));

        newResource.setLocallyModified(false);
        newResource.setLocalOnly(false);

        qevercloud::Note modifiedNote(*pNote);
        if (!modifiedNote.resources()) {
            modifiedNote.setResources(
                QList<qevercloud::Resource>{} << newResource);
        }
        else {
            modifiedNote.mutableResources()->append(newResource);
        }

        modifiedNote.setLocallyModified(false);
        modifiedNote.setLocalOnly(false);
        // NOTE: intentionally acting like the note hasn't changed at all as
        // that seems to be the behaviour of actual Evernote servers

        res = m_pFakeNoteStore->setNote(modifiedNote, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::
    setNewResourcesInExistingNotesFromLinkedNotebooksToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QSet<QString> affectedLinkedNotebookGuids;

    QVERIFY(!m_guidsOfLinkedNotebookRemoteItemsToModify.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfLinkedNotebookRemoteItemsToModify.m_noteGuids))
    {
        const auto * pNote = m_pFakeNoteStore->findNote(noteGuid);

        QVERIFY2(
            pNote != nullptr,
            "Detected unexpectedly missing note in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->notebookGuid(),
            "Detected note without notebook guid in fake note store");

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(*pNote->notebookGuid());

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing notebook in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNotebook->linkedNotebookGuid(),
            "Internal error: the note to be added a new resource should "
            "have been from a linked notebook but it's not");

        Q_UNUSED(affectedLinkedNotebookGuids.insert(
            *pNotebook->linkedNotebookGuid()))

        qevercloud::Resource newResource;
        newResource.setGuid(UidGenerator::Generate());
        newResource.setData(qevercloud::Data{});
        newResource.mutableData()->setBody(QByteArray("New resource"));
        newResource.mutableData()->setSize(newResource.data()->body()->size());

        newResource.mutableData()->setBodyHash(QCryptographicHash::hash(
            *newResource.data()->body(), QCryptographicHash::Md5));

        newResource.setLocallyModified(false);
        newResource.setLocalOnly(false);

        qevercloud::Note modifiedNote(*pNote);
        if (!modifiedNote.resources()) {
            modifiedNote.setResources(
                QList<qevercloud::Resource>{} << newResource);
        }
        else {
            modifiedNote.mutableResources()->append(newResource);
        }
        modifiedNote.setLocallyModified(false);
        modifiedNote.setLocalOnly(false);
        // NOTE: intentionally acting like the note hasn't changed at all as
        // that seems to be the behaviour of actual Evernote servers

        res = m_pFakeNoteStore->setNote(modifiedNote, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    // Need to update the sync state for affected linked notebooks
    for (const auto & linkedNotebookGuid: qAsConst(affectedLinkedNotebookGuids))
    {
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(linkedNotebookGuid));

        const auto * pLinkedNotebook =
            m_pFakeNoteStore->findLinkedNotebook(linkedNotebookGuid);

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }
}

void SynchronizationTester::setModifiedUserOwnItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QVERIFY(!m_guidsOfUsersOwnRemoteItemsToModify.m_savedSearchGuids.isEmpty());

    for (const auto & savedSearchGuid:
         qAsConst(m_guidsOfUsersOwnRemoteItemsToModify.m_savedSearchGuids))
    {
        const auto * pSavedSearch =
            m_pFakeNoteStore->findSavedSearch(savedSearchGuid);

        QVERIFY2(
            pSavedSearch != nullptr,
            "Detected unexpectedly missing saved search in fake note store");

        qevercloud::SavedSearch modifiedSavedSearch(*pSavedSearch);

        modifiedSavedSearch.setName(
            modifiedSavedSearch.name().value() + gModifiedRemotelySuffix);

        modifiedSavedSearch.setLocallyModified(false);
        modifiedSavedSearch.setLocalOnly(false);
        modifiedSavedSearch.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setSavedSearch(
            modifiedSavedSearch, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    QVERIFY(!m_guidsOfUsersOwnRemoteItemsToModify.m_tagGuids.isEmpty());

    for (const auto & tagGuid:
         qAsConst(m_guidsOfUsersOwnRemoteItemsToModify.m_tagGuids))
    {
        const auto * pTag = m_pFakeNoteStore->findTag(tagGuid);

        QVERIFY2(
            pTag != nullptr,
            "Detected unexpectedly missing tag in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            !pTag->linkedNotebookGuid(),
            "Detected broken test condition - the tag was supposed to be "
            "user's own one has linked notebook guid");

        qevercloud::Tag modifiedTag(*pTag);
        modifiedTag.setName(
            modifiedTag.name().value() + gModifiedRemotelySuffix);
        modifiedTag.setLocallyModified(false);
        modifiedTag.setLocalOnly(false);
        modifiedTag.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setTag(modifiedTag, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    QVERIFY(!m_guidsOfUsersOwnRemoteItemsToModify.m_notebookGuids.isEmpty());

    for (const auto & notebookGuid:
         qAsConst(m_guidsOfUsersOwnRemoteItemsToModify.m_notebookGuids))
    {
        const auto * pNotebook = m_pFakeNoteStore->findNotebook(notebookGuid);

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing notebook in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            !pNotebook->linkedNotebookGuid(),
            "Detected broken test condition - the notebook was supposed to "
            "be user's own has linked notebook guid");

        qevercloud::Notebook modifiedNotebook(*pNotebook);

        modifiedNotebook.setName(
            modifiedNotebook.name().value() + gModifiedRemotelySuffix);

        modifiedNotebook.setLocallyModified(false);
        modifiedNotebook.setLocalOnly(false);
        modifiedNotebook.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setNotebook(modifiedNotebook, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    QVERIFY(!m_guidsOfUsersOwnRemoteItemsToModify.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfUsersOwnRemoteItemsToModify.m_noteGuids))
    {
        const auto * pNote = m_pFakeNoteStore->findNote(noteGuid);

        QVERIFY2(
            pNote != nullptr,
            "Detected unexpectedly missing note in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->notebookGuid(),
            "Detected note without notebook guid in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            !pNote->resources() || pNote->resources()->isEmpty(),
            "Detected broken test condition - the note to be modified is "
            "not supposed to contain resources");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->title(),
            "Detected note without title in fake note store");

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(*pNote->notebookGuid());

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing notebook in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            !pNotebook->linkedNotebookGuid(),
            "Detected broken test condition - the note was supposed to be "
            "user's own belongs to a notebook which has linked notebook guid");

        qevercloud::Note modifiedNote(*pNote);
        modifiedNote.setTitle(
            modifiedNote.title().value() + gModifiedRemotelySuffix);
        modifiedNote.setLocallyModified(false);
        modifiedNote.setLocalOnly(false);
        modifiedNote.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setNote(modifiedNote, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    setModifiedUserOwnResourcesOnlyToRemoteStorage();
}

void SynchronizationTester::setModifiedUserOwnResourcesOnlyToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QVERIFY(!m_guidsOfUsersOwnRemoteItemsToModify.m_resourceGuids.isEmpty());

    for (const auto & resourceGuid:
         qAsConst(m_guidsOfUsersOwnRemoteItemsToModify.m_resourceGuids))
    {
        const auto * pResource = m_pFakeNoteStore->findResource(resourceGuid);

        QVERIFY2(
            pResource != nullptr,
            "Detected unexpectedly missing resource in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pResource->noteGuid(),
            "Detected resource without note guid in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pResource->data() && pResource->data()->body(),
            "Detected resource without data body in fake note store");

        const auto * pNote = m_pFakeNoteStore->findNote(*pResource->noteGuid());

        QVERIFY2(
            pNote != nullptr,
            "Detected unexpectedly missing note in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->notebookGuid(),
            "Detected note without notebook guid in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->resources() && !pNote->resources()->isEmpty(),
            "Detected broken test condition - the resource's note doesn't "
            "have resources in fake note store");

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(*pNote->notebookGuid());

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing notebook in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            !pNotebook->linkedNotebookGuid(),
            "Detected broken test condition - the note was supposed to be "
            "user's own belongs to a notebook which has linked notebook guid");

        qevercloud::Resource modifiedResource(*pResource);

        modifiedResource.mutableData()->setBody(
            *modifiedResource.data()->body() + QByteArray("_modified_remotely"));

        modifiedResource.mutableData()->setSize(
            modifiedResource.data()->body()->size());

        modifiedResource.mutableData()->setBodyHash(QCryptographicHash::hash(
            *modifiedResource.data()->body(), QCryptographicHash::Md5));

        modifiedResource.setLocallyModified(false);
        modifiedResource.setLocalOnly(false);
        modifiedResource.setUpdateSequenceNum(std::nullopt);

        qevercloud::Note modifiedNote(*pNote);
        auto noteResources = modifiedNote.resources().value();
        for (auto & noteResource: noteResources) {
            if (noteResource.guid() == modifiedResource.guid()) {
                noteResource = modifiedResource;
                break;
            }
        }

        modifiedNote.setResources(noteResources);
        modifiedNote.setLocallyModified(true);
        modifiedNote.setLocalOnly(false);
        // NOTE: intentionally leaving the update sequence number to stay
        // as it is within note

        res = m_pFakeNoteStore->setNote(modifiedNote, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::setModifiedLinkedNotebookItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QVERIFY(!m_guidsOfLinkedNotebookRemoteItemsToModify.m_linkedNotebookGuids
                 .isEmpty());

    for (const auto & linkedNotebookGuid: qAsConst(
             m_guidsOfLinkedNotebookRemoteItemsToModify.m_linkedNotebookGuids))
    {
        const auto * pLinkedNotebook =
            m_pFakeNoteStore->findLinkedNotebook(linkedNotebookGuid);

        QVERIFY2(
            pLinkedNotebook != nullptr,
            "Detected unexpectedly missing linked notebook in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pLinkedNotebook->shareName(),
            "Detected linked notebook without share name in fake note store");

        qevercloud::LinkedNotebook modifiedLinkedNotebook(*pLinkedNotebook);

        modifiedLinkedNotebook.setShareName(
            *modifiedLinkedNotebook.shareName() + gModifiedRemotelySuffix);

        modifiedLinkedNotebook.setLocallyModified(false);
        modifiedLinkedNotebook.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setLinkedNotebook(
            modifiedLinkedNotebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    QVERIFY(!m_guidsOfLinkedNotebookRemoteItemsToModify.m_tagGuids.isEmpty());

    for (const auto & tagGuid:
         qAsConst(m_guidsOfLinkedNotebookRemoteItemsToModify.m_tagGuids))
    {
        const auto * pTag = m_pFakeNoteStore->findTag(tagGuid);

        QVERIFY2(
            pTag != nullptr,
            "Detected unexpectedly missing linked notebook's tag in fake note "
            "store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pTag->linkedNotebookGuid(),
            "Detected broken test condition - the tag was supposed to belong "
            "to a linked notebook but it doesn't");

        qevercloud::Tag modifiedTag(*pTag);
        modifiedTag.setName(
            modifiedTag.name().value() + gModifiedRemotelySuffix);
        modifiedTag.setLocallyModified(false);
        modifiedTag.setLocalOnly(false);
        modifiedTag.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setTag(modifiedTag, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        // Need to update the linked notebook's sync state
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(*pTag->linkedNotebookGuid()));

        const auto * pLinkedNotebook =
            m_pFakeNoteStore->findLinkedNotebook(*pTag->linkedNotebookGuid());

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }

    QVERIFY(
        !m_guidsOfLinkedNotebookRemoteItemsToModify.m_notebookGuids.isEmpty());

    for (const auto & notebookGuid:
         qAsConst(m_guidsOfLinkedNotebookRemoteItemsToModify.m_notebookGuids))
    {
        const auto * pNotebook = m_pFakeNoteStore->findNotebook(notebookGuid);

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing linked notebook's notebook "
            "in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNotebook->linkedNotebookGuid(),
            "Detected broken test condition - the notebook supposed to belong "
            "to a linked notebook but it doesn't");

        qevercloud::Notebook modifiedNotebook(*pNotebook);

        modifiedNotebook.setName(
            modifiedNotebook.name().value() + gModifiedRemotelySuffix);

        modifiedNotebook.setLocallyModified(false);
        modifiedNotebook.setLocalOnly(false);
        modifiedNotebook.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setNotebook(modifiedNotebook, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        // Need to update the linked notebook's sync state
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(*pNotebook->linkedNotebookGuid()));

        const auto * pLinkedNotebook = m_pFakeNoteStore->findLinkedNotebook(
            *pNotebook->linkedNotebookGuid());

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }

    QVERIFY(!m_guidsOfLinkedNotebookRemoteItemsToModify.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfLinkedNotebookRemoteItemsToModify.m_noteGuids))
    {
        const auto * pNote = m_pFakeNoteStore->findNote(noteGuid);

        QVERIFY2(
            pNote != nullptr,
            "Detected unexpectedly missing linked notebook's note in fake "
            "note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->notebookGuid(),
            "Detected note without notebook guid in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            !pNote->resources() || pNote->resources()->isEmpty(),
            "Detected broken test condition - the note to be modified was "
            "not supposed to contain resources");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->title(),
            "Detected note without title in fake note store");

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(*pNote->notebookGuid());

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing linked notebook's note's notebook "
            "in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNotebook->linkedNotebookGuid(),
            "Detected broken test condition - the note was supposed to belong "
            "to a linked notebook but it doesn't");

        qevercloud::Note modifiedNote(*pNote);
        modifiedNote.setTitle(
            modifiedNote.title().value() + gModifiedRemotelySuffix);
        modifiedNote.setLocallyModified(false);
        modifiedNote.setLocalOnly(false);
        modifiedNote.setUpdateSequenceNum(std::nullopt);

        res = m_pFakeNoteStore->setNote(modifiedNote, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        // Need to update the linked notebook's sync state
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(*pNotebook->linkedNotebookGuid()));

        const auto * pLinkedNotebook = m_pFakeNoteStore->findLinkedNotebook(
            *pNotebook->linkedNotebookGuid());

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }

    setModifiedLinkedNotebookResourcesOnlyToRemoteStorage();
}

void SynchronizationTester::
    setModifiedLinkedNotebookResourcesOnlyToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QVERIFY(
        !m_guidsOfLinkedNotebookRemoteItemsToModify.m_resourceGuids.isEmpty());

    for (const auto & resourceGuid:
         qAsConst(m_guidsOfLinkedNotebookRemoteItemsToModify.m_resourceGuids))
    {
        const auto * pResource = m_pFakeNoteStore->findResource(resourceGuid);

        QVERIFY2(
            pResource != nullptr,
            "Detected unexpectedly missing linked notebook's resource "
            "in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pResource->noteGuid(),
            "Detected resource without note guid in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pResource->data() && pResource->data()->body(),
            "Detected resource without data body in fake note store");

        const auto * pNote = m_pFakeNoteStore->findNote(*pResource->noteGuid());

        QVERIFY2(
            pNote != nullptr,
            "Detected unexpectedly missing linked notebook's note in fake "
            "note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNote->notebookGuid(),
            "Detected note without notebook guid in fake note store");

        QVERIFY2(
            pNote->resources() && !pNote->resources()->isEmpty(),
            "Detected broken test condition - the resource's note has no "
            "resources");

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(*pNote->notebookGuid());

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing linked notebook's note's notebook "
            "in fake note store");

        // NOLINTNEXTLINE
        QVERIFY2(
            pNotebook->linkedNotebookGuid(),
            "Detected broken test condition - the note was supposed to belong "
            "to a linked notebook but it doesn't");

        qevercloud::Resource modifiedResource(*pResource);

        modifiedResource.mutableData()->setBody(
            *modifiedResource.data()->body() + QByteArray("_modified_remotely"));

        modifiedResource.mutableData()->setSize(
            modifiedResource.data()->body()->size());

        modifiedResource.mutableData()->setBodyHash(QCryptographicHash::hash(
            *modifiedResource.data()->body(), QCryptographicHash::Md5));

        modifiedResource.setLocallyModified(false);
        modifiedResource.setLocalOnly(false);
        modifiedResource.setUpdateSequenceNum(std::nullopt);

        qevercloud::Note modifiedNote(*pNote);
        auto noteResources = modifiedNote.resources().value();
        for (auto & noteResource: noteResources) {
            if (noteResource.guid() == modifiedResource.guid()) {
                noteResource = modifiedResource;
                break;
            }
        }
        modifiedNote.setResources(noteResources);
        modifiedNote.setLocallyModified(false);
        modifiedNote.setLocalOnly(false);
        // NOTE: intentionally leaving the update sequence number to stay
        // as it is within note

        res = m_pFakeNoteStore->setNote(modifiedNote, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        // Need to update the linked notebook's sync state
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(*pNotebook->linkedNotebookGuid()));

        const auto * pLinkedNotebook = m_pFakeNoteStore->findLinkedNotebook(
            *pNotebook->linkedNotebookGuid());

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }
}

void SynchronizationTester::setExpungedUserOwnItemsToRemoteStorage()
{
    QVERIFY(!m_guidsOfUserOwnRemoteItemsToExpunge.m_savedSearchGuids.isEmpty());

    for (const auto & savedSearchGuid:
         qAsConst(m_guidsOfUserOwnRemoteItemsToExpunge.m_savedSearchGuids))
    {
        m_pFakeNoteStore->setExpungedSavedSearchGuid(savedSearchGuid);
    }

    QVERIFY(!m_guidsOfUserOwnRemoteItemsToExpunge.m_tagGuids.isEmpty());

    for (const auto & tagGuid:
         qAsConst(m_guidsOfUserOwnRemoteItemsToExpunge.m_tagGuids))
    {
        m_pFakeNoteStore->setExpungedTagGuid(tagGuid);
    }

    QVERIFY(!m_guidsOfUserOwnRemoteItemsToExpunge.m_notebookGuids.isEmpty());

    for (const auto & notebookGuid:
         qAsConst(m_guidsOfUserOwnRemoteItemsToExpunge.m_notebookGuids))
    {
        m_pFakeNoteStore->setExpungedNotebookGuid(notebookGuid);
    }

    QVERIFY(!m_guidsOfUserOwnRemoteItemsToExpunge.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfUserOwnRemoteItemsToExpunge.m_noteGuids))
    {
        m_pFakeNoteStore->setExpungedNoteGuid(noteGuid);
    }
}

void SynchronizationTester::setExpungedLinkedNotebookItemsToRemoteStorage()
{
    QVERIFY(!m_guidsOfLinkedNotebookRemoteItemsToExpunge.m_tagGuids.isEmpty());

    for (const auto & tagGuid:
         qAsConst(m_guidsOfLinkedNotebookRemoteItemsToExpunge.m_tagGuids))
    {
        m_pFakeNoteStore->setExpungedTagGuid(tagGuid);
    }

    QVERIFY(!m_guidsOfLinkedNotebookRemoteItemsToExpunge.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfLinkedNotebookRemoteItemsToExpunge.m_noteGuids))
    {
        m_pFakeNoteStore->setExpungedNoteGuid(noteGuid);
    }
}

void SynchronizationTester::
    setExpungedLinkedNotebookNotesToRemoteStorageToProduceNotelessLinkedNotebookTags()
{
    QVERIFY(
        !m_guidsOfLinkedNotebookNotesToExpungeToProduceNotelessLinkedNotebookTags
             .isEmpty());

    for (
        const auto & noteGuid: qAsConst(
            m_guidsOfLinkedNotebookNotesToExpungeToProduceNotelessLinkedNotebookTags))
    {
        m_pFakeNoteStore->setExpungedNoteGuid(noteGuid);
    }
}

void SynchronizationTester::expungeNotelessLinkedNotebookTagsFromRemoteStorage()
{
    QVERIFY(!m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged.isEmpty());

    for (const auto & tagGuid:
         qAsConst(m_guidsOfLinkedNotebookTagsExpectedToBeAutoExpunged))
    {
        m_pFakeNoteStore->setExpungedTagGuid(tagGuid);
    }
}

void SynchronizationTester::setNewUserOwnItemsToLocalStorage()
{
    ErrorString errorDescription;
    bool res = false;

    qevercloud::SavedSearch firstLocalSavedSearch;
    firstLocalSavedSearch.setName(QStringLiteral("First local saved search"));

    firstLocalSavedSearch.setQuery(
        QStringLiteral("First local saved search query"));

    firstLocalSavedSearch.setLocallyModified(true);
    firstLocalSavedSearch.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addSavedSearch(
        firstLocalSavedSearch, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::SavedSearch secondLocalSavedSearch;
    secondLocalSavedSearch.setName(QStringLiteral("Second local saved search"));

    secondLocalSavedSearch.setQuery(
        QStringLiteral("Second local saved search query"));

    secondLocalSavedSearch.setLocallyModified(true);
    secondLocalSavedSearch.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addSavedSearch(
        secondLocalSavedSearch, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::SavedSearch thirdLocalSavedSearch;
    thirdLocalSavedSearch.setName(QStringLiteral("Third local saved search"));

    thirdLocalSavedSearch.setQuery(
        QStringLiteral("Third local saved search query"));

    thirdLocalSavedSearch.setLocallyModified(true);
    thirdLocalSavedSearch.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addSavedSearch(
        thirdLocalSavedSearch, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag firstLocalTag;
    firstLocalTag.setName(QStringLiteral("First local tag"));
    firstLocalTag.setLocallyModified(true);
    firstLocalTag.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(
        firstLocalTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag secondLocalTag;
    secondLocalTag.setName(QStringLiteral("Second local tag"));
    secondLocalTag.setParentTagLocalId(firstLocalTag.localId());
    secondLocalTag.setLocallyModified(true);
    secondLocalTag.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(
        secondLocalTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag thirdLocalTag;
    thirdLocalTag.setName(QStringLiteral("Third local tag"));
    thirdLocalTag.setParentTagLocalId(secondLocalTag.localId());
    thirdLocalTag.setLocallyModified(true);
    thirdLocalTag.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(
        thirdLocalTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook firstLocalNotebook;
    firstLocalNotebook.setName(QStringLiteral("First local notebook"));
    firstLocalNotebook.setDefaultNotebook(false);
    firstLocalNotebook.setLocallyModified(true);
    firstLocalNotebook.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNotebook(
        firstLocalNotebook, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook secondLocalNotebook;
    secondLocalNotebook.setName(QStringLiteral("Second local notebook"));
    secondLocalNotebook.setDefaultNotebook(false);
    secondLocalNotebook.setLocallyModified(true);
    secondLocalNotebook.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNotebook(
        secondLocalNotebook, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook thirdLocalNotebook;
    thirdLocalNotebook.setName(QStringLiteral("Third local notebook"));
    thirdLocalNotebook.setDefaultNotebook(false);
    thirdLocalNotebook.setLocallyModified(true);
    thirdLocalNotebook.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNotebook(
        thirdLocalNotebook, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note firstLocalNote;
    firstLocalNote.setNotebookLocalId(firstLocalNotebook.localId());
    firstLocalNote.setTitle(QStringLiteral("First local note"));

    firstLocalNote.setContent(
        QStringLiteral("<en-note><div>First local note</div></en-note>"));

    firstLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    firstLocalNote.setUpdated(firstLocalNote.created());
    firstLocalNote.setLocallyModified(true);
    firstLocalNote.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        firstLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note secondLocalNote;
    secondLocalNote.setNotebookLocalId(firstLocalNotebook.localId());
    secondLocalNote.setTitle(QStringLiteral("Second local note"));

    secondLocalNote.setContent(
        QStringLiteral("<en-note><div>Second local note</div></en-note>"));

    secondLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    secondLocalNote.setUpdated(
        secondLocalNote.created());
    secondLocalNote.setTagLocalIds(
        QStringList{} << firstLocalTag.localId() << secondLocalTag.localId());
    secondLocalNote.setLocallyModified(true);
    secondLocalNote.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        secondLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note thirdLocalNote;
    thirdLocalNote.setNotebookLocalId(secondLocalNotebook.localId());
    thirdLocalNote.setTitle(QStringLiteral("Third local note"));

    thirdLocalNote.setContent(
        QStringLiteral("<en-note><div>Third local note</div></en-note>"));

    thirdLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    thirdLocalNote.setUpdated(thirdLocalNote.created());
    thirdLocalNote.mutableTagLocalIds().append(thirdLocalTag.localId());
    thirdLocalNote.setLocallyModified(true);
    thirdLocalNote.setLocalOnly(false);

    qevercloud::Resource thirdLocalNoteResource;
    thirdLocalNoteResource.setNoteLocalId(thirdLocalNote.localId());
    thirdLocalNoteResource.setMime(QStringLiteral("text/plain"));

    thirdLocalNoteResource.setData(qevercloud::Data{});
    thirdLocalNoteResource.mutableData()->setBody(
        QByteArray("Third note first resource data body"));

    thirdLocalNoteResource.mutableData()->setSize(
        thirdLocalNoteResource.data()->body()->size());

    thirdLocalNoteResource.mutableData()->setBodyHash(QCryptographicHash::hash(
        *thirdLocalNoteResource.data()->body(), QCryptographicHash::Md5));

    thirdLocalNoteResource.setLocallyModified(true);
    thirdLocalNoteResource.setLocalOnly(false);

    thirdLocalNote.setResources(
        QList<qevercloud::Resource>{} << thirdLocalNoteResource);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        thirdLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note fourthLocalNote;
    fourthLocalNote.setNotebookLocalId(thirdLocalNotebook.localId());
    fourthLocalNote.setTitle(QStringLiteral("Fourth local note"));

    fourthLocalNote.setContent(
        QStringLiteral("<en-note><div>Fourth local note</div></en-note>"));

    fourthLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());

    fourthLocalNote.setUpdated(
        fourthLocalNote.created());

    fourthLocalNote.setTagLocalIds(
        QStringList{} << secondLocalTag.localId() << thirdLocalTag.localId());

    fourthLocalNote.setLocallyModified(true);
    fourthLocalNote.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        fourthLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
}

void SynchronizationTester::setNewLinkedNotebookItemsToLocalStorage()
{
    ErrorString errorDescription;
    bool res = false;

    auto linkedNotebooks = m_pLocalStorageManagerAsync->localStorageManager()
                               ->listAllLinkedNotebooks(errorDescription);

    QVERIFY2(
        !linkedNotebooks.isEmpty(),
        qPrintable(errorDescription.nonLocalizedString()));

    QVERIFY2(
        linkedNotebooks.size() == 3,
        qPrintable(
            QString::fromUtf8("Expected to find 3 linked notebooks "
                              "in the local storage, instead found ") +
            QString::number(linkedNotebooks.size())));

    qevercloud::Tag firstLocalTag;

    firstLocalTag.setName(
        QStringLiteral("First local tag in a linked notebook"));

    firstLocalTag.setLocallyModified(true);
    firstLocalTag.setLocalOnly(false);
    firstLocalTag.setLinkedNotebookGuid(linkedNotebooks[0].guid());

    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(
        firstLocalTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag secondLocalTag;

    secondLocalTag.setName(
        QStringLiteral("Second local tag in a linked notebook"));

    secondLocalTag.setLocallyModified(true);
    secondLocalTag.setLocalOnly(false);
    secondLocalTag.setLinkedNotebookGuid(linkedNotebooks[0].guid());

    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(
        secondLocalTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag thirdLocalTag;

    thirdLocalTag.setName(
        QStringLiteral("Third local tag in a linked notebook"));

    thirdLocalTag.setLocallyModified(true);
    thirdLocalTag.setLocalOnly(false);
    thirdLocalTag.setLinkedNotebookGuid(linkedNotebooks[1].guid());

    res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(
        thirdLocalTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    QString firstNotebookGuid;
    QString secondNotebookGuid;
    QString thirdNotebookGuid;

    QString firstNotebookLocalId;
    QString secondNotebookLocalId;
    QString thirdNotebookLocalId;

    auto notebooks =
        m_pLocalStorageManagerAsync->localStorageManager()->listAllNotebooks(
            errorDescription);

    QVERIFY2(
        !notebooks.isEmpty(),
        qPrintable(errorDescription.nonLocalizedString()));

    for (const auto & notebook: qAsConst(notebooks)) {
        if (!notebook.guid() || !notebook.linkedNotebookGuid()) {
            continue;
        }

        const QString & linkedNotebookGuid = *notebook.linkedNotebookGuid();
        if (linkedNotebookGuid == linkedNotebooks[0].guid()) {
            firstNotebookGuid = *notebook.guid();
            firstNotebookLocalId = notebook.localId();
        }
        else if (linkedNotebookGuid == linkedNotebooks[1].guid()) {
            secondNotebookGuid = *notebook.guid();
            secondNotebookLocalId = notebook.localId();
        }
        else if (linkedNotebookGuid == linkedNotebooks[2].guid()) {
            thirdNotebookGuid = *notebook.guid();
            thirdNotebookLocalId = notebook.localId();
        }

        if (!firstNotebookGuid.isEmpty() && !secondNotebookGuid.isEmpty() &&
            !thirdNotebookGuid.isEmpty())
        {
            break;
        }
    }

    QVERIFY2(
        !firstNotebookGuid.isEmpty(),
        "Wasn't able to find the guid of the notebook corresponding "
        "to the first linked notebook");

    QVERIFY2(
        !secondNotebookGuid.isEmpty(),
        "Wasn't able to find the guid of the notebook corresponding "
        "to the second linked notebook");

    QVERIFY2(
        !thirdNotebookGuid.isEmpty(),
        "Wasn't able to tinf the guid of the notebook corresponding "
        "to the third linked notebook");

    qevercloud::Note firstLocalNote;
    firstLocalNote.setNotebookGuid(firstNotebookGuid);
    firstLocalNote.setNotebookLocalId(firstNotebookLocalId);

    firstLocalNote.setTitle(
        QStringLiteral("First local note in a linked notebook"));

    firstLocalNote.setContent(
        QStringLiteral("<en-note><div>First local note in a linked notebook"
                       "</div></en-note>"));

    firstLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    firstLocalNote.setUpdated(firstLocalNote.created());
    firstLocalNote.setLocallyModified(true);
    firstLocalNote.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        firstLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note secondLocalNote;
    secondLocalNote.setNotebookGuid(secondNotebookGuid);
    secondLocalNote.setNotebookLocalId(secondNotebookLocalId);

    secondLocalNote.setTitle(
        QStringLiteral("Second local note in a linked notebook"));

    secondLocalNote.setContent(
        QStringLiteral("<en-note><div>Second local note in a linked notebook"
                       "</div></en-note>"));

    secondLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    secondLocalNote.setUpdated(
        secondLocalNote.created());
    secondLocalNote.setTagLocalIds(
        QStringList{} << firstLocalTag.localId() << secondLocalTag.localId());
    secondLocalNote.setLocallyModified(true);
    secondLocalNote.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        secondLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note thirdLocalNote;
    thirdLocalNote.setNotebookGuid(thirdNotebookGuid);
    thirdLocalNote.setNotebookLocalId(thirdNotebookLocalId);

    thirdLocalNote.setTitle(
        QStringLiteral("Third local note in a linked notebook"));

    thirdLocalNote.setContent(
        QStringLiteral("<en-note><div>Third local note in a linked notebook"
                       "</div></en-note>"));

    thirdLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    thirdLocalNote.setUpdated(thirdLocalNote.created());
    thirdLocalNote.setTagLocalIds(QStringList{} << thirdLocalTag.localId());
    thirdLocalNote.setLocallyModified(true);
    thirdLocalNote.setLocalOnly(false);

    qevercloud::Resource thirdLocalNoteResource;
    thirdLocalNoteResource.setNoteLocalId(thirdLocalNote.localId());
    thirdLocalNoteResource.setMime(QStringLiteral("text/plain"));

    thirdLocalNoteResource.setData(qevercloud::Data{});
    thirdLocalNoteResource.mutableData()->setBody(
        QByteArray("Third linked notebook's note's first resource data body"));

    thirdLocalNoteResource.mutableData()->setSize(
        thirdLocalNoteResource.data()->body()->size());

    thirdLocalNoteResource.mutableData()->setBodyHash(QCryptographicHash::hash(
        *thirdLocalNoteResource.data()->body(), QCryptographicHash::Md5));

    thirdLocalNoteResource.setLocallyModified(true);
    thirdLocalNoteResource.setLocalOnly(false);

    thirdLocalNote.setResources(
        QList<qevercloud::Resource>{} << thirdLocalNoteResource);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        thirdLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note fourthLocalNote;
    fourthLocalNote.setNotebookGuid(thirdNotebookGuid);
    fourthLocalNote.setNotebookLocalId(thirdNotebookLocalId);

    fourthLocalNote.setTitle(
        QStringLiteral("Fourth local note in a linked notebook"));

    fourthLocalNote.setContent(
        QStringLiteral("<en-note><div>Fourth local note in a linked notebook"
                       "</div></en-note>"));

    fourthLocalNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    fourthLocalNote.setUpdated(
        fourthLocalNote.created());
    fourthLocalNote.setTagLocalIds(
        QStringList{} << secondLocalTag.localId() << thirdLocalTag.localId());
    fourthLocalNote.setLocallyModified(true);
    fourthLocalNote.setLocalOnly(false);

    res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
        fourthLocalNote, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
}

void SynchronizationTester::setNewUserOwnItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    qevercloud::SavedSearch fourthSearch;
    fourthSearch.setGuid(UidGenerator::Generate());
    fourthSearch.setName(QStringLiteral("Fourth saved search"));
    fourthSearch.setQuery(QStringLiteral("Fourth saved search query"));
    res = m_pFakeNoteStore->setSavedSearch(fourthSearch, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag fourthTag;
    fourthTag.setGuid(UidGenerator::Generate());
    fourthTag.setName(QStringLiteral("Fourth tag"));
    res = m_pFakeNoteStore->setTag(fourthTag, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook fourthNotebook;
    fourthNotebook.setGuid(UidGenerator::Generate());
    fourthNotebook.setName(QStringLiteral("Fourth notebook"));
    fourthNotebook.setDefaultNotebook(false);
    res = m_pFakeNoteStore->setNotebook(fourthNotebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note sixthNote;
    sixthNote.setGuid(UidGenerator::Generate());
    sixthNote.setNotebookGuid(fourthNotebook.guid());
    sixthNote.setNotebookLocalId(fourthNotebook.localId());
    sixthNote.setTitle(QStringLiteral("Sixth note"));

    sixthNote.setContent(
        QStringLiteral("<en-note><div>Sixth note</div></en-note>"));

    sixthNote.setContentLength(sixthNote.content()->size());

    sixthNote.setContentHash(QCryptographicHash::hash(
        sixthNote.content()->toUtf8(), QCryptographicHash::Md5));

    sixthNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    sixthNote.setUpdated(sixthNote.created());
    res = m_pFakeNoteStore->setNote(sixthNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note seventhNote;
    seventhNote.setGuid(UidGenerator::Generate());
    seventhNote.setNotebookGuid(fourthNotebook.guid());
    seventhNote.setNotebookLocalId(fourthNotebook.localId());
    seventhNote.setTitle(QStringLiteral("Seventh note"));

    seventhNote.setContent(
        QStringLiteral("<en-note><div>Seventh note</div></en-note>"));

    seventhNote.setContentLength(seventhNote.content()->size());

    seventhNote.setContentHash(QCryptographicHash::hash(
        seventhNote.content()->toUtf8(), QCryptographicHash::Md5));

    seventhNote.setCreated(QDateTime::currentMSecsSinceEpoch());
    seventhNote.setUpdated(seventhNote.created());

    seventhNote.setTagGuids(
        QList<qevercloud::Guid>{} << fourthTag.guid().value());

    seventhNote.setTagLocalIds(QStringList{} << fourthTag.localId());

    qevercloud::Resource seventhNoteFirstResource;
    seventhNoteFirstResource.setGuid(UidGenerator::Generate());
    seventhNoteFirstResource.setNoteGuid(seventhNote.guid());
    seventhNoteFirstResource.setNoteLocalId(seventhNote.localId());
    seventhNoteFirstResource.setMime(QStringLiteral("text/plain"));

    seventhNoteFirstResource.setData(qevercloud::Data{});
    seventhNoteFirstResource.mutableData()->setBody(
        QByteArray("Seventh note first resource data body"));

    seventhNoteFirstResource.mutableData()->setSize(
        seventhNoteFirstResource.data()->body()->size());

    seventhNoteFirstResource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *seventhNoteFirstResource.data()->body(), QCryptographicHash::Md5));

    seventhNote.setResources(
        QList<qevercloud::Resource>{} << seventhNoteFirstResource);

    res = m_pFakeNoteStore->setNote(seventhNote, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
}

void SynchronizationTester::setNewLinkedNotebookItemsToRemoteStorage()
{
    ErrorString errorDescription;
    bool res = false;

    auto existingLinkedNotebooks = m_pFakeNoteStore->linkedNotebooks();

    for (const auto it:
         qevercloud::toRange(::qAsConst(existingLinkedNotebooks))) {
        const QString & linkedNotebookGuid = it.key();

        auto notebooks = m_pFakeNoteStore->findNotebooksForLinkedNotebookGuid(
            linkedNotebookGuid);

        QVERIFY2(
            notebooks.size() == 1,
            "Unexpected number of notebooks per linked notebook guid");

        const qevercloud::Notebook * pNotebook = *(notebooks.constBegin());

        qevercloud::Tag newTag;
        newTag.setGuid(UidGenerator::Generate());

        newTag.setName(
            QStringLiteral("New tag for linked notebook with guid ") +
            linkedNotebookGuid);

        newTag.setLinkedNotebookGuid(linkedNotebookGuid);
        res = m_pFakeNoteStore->setTag(newTag, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        qevercloud::Note newNote;
        newNote.setGuid(UidGenerator::Generate());
        newNote.setNotebookGuid(pNotebook->guid());
        newNote.setNotebookLocalId(pNotebook->localId());

        newNote.setTitle(
            QStringLiteral("New note for linked notebook with guid ") +
            linkedNotebookGuid);

        newNote.setContent(
            QStringLiteral("<en-note><div>New linked notebook note "
                           "content</div></en-note>"));

        newNote.setContentLength(newNote.content()->size());

        newNote.setContentHash(QCryptographicHash::hash(
            newNote.content()->toUtf8(), QCryptographicHash::Md5));

        newNote.setCreated(QDateTime::currentMSecsSinceEpoch());
        newNote.setUpdated(newNote.created());
        newNote.setTagGuids(QList<qevercloud::Guid>{} << newTag.guid().value());
        newNote.setTagLocalIds(QStringList{} << newTag.localId());

        qevercloud::Resource newNoteResource;
        newNoteResource.setGuid(UidGenerator::Generate());
        newNoteResource.setNoteGuid(newNote.guid());
        newNoteResource.setNoteLocalId(newNote.localId());
        newNoteResource.setMime(QStringLiteral("text/plain"));

        newNoteResource.setData(qevercloud::Data{});
        newNoteResource.mutableData()->setBody(
            QByteArray("New note resource data body"));

        newNoteResource.mutableData()->setSize(
            newNoteResource.data()->body()->size());

        newNoteResource.mutableData()->setBodyHash(QCryptographicHash::hash(
            *newNoteResource.data()->body(), QCryptographicHash::Md5));

        newNote.setResources(QList<qevercloud::Resource>{} << newNoteResource);

        res = m_pFakeNoteStore->setNote(newNote, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        // Need to update the sync state for this linked notebook
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(linkedNotebookGuid));

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            it.value().username().value(), syncState);
    }

    qevercloud::LinkedNotebook fourthLinkedNotebook;
    fourthLinkedNotebook.setGuid(UidGenerator::Generate());

    fourthLinkedNotebook.setUsername(
        QStringLiteral("Fourth linked notebook owner"));

    fourthLinkedNotebook.setShareName(
        QStringLiteral("Fourth linked notebook share name"));

    fourthLinkedNotebook.setShardId(UidGenerator::Generate());
    fourthLinkedNotebook.setSharedNotebookGlobalId(UidGenerator::Generate());

    fourthLinkedNotebook.setNoteStoreUrl(
        QStringLiteral("Fourth linked notebook fake note store URL"));

    fourthLinkedNotebook.setWebApiUrlPrefix(
        QStringLiteral("Fourth linked notebook fake web API URL prefix"));

    res = m_pFakeNoteStore->setLinkedNotebook(
        fourthLinkedNotebook, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    m_pFakeNoteStore->setLinkedNotebookAuthToken(
        fourthLinkedNotebook.username().value(), UidGenerator::Generate());

    qevercloud::Tag fourthLinkedNotebookFirstTag;
    fourthLinkedNotebookFirstTag.setGuid(UidGenerator::Generate());

    fourthLinkedNotebookFirstTag.setName(
        QStringLiteral("Fourth linked notebook first tag"));

    fourthLinkedNotebookFirstTag.setLinkedNotebookGuid(
        fourthLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        fourthLinkedNotebookFirstTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag fourthLinkedNotebookSecondTag;
    fourthLinkedNotebookSecondTag.setGuid(UidGenerator::Generate());

    fourthLinkedNotebookSecondTag.setName(
        QStringLiteral("Fourth linked notebook second tag"));

    fourthLinkedNotebookSecondTag.setParentGuid(
        fourthLinkedNotebookFirstTag.guid());

    fourthLinkedNotebookSecondTag.setParentTagLocalId(
        fourthLinkedNotebookFirstTag.localId());

    fourthLinkedNotebookSecondTag.setLinkedNotebookGuid(
        fourthLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        fourthLinkedNotebookSecondTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Tag fourthLinkedNotebookThirdTag;
    fourthLinkedNotebookThirdTag.setGuid(UidGenerator::Generate());

    fourthLinkedNotebookThirdTag.setName(
        QStringLiteral("Fourth linked notebook third tag"));

    fourthLinkedNotebookThirdTag.setParentGuid(
        fourthLinkedNotebookSecondTag.guid());

    fourthLinkedNotebookThirdTag.setParentTagLocalId(
        fourthLinkedNotebookSecondTag.localId());

    fourthLinkedNotebookThirdTag.setLinkedNotebookGuid(
        fourthLinkedNotebook.guid());

    res = m_pFakeNoteStore->setTag(
        fourthLinkedNotebookThirdTag, errorDescription);

    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("Fourth linked notebook's notebook"));
    notebook.setDefaultNotebook(false);
    notebook.setLinkedNotebookGuid(fourthLinkedNotebook.guid());
    res = m_pFakeNoteStore->setNotebook(notebook, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    qevercloud::Note note;
    note.setGuid(UidGenerator::Generate());
    note.setNotebookGuid(notebook.guid());
    note.setNotebookLocalId(notebook.localId());

    note.setTitle(
        QStringLiteral("First note for linked notebook with guid ") +
        fourthLinkedNotebook.guid().value());

    note.setContent(
        QStringLiteral("<en-note><div>Fourth linked notebook's first note "
                       "content</div></en-note>"));

    note.setContentLength(note.content()->size());

    note.setContentHash(QCryptographicHash::hash(
        note.content()->toUtf8(), QCryptographicHash::Md5));

    note.setCreated(QDateTime::currentMSecsSinceEpoch());
    note.setUpdated(note.created());

    note.setTagGuids(
        QList<qevercloud::Guid>{} << fourthLinkedNotebookFirstTag.guid().value()
        << fourthLinkedNotebookSecondTag.guid().value()
        << fourthLinkedNotebookThirdTag.guid().value());

    note.setTagLocalIds(
        QStringList() << fourthLinkedNotebookFirstTag.localId()
        << fourthLinkedNotebookSecondTag.localId()
        << fourthLinkedNotebookThirdTag.localId());

    qevercloud::Resource resource;
    resource.setGuid(UidGenerator::Generate());
    resource.setNoteGuid(note.guid());
    resource.setNoteLocalId(note.localId());
    resource.setMime(QStringLiteral("text/plain"));
    resource.setData(qevercloud::Data{});
    resource.mutableData()->setBody(
        QByteArray("qevercloud::Note resource data body"));
    resource.mutableData()->setSize(resource.data()->body()->size());

    resource.mutableData()->setBodyHash(
        QCryptographicHash::hash(
            *resource.data()->body(), QCryptographicHash::Md5));

    note.setResources(QList<qevercloud::Resource>{} << resource);
    res = m_pFakeNoteStore->setNote(note, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    // Need to set linked notebook sync state for the fourth linked notebook
    // since it might be required in incremental sync
    qevercloud::SyncState syncState;
    syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

    syncState.setFullSyncBefore(
        QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

    syncState.setUploaded(42);

    syncState.setUpdateCount(
        m_pFakeNoteStore->currentMaxUsn(fourthLinkedNotebook.guid().value()));

    m_pFakeNoteStore->setLinkedNotebookSyncState(
        fourthLinkedNotebook.username().value(), syncState);
}

void SynchronizationTester::setModifiedUserOwnItemsToLocalStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QVERIFY(!m_guidsOfUserOwnLocalItemsToModify.m_savedSearchGuids.isEmpty());

    for (const auto & savedSearchGuid:
         qAsConst(m_guidsOfUserOwnLocalItemsToModify.m_savedSearchGuids))
    {
        qevercloud::SavedSearch savedSearch;
        savedSearch.setGuid(savedSearchGuid);

        res =
            m_pLocalStorageManagerAsync->localStorageManager()->findSavedSearch(
                savedSearch, errorDescription);

        QVERIFY2(
            res,
            "Detected unexpectedly missing saved search in the local storage");

        QVERIFY2(
            savedSearch.name(),
            "Detected saved search without a name in the local storage");

        savedSearch.setName(*savedSearch.name() + gModifiedLocallySuffix);
        savedSearch.setLocallyModified(true);

        res = m_pLocalStorageManagerAsync->localStorageManager()
                  ->updateSavedSearch(savedSearch, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    QVERIFY(!m_guidsOfUserOwnLocalItemsToModify.m_tagGuids.isEmpty());

    for (const auto & tagGuid:
         qAsConst(m_guidsOfUserOwnLocalItemsToModify.m_tagGuids))
    {
        qevercloud::Tag tag;
        tag.setGuid(tagGuid);

        res = m_pLocalStorageManagerAsync->localStorageManager()->findTag(
            tag, errorDescription);

        QVERIFY2(res, "Detected unexpectedly missing tag in the local storage");

        QVERIFY2(
            tag.name(), "Detected tag without a name in the local storage");

        tag.setName(*tag.name() + gModifiedLocallySuffix);
        tag.setLocallyModified(true);

        res = m_pLocalStorageManagerAsync->localStorageManager()->updateTag(
            tag, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    QVERIFY(!m_guidsOfUserOwnLocalItemsToModify.m_notebookGuids.isEmpty());

    for (const auto & notebookGuid:
         qAsConst(m_guidsOfUserOwnLocalItemsToModify.m_notebookGuids))
    {
        qevercloud::Notebook notebook;
        notebook.setGuid(notebookGuid);

        res = m_pLocalStorageManagerAsync->localStorageManager()->findNotebook(
            notebook, errorDescription);

        QVERIFY2(
            res, "Detected unexpectedly missing notebook in the local storage");

        QVERIFY2(
            notebook.name(),
            "Detected notebook without a name in the local storage");

        notebook.setName(*notebook.name() + gModifiedLocallySuffix);
        notebook.setLocallyModified(true);

        res =
            m_pLocalStorageManagerAsync->localStorageManager()->updateNotebook(
                notebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    QVERIFY(!m_guidsOfUserOwnLocalItemsToModify.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfUserOwnLocalItemsToModify.m_noteGuids))
    {
        qevercloud::Note note;
        note.setGuid(noteGuid);

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata);

        res = pLocalStorageManager->findNote(note, options, errorDescription);
        QVERIFY2(
            res, "Detected unexpectedly missing note in the local storage");

        QVERIFY2(
            note.title(),
            "Detected note without title in the local storage");

        note.setTitle(*note.title() + gModifiedLocallySuffix);
        note.setLocallyModified(true);

        res = pLocalStorageManager->updateNote(
            note,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            LocalStorageManager::UpdateNoteOptions(),
#else
            LocalStorageManager::UpdateNoteOptions(0), // NOLINT
#endif
            errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::setModifiedLinkedNotebookItemsToLocalStorage()
{
    ErrorString errorDescription;
    bool res = false;

    QVERIFY(!m_guidsOfLinkedNotebookLocalItemsToModify.m_tagGuids.isEmpty());

    for (const auto & tagGuid:
         qAsConst(m_guidsOfLinkedNotebookLocalItemsToModify.m_tagGuids))
    {
        qevercloud::Tag tag;
        tag.setGuid(tagGuid);

        res = m_pLocalStorageManagerAsync->localStorageManager()->findTag(
            tag, errorDescription);

        QVERIFY2(res, "Detected unexpectedly missing tag in the local storage");

        QVERIFY2(
            tag.name(), "Detected tag without a name in the local storage");

        tag.setName(*tag.name() + gModifiedLocallySuffix);
        tag.setLocallyModified(true);

        res = m_pLocalStorageManagerAsync->localStorageManager()->updateTag(
            tag, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    QVERIFY(
        !m_guidsOfLinkedNotebookLocalItemsToModify.m_notebookGuids.isEmpty());

    for (const auto & notebookGuid:
         qAsConst(m_guidsOfLinkedNotebookLocalItemsToModify.m_notebookGuids))
    {
        qevercloud::Notebook notebook;
        notebook.setGuid(notebookGuid);

        res = m_pLocalStorageManagerAsync->localStorageManager()->findNotebook(
            notebook, errorDescription);

        QVERIFY2(
            res, "Detected unexpectedly missing notebook in the local storage");

        QVERIFY2(
            notebook.name(),
            "Detected notebook without a name in the local storage");

        notebook.setName(*notebook.name() + gModifiedLocallySuffix);
        notebook.setLocallyModified(true);

        res =
            m_pLocalStorageManagerAsync->localStorageManager()->updateNotebook(
                notebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    QVERIFY(!m_guidsOfLinkedNotebookLocalItemsToModify.m_noteGuids.isEmpty());

    for (const auto & noteGuid:
         qAsConst(m_guidsOfLinkedNotebookLocalItemsToModify.m_noteGuids))
    {
        qevercloud::Note note;
        note.setGuid(noteGuid);

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata);

        res = pLocalStorageManager->findNote(note, options, errorDescription);

        QVERIFY2(
            res, "Detected unexpectedly missing note in the local storage");

        QVERIFY2(
            note.title(),
            "Detected note without title in the local storage");

        note.setTitle(*note.title() + gModifiedLocallySuffix);
        note.setLocallyModified(true);

        res = pLocalStorageManager->updateNote(
            note,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            LocalStorageManager::UpdateNoteOptions(),
#else
            LocalStorageManager::UpdateNoteOptions(0), // NOLINT
#endif
            errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::
    setConflictingSavedSearchesFromUserOwnDataToLocalAndRemoteStorages(
        const ConflictingItemsUsnOption usnOption)
{
    QVERIFY(!m_guidsOfUserOwnLocalItemsToModify.m_savedSearchGuids.isEmpty());

    for (const auto & savedSearchGuid:
         qAsConst(m_guidsOfUserOwnLocalItemsToModify.m_savedSearchGuids))
    {
        const auto * pSavedSearch =
            m_pFakeNoteStore->findSavedSearch(savedSearchGuid);

        QVERIFY2(
            pSavedSearch != nullptr,
            "Detected unexpectedly missing saved search in fake note store");

        QString originalName = pSavedSearch->name().value();

        qevercloud::SavedSearch modifiedSavedSearch(*pSavedSearch);
        modifiedSavedSearch.setName(originalName + gModifiedRemotelySuffix);
        modifiedSavedSearch.setLocallyModified(false);
        modifiedSavedSearch.setLocalOnly(false);
        modifiedSavedSearch.setUpdateSequenceNum(std::nullopt);

        ErrorString errorDescription;

        bool res = m_pFakeNoteStore->setSavedSearch(
            modifiedSavedSearch, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        m_expectedSavedSearchNamesByGuid[savedSearchGuid] =
            modifiedSavedSearch.name().value();

        if (usnOption == ConflictingItemsUsnOption::LargerRemoteUsn) {
            modifiedSavedSearch = *pSavedSearch;
            modifiedSavedSearch.setLocallyModified(true);
            modifiedSavedSearch.setLocalOnly(false);
        }

        modifiedSavedSearch.setLocalId(QString());
        modifiedSavedSearch.setName(originalName + gModifiedLocallySuffix);

        res = m_pLocalStorageManagerAsync->localStorageManager()
                  ->updateSavedSearch(modifiedSavedSearch, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::
    setConflictingTagsFromUserOwnDataToLocalAndRemoteStorages(
        const ConflictingItemsUsnOption usnOption)
{
    setConflictingTagsToLocalAndRemoteStoragesImpl(
        m_guidsOfUserOwnLocalItemsToModify.m_tagGuids, usnOption,
        /* should have linked notebook guid = */ false);
}

void SynchronizationTester::
    setConflictingNotebooksFromUserOwnDataToLocalAndRemoteStorages(
        const ConflictingItemsUsnOption usnOption)
{
    setConflictingNotebooksToLocalAndRemoteStoragesImpl(
        m_guidsOfUserOwnLocalItemsToModify.m_notebookGuids, usnOption,
        /* should have linked notebook guid = */ false);
}

void SynchronizationTester::
    setConflictingNotesFromUserOwnDataToLocalAndRemoteStorages(
        const ConflictingItemsUsnOption usnOption)
{
    setConflictingNotesToLocalAndRemoteStoragesImpl(
        m_guidsOfUserOwnLocalItemsToModify.m_noteGuids, usnOption);
}

void SynchronizationTester::
    setConflictingTagsFromLinkedNotebooksToLocalAndRemoteStorages(
        const ConflictingItemsUsnOption usnOption)
{
    setConflictingTagsToLocalAndRemoteStoragesImpl(
        m_guidsOfLinkedNotebookLocalItemsToModify.m_tagGuids, usnOption,
        /* should have linked notebook guid = */ true);
}

void SynchronizationTester::
    setConflictingNotebooksFromLinkedNotebooksToLocalAndRemoteStorages(
        const ConflictingItemsUsnOption usnOption)
{
    setConflictingNotebooksToLocalAndRemoteStoragesImpl(
        m_guidsOfLinkedNotebookLocalItemsToModify.m_notebookGuids, usnOption,
        /* should have linked notebook guid = */ true);
}

void SynchronizationTester::
    setConflictingNotesFromLinkedNotebooksToLocalAndRemoteStorages(
        const ConflictingItemsUsnOption usnOption)
{
    setConflictingNotesToLocalAndRemoteStoragesImpl(
        m_guidsOfLinkedNotebookLocalItemsToModify.m_noteGuids, usnOption);
}

void SynchronizationTester::setConflictingTagsToLocalAndRemoteStoragesImpl(
    const QStringList & sourceTagGuids,
    const ConflictingItemsUsnOption usnOption,
    const bool shouldHaveLinkedNotebookGuid)
{
    QVERIFY(!sourceTagGuids.isEmpty());

    for (const auto & tagGuid: qAsConst(sourceTagGuids)) {
        const auto * pRemoteTag = m_pFakeNoteStore->findTag(tagGuid);

        QVERIFY2(
            pRemoteTag != nullptr,
            "Detected unexpectedly missing tag in fake note store");

        QVERIFY(
            pRemoteTag->linkedNotebookGuid().has_value() ==
            shouldHaveLinkedNotebookGuid);

        QString originalName = pRemoteTag->name().value();

        qevercloud::Tag modifiedRemoteTag(*pRemoteTag);
        modifiedRemoteTag.setName(originalName + gModifiedRemotelySuffix);
        modifiedRemoteTag.setLocallyModified(false);
        modifiedRemoteTag.setLocalOnly(false);
        modifiedRemoteTag.setUpdateSequenceNum(std::nullopt);

        ErrorString errorDescription;
        bool res =
            m_pFakeNoteStore->setTag(modifiedRemoteTag, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        m_expectedTagNamesByGuid[tagGuid] = modifiedRemoteTag.name().value();

        qevercloud::Tag localTag;
        localTag.setGuid(tagGuid);

        res = m_pLocalStorageManagerAsync->localStorageManager()->findTag(
            localTag, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        qevercloud::Tag modifiedLocalTag(localTag);
        modifiedLocalTag.setName(originalName + gModifiedLocallySuffix);
        modifiedLocalTag.setLocallyModified(true);
        modifiedLocalTag.setLocalOnly(false);

        if (usnOption == ConflictingItemsUsnOption::SameUsn) {
            modifiedLocalTag.setUpdateSequenceNum(
                modifiedRemoteTag.updateSequenceNum().value());
        }

        res = m_pLocalStorageManagerAsync->localStorageManager()->updateTag(
            modifiedLocalTag, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        if (!pRemoteTag->linkedNotebookGuid()) {
            continue;
        }

        // Need to update the linked notebook's sync state
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(*pRemoteTag->linkedNotebookGuid()));

        const auto * pLinkedNotebook = m_pFakeNoteStore->findLinkedNotebook(
            *pRemoteTag->linkedNotebookGuid());

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }
}

void SynchronizationTester::setConflictingNotebooksToLocalAndRemoteStoragesImpl(
    const QStringList & sourceNotebookGuids,
    const ConflictingItemsUsnOption usnOption,
    const bool shouldHaveLinkedNotebookGuid)
{
    QVERIFY(!sourceNotebookGuids.isEmpty());

    for (const auto & notebookGuid: qAsConst(sourceNotebookGuids)) {
        const auto * pNotebook = m_pFakeNoteStore->findNotebook(notebookGuid);

        QVERIFY2(
            pNotebook != nullptr,
            "Detected unexpectedly missing notebook in fake note store");

        QVERIFY(
            pNotebook->linkedNotebookGuid().has_value() ==
            shouldHaveLinkedNotebookGuid);

        QString originalName = pNotebook->name().value();

        qevercloud::Notebook modifiedNotebook(*pNotebook);
        modifiedNotebook.setName(originalName + gModifiedRemotelySuffix);
        modifiedNotebook.setLocallyModified(false);
        modifiedNotebook.setLocalOnly(false);
        modifiedNotebook.setUpdateSequenceNum(std::nullopt);

        ErrorString errorDescription;

        bool res =
            m_pFakeNoteStore->setNotebook(modifiedNotebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        m_expectedNotebookNamesByGuid[notebookGuid] =
            modifiedNotebook.name().value();

        if (usnOption == ConflictingItemsUsnOption::LargerRemoteUsn) {
            modifiedNotebook = *pNotebook;
            modifiedNotebook.setLocallyModified(true);
            modifiedNotebook.setLocalOnly(false);
        }

        modifiedNotebook.setLocalId(QString());
        modifiedNotebook.setName(originalName + gModifiedLocallySuffix);

        res =
            m_pLocalStorageManagerAsync->localStorageManager()->updateNotebook(
                modifiedNotebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        if (!pNotebook->linkedNotebookGuid()) {
            continue;
        }

        // Need to update the linked notebook's sync state
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(*pNotebook->linkedNotebookGuid()));

        const auto * pLinkedNotebook = m_pFakeNoteStore->findLinkedNotebook(
            *pNotebook->linkedNotebookGuid());

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }
}

void SynchronizationTester::setConflictingNotesToLocalAndRemoteStoragesImpl(
    const QStringList & sourceNoteGuids,
    const ConflictingItemsUsnOption usnOption)
{
    QVERIFY(!sourceNoteGuids.isEmpty());

    for (const auto & noteGuid: qAsConst(sourceNoteGuids)) {
        const auto * pNote = m_pFakeNoteStore->findNote(noteGuid);

        QVERIFY2(
            pNote != nullptr,
            "Detected unexpectedly missing note in fake note store");

        QString originalTitle = pNote->title().value();
        qint32 originalUsn = pNote->updateSequenceNum().value();

        qevercloud::Note modifiedNote(*pNote);
        modifiedNote.setTitle(originalTitle + gModifiedRemotelySuffix);
        modifiedNote.setLocallyModified(false);
        modifiedNote.setLocalOnly(false);
        modifiedNote.setUpdateSequenceNum(std::nullopt);

        // Remove any resources the note might have had to make the test more
        // interesting
        modifiedNote.setResources(std::nullopt);

        ErrorString errorDescription;
        bool res = m_pFakeNoteStore->setNote(modifiedNote, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        if (usnOption == ConflictingItemsUsnOption::SameUsn) {
            m_expectedNoteTitlesByGuid[noteGuid] =
                originalTitle + gModifiedLocallySuffix;
        }
        else {
            modifiedNote.setUpdateSequenceNum(originalUsn);
            m_expectedNoteTitlesByGuid[noteGuid] = modifiedNote.title().value();
        }

        modifiedNote.setLocallyModified(true);
        modifiedNote.setLocalId(QString());
        modifiedNote.setTitle(originalTitle + gModifiedLocallySuffix);

        res = m_pLocalStorageManagerAsync->localStorageManager()->updateNote(
            modifiedNote,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            LocalStorageManager::UpdateNoteOptions(),
#else
            LocalStorageManager::UpdateNoteOptions(0), // NOLINT
#endif
            errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(pNote->notebookGuid().value());

        QVERIFY(pNotebook != nullptr);

        if (!pNotebook->linkedNotebookGuid()) {
            continue;
        }

        // Need to update the linked notebook's sync state
        qevercloud::SyncState syncState;
        syncState.setCurrentTime(QDateTime::currentMSecsSinceEpoch());

        syncState.setFullSyncBefore(
            QDateTime::currentDateTime().addMonths(-1).toMSecsSinceEpoch());

        syncState.setUploaded(42);

        syncState.setUpdateCount(
            m_pFakeNoteStore->currentMaxUsn(*pNotebook->linkedNotebookGuid()));

        const auto * pLinkedNotebook = m_pFakeNoteStore->findLinkedNotebook(
            *pNotebook->linkedNotebookGuid());

        QVERIFY(pLinkedNotebook != nullptr);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            pLinkedNotebook->username().value(), syncState);
    }
}

void SynchronizationTester::copyRemoteItemsToLocalStorage()
{
    ErrorString errorDescription;
    bool res = false;

    // ====== Saved searches ======
    auto searches = m_pFakeNoteStore->savedSearches();
    for (const auto it: qevercloud::toRange(::qAsConst(searches))) {
        qevercloud::SavedSearch search(it.value());
        search.setLocallyModified(false);
        search.setLocalOnly(false);

        res =
            m_pLocalStorageManagerAsync->localStorageManager()->addSavedSearch(
                search, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    // ====== Linked notebooks ======
    auto linkedNotebooks = m_pFakeNoteStore->linkedNotebooks();
    for (const auto it: qevercloud::toRange(::qAsConst(linkedNotebooks))) {
        qevercloud::LinkedNotebook linkedNotebook(it.value());
        linkedNotebook.setLocallyModified(false);

        res = m_pLocalStorageManagerAsync->localStorageManager()
                  ->addLinkedNotebook(linkedNotebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    // ====== Tags ======
    auto tags = m_pFakeNoteStore->tags();

    QList<qevercloud::Tag> tagsList;
    tagsList.reserve(tags.size());

    for (const auto it: qevercloud::toRange(::qAsConst(tags))) {
        tagsList << it.value();
    }

    res = sortTagsByParentChildRelations(tagsList, errorDescription);
    QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

    for (const auto & t: ::qAsConst(tagsList)) {
        qevercloud::Tag tag(t);
        tag.setLocallyModified(false);
        tag.setLocalOnly(false);

        const auto * pRemoteTag = m_pFakeNoteStore->findTag(t.guid().value());
        if (pRemoteTag && pRemoteTag->linkedNotebookGuid()) {
            tag.setLinkedNotebookGuid(*pRemoteTag->linkedNotebookGuid());
        }

        res = m_pLocalStorageManagerAsync->localStorageManager()->addTag(
            tag, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    // ====== Notebooks ======
    auto notebooks = m_pFakeNoteStore->notebooks();

    for (const auto it: qevercloud::toRange(::qAsConst(notebooks))) {
        qevercloud::Notebook notebook(it.value());
        notebook.setLocallyModified(false);
        notebook.setLocalOnly(false);

        const auto * pRemoteNotebook =
            m_pFakeNoteStore->findNotebook(it.value().guid().value());

        if (pRemoteNotebook && pRemoteNotebook->linkedNotebookGuid()) {
            notebook.setLinkedNotebookGuid(
                *pRemoteNotebook->linkedNotebookGuid());
        }

        res = m_pLocalStorageManagerAsync->localStorageManager()->addNotebook(
            notebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }

    // ====== Notes ======
    auto notes = m_pFakeNoteStore->notes();
    for (const auto it: qevercloud::toRange(::qAsConst(notes))) {
        qevercloud::Note note(it.value());
        note.setLocallyModified(false);
        note.setLocalOnly(false);

        if (note.resources() && !note.resources()->isEmpty()) {
            auto resources = *note.resources();
            for (auto & resource: resources) {
                const auto * pRemoteResource =
                    m_pFakeNoteStore->findResource(resource.guid().value());

                if (pRemoteResource) {
                    resource = *pRemoteResource;
                }

                resource.setLocallyModified(false);
                resource.setLocalOnly(false);
            }

            note.setResources(resources);
        }

        res = m_pLocalStorageManagerAsync->localStorageManager()->addNote(
            note, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::setRemoteStorageSyncStateToPersistentSyncSettings()
{
    qint32 usersOwnMaxUsn = m_pFakeNoteStore->currentMaxUsn();
    qevercloud::Timestamp timestamp = QDateTime::currentMSecsSinceEpoch();

    ApplicationSettings appSettings(
        m_testAccount, SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup =
        QStringLiteral("Synchronization/www.evernote.com/") +
        QString::number(m_testAccount.id()) + QStringLiteral("/") +
        LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");

    appSettings.setValue(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY, usersOwnMaxUsn);
    appSettings.setValue(keyGroup + LAST_SYNC_TIME_KEY, timestamp);

    auto linkedNotebooks = m_pFakeNoteStore->linkedNotebooks();

    appSettings.beginWriteArray(
        keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS, linkedNotebooks.size());

    int counter = 0;
    for (auto it = linkedNotebooks.constBegin(),
              end = linkedNotebooks.constEnd();
         it != end; ++it, ++counter)
    {
        appSettings.setArrayIndex(counter);
        appSettings.setValue(LINKED_NOTEBOOK_GUID_KEY, it.key());

        qint32 linkedNotebookMaxUsn = m_pFakeNoteStore->currentMaxUsn(it.key());

        appSettings.setValue(
            LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY, linkedNotebookMaxUsn);

        appSettings.setValue(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY, timestamp);

        qevercloud::SyncState syncState;
        syncState.setCurrentTime(timestamp);

        syncState.setFullSyncBefore(
            QDateTime::fromMSecsSinceEpoch(timestamp)
            .addMonths(-1)
            .toMSecsSinceEpoch());

        syncState.setUploaded(42);
        syncState.setUpdateCount(linkedNotebookMaxUsn);

        m_pFakeNoteStore->setLinkedNotebookSyncState(
            it.value().username().value(), syncState);
    }
    appSettings.endArray();
}

void SynchronizationTester::checkProgressNotificationsOrder(
    const SynchronizationManagerSignalsCatcher & catcher)
{
    ErrorString errorDescription;
    if (!catcher.checkSyncChunkDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(
            QString::fromUtf8("Wrong sync chunk download progress order: ") +
            errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkLinkedNotebookSyncChunkDownloadProgressOrder(
            errorDescription))
    {
        QFAIL(qPrintable(
            QString::fromUtf8("Wrong linked notebook sync chunk "
                              "download progress order: ") +
            errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkNoteDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(
            QString::fromUtf8("Wrong note download progress order: ") +
            errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkLinkedNotebookNoteDownloadProgressOrder(errorDescription))
    {
        QFAIL(qPrintable(
            QString::fromUtf8("Wrong linked notebook note download "
                              "progress order: ") +
            errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkResourceDownloadProgressOrder(errorDescription)) {
        QFAIL(qPrintable(
            QString::fromUtf8("Wrong resource download progress order: ") +
            errorDescription.nonLocalizedString()));
    }

    errorDescription.clear();
    if (!catcher.checkLinkedNotebookResourceDownloadProgressOrder(
            errorDescription)) {
        QFAIL(qPrintable(
            QString::fromUtf8("Wrong linked notebook resource "
                              "download progress order: ") +
            errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::checkSyncChunksDataProcessingProgressEmpty(
    const SynchronizationManagerSignalsCatcher & catcher)
{
    ErrorString errorDescription;
    if (!catcher.checkSyncChunksDataProcessingProgressEmpty(errorDescription)) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::checkSyncChunksDataProcessingProgressOrder(
    const SynchronizationManagerSignalsCatcher & catcher)
{
    ErrorString errorDescription;
    if (!catcher.checkSyncChunksDataProcessingProgressOrder(errorDescription)) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(
    const SynchronizationManagerSignalsCatcher & catcher)
{
    ErrorString errorDescription;
    if (!catcher.checkLinkedNotebookSyncChunksDataProcessingProgressEmpty(
            errorDescription))
    {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }
}

void SynchronizationTester::checkLinkedNotebookSyncChunksDataProcessingProgressOrder(
    const SynchronizationManagerSignalsCatcher & catcher)
{
    ErrorString errorDescription;
    if (!catcher.checkLinkedNotebookSyncChunksDataProcessingProgressOrder(
            errorDescription))
    {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
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

    for (const auto & linkedNotebook: ::qAsConst(localLinkedNotebooks)) {
        linkedNotebookGuids << linkedNotebook.guid().value();
    }

    QHash<QString, qevercloud::Tag> localTags;
    QHash<QString, qevercloud::Notebook> localNotebooks;
    QHash<QString, qevercloud::Note> localNotes;

    for (const auto & linkedNotebookGuid: qAsConst(linkedNotebookGuids)) {
        QHash<QString, qevercloud::Tag> currentLocalTags;
        listTagsFromLocalStorage(0, linkedNotebookGuid, currentLocalTags);

        for (const auto it: qevercloud::toRange(::qAsConst(currentLocalTags)))
        {
            localTags[it.key()] = it.value();
        }

        QHash<QString, qevercloud::Notebook> currentLocalNotebooks;

        listNotebooksFromLocalStorage(
            0, linkedNotebookGuid, currentLocalNotebooks);

        for (const auto it:
             qevercloud::toRange(::qAsConst(currentLocalNotebooks))) {
            localNotebooks[it.key()] = it.value();
        }

        QHash<QString, qevercloud::Note> currentLocalNotes;
        listNotesFromLocalStorage(0, linkedNotebookGuid, currentLocalNotes);

        for (const auto it:
             qevercloud::toRange(::qAsConst(currentLocalNotes))) {
            localNotes[it.key()] = it.value();
        }
    }

    // List stuff from remote storage

    auto remoteSavedSearches = m_pFakeNoteStore->savedSearches();
    auto remoteLinkedNotebooks = m_pFakeNoteStore->linkedNotebooks();
    auto remoteTags = m_pFakeNoteStore->tags();
    auto remoteNotebooks = m_pFakeNoteStore->notebooks();
    auto remoteNotes = m_pFakeNoteStore->notes();

    QVERIFY2(
        localSavedSearches.size() == remoteSavedSearches.size(),
        qPrintable(
            QString::fromUtf8("The number of saved searches in local "
                              "and remote storages doesn't match: ") +
            QString::number(localSavedSearches.size()) +
            QString::fromUtf8(" local ones vs ") +
            QString::number(remoteSavedSearches.size()) +
            QString::fromUtf8(" remote ones")));

    for (const auto it: qevercloud::toRange(::qAsConst(localSavedSearches))) {
        auto rit = remoteSavedSearches.find(it.key());

        QVERIFY2(
            rit != remoteSavedSearches.end(),
            qPrintable(
                QString::fromUtf8("Couldn't find one of local saved "
                                  "searches within the remote storage: ") +
                ToString(it.value())));

        qevercloud::SavedSearch localSavedSearch = it.value();
        localSavedSearch.setLocalId(QString{});
        localSavedSearch.setLocalData({});
        localSavedSearch.setLocalOnly(false);
        localSavedSearch.setLocallyFavorited(false);

        qevercloud::SavedSearch remoteSavedSearch = rit.value();
        remoteSavedSearch.setLocalId(QString{});
        remoteSavedSearch.setLocalData({});
        remoteSavedSearch.setLocalOnly(false);
        remoteSavedSearch.setLocallyModified(false);
        remoteSavedSearch.setLocallyFavorited(false);

        QVERIFY2(
            remoteSavedSearch == localSavedSearch,
            qPrintable(
                QString::fromUtf8("Found mismatch between local and "
                                  "remote saved searches: local one: ") +
                ToString(localSavedSearch) + QString::fromUtf8("\nRemote one: ") +
                ToString(remoteSavedSearch)));
    }

    QVERIFY2(
        localLinkedNotebooks.size() == remoteLinkedNotebooks.size(),
        qPrintable(
            QString::fromUtf8("The number of linked notebooks in local "
                              "and remote storages doesn't match: ") +
            QString::number(localLinkedNotebooks.size()) +
            QString::fromUtf8(" local ones vs ") +
            QString::number(remoteLinkedNotebooks.size()) +
            QString::fromUtf8(" remote ones")));

    for (const auto it: qevercloud::toRange(::qAsConst(localLinkedNotebooks)))
    {
        auto rit = remoteLinkedNotebooks.find(it.key());

        QVERIFY2(
            rit != remoteLinkedNotebooks.end(),
            qPrintable(
                QString::fromUtf8("Couldn't find one of local linked "
                                  "notebooks within the remote storage: ") +
                ToString(it.value())));

        qevercloud::LinkedNotebook localLinkedNotebook = it.value();
        localLinkedNotebook.setLocalData({});
        localLinkedNotebook.setLocalOnly(false);
        localLinkedNotebook.setLocallyFavorited(false);

        qevercloud::LinkedNotebook remoteLinkedNotebook = rit.value();
        remoteLinkedNotebook.setLocalData({});
        remoteLinkedNotebook.setLocalOnly(false);
        remoteLinkedNotebook.setLocallyModified(false);
        remoteLinkedNotebook.setLocallyFavorited(false);

        QVERIFY2(
            remoteLinkedNotebook == localLinkedNotebook,
            qPrintable(
                QString::fromUtf8("Found mismatch between local and "
                                  "remote linked notebooks: local one: ") +
                ToString(localLinkedNotebook) + QString::fromUtf8("\nRemote one: ") +
                ToString(remoteLinkedNotebook)));
    }

    if (localTags.size() != remoteTags.size()) {
        QString error;
        QTextStream strm(&error);

        strm
            << "The number of tags in local and remote storages doesn't match: "
            << localTags.size() << " local ones vs " << remoteTags.size()
            << " remote ones\nLocal tags:\n";

        for (const auto it: qevercloud::toRange(::qAsConst(localTags))) {
            strm << it.value() << "\n";
        }

        strm << "\nRemote tags:\n";

        for (const auto it: qevercloud::toRange(::qAsConst(remoteTags))) {
            strm << it.value() << "\n";
        }

        QNWARNING("tests:synchronization", error);
    }

    QVERIFY2(
        localTags.size() == remoteTags.size(),
        qPrintable(
            QString::fromUtf8("The number of tags in local and "
                              "remote storages doesn't match: ") +
            QString::number(localTags.size()) +
            QString::fromUtf8(" local ones vs ") +
            QString::number(remoteTags.size()) +
            QString::fromUtf8(" remote ones")));

    for (const auto it: qevercloud::toRange(::qAsConst(localTags))) {
        auto rit = remoteTags.find(it.key());

        QVERIFY2(
            rit != remoteTags.end(),
            qPrintable(
                QString::fromUtf8("Couldn't find one of local tags "
                                  "within the remote storage: ") +
                ToString(it.value())));

        qevercloud::Tag localTag = it.value();
        localTag.setLocalId(QString{});
        localTag.setLocalData({});
        localTag.setLocalOnly(false);
        localTag.setLocallyFavorited(false);
        localTag.setParentTagLocalId(QString{});

        qevercloud::Tag remoteTag = rit.value();
        remoteTag.setLocalId(QString{});
        remoteTag.setLocalData({});
        remoteTag.setLocalOnly(false);
        remoteTag.setLocallyModified(false);
        remoteTag.setLocallyFavorited(false);
        remoteTag.setParentTagLocalId(QString{});

        QVERIFY2(
            remoteTag == localTag,
            qPrintable(
                QString::fromUtf8("Found mismatch between local and "
                                  "remote tags: local one: ") +
                ToString(localTag) + QString::fromUtf8("\nRemote one: ") +
                ToString(remoteTag)));
    }

    QVERIFY2(
        localNotebooks.size() == remoteNotebooks.size(),
        qPrintable(
            QString::fromUtf8("The number of notebooks in local and "
                              "remote storages doesn't match: ") +
            QString::number(localNotebooks.size()) +
            QString::fromUtf8(" local ones vs ") +
            QString::number(remoteNotebooks.size()) +
            QString::fromUtf8(" remote ones")));

    for (const auto it: qevercloud::toRange(::qAsConst(localNotebooks))) {
        auto rit = remoteNotebooks.find(it.key());

        QVERIFY2(
            rit != remoteNotebooks.end(),
            qPrintable(
                QString::fromUtf8("Couldn't find one of local notebooks "
                                  "within the remote storage: ") +
                ToString(it.value())));

        qevercloud::Notebook localNotebook = it.value();
        localNotebook.setLocalId(QString{});
        localNotebook.setLocalData({});
        localNotebook.setLocalOnly(false);
        localNotebook.setLocallyFavorited(false);

        qevercloud::Notebook remoteNotebook = rit.value();
        remoteNotebook.setLocalId(QString{});
        remoteNotebook.setLocalData({});
        remoteNotebook.setLocalOnly(false);
        remoteNotebook.setLocallyModified(false);
        remoteNotebook.setLocallyFavorited(false);

        QVERIFY2(
            remoteNotebook == localNotebook,
            qPrintable(
                QString::fromUtf8("Found mismatch between local and "
                                  "remote notebooks: local one: ") +
                ToString(localNotebook) + QString::fromUtf8("\nRemote one: ") +
                ToString(remoteNotebook)));
    }

    QVERIFY2(
        localNotes.size() == remoteNotes.size(),
        qPrintable(
            QString::fromUtf8("The number of notes in local and "
                              "remote storages doesn't match: ") +
            QString::number(localNotes.size()) +
            QString::fromUtf8(" local ones vs ") +
            QString::number(remoteNotes.size()) +
            QString::fromUtf8(" remote ones")));

    for (const auto it: qevercloud::toRange(::qAsConst(localNotes))) {
        auto rit = remoteNotes.find(it.key());

        QVERIFY2(
            rit != remoteNotes.end(),
            qPrintable(
                QString::fromUtf8("Couldn't find one of local notes "
                                  "within the remote storage: ") +
                ToString(it.value())));

        qevercloud::Note localNote = it.value();
        localNote.setLocalId(QString{});
        localNote.setLocalData({});
        localNote.setLocalOnly(false);
        localNote.setLocallyFavorited(false);
        localNote.setNotebookLocalId(QString{});
        localNote.setTagLocalIds(QStringList{});

        if (localNote.resources() && !localNote.resources()->isEmpty()) {
            for (auto & resource: *localNote.mutableResources()) {
                resource.setNoteLocalId(QString{});
                resource.setLocalId(QString{});
                resource.setLocalData({});
                resource.setLocalOnly(false);
                resource.setLocallyFavorited(false);
            }
        }

        qevercloud::Note remoteNote = rit.value();
        remoteNote.setLocalId(QString{});
        remoteNote.setLocalData({});
        remoteNote.setLocalOnly(false);
        remoteNote.setLocallyModified(false);
        remoteNote.setLocallyFavorited(false);
        remoteNote.setNotebookLocalId(QString{});
        remoteNote.setTagLocalIds(QStringList{});

        if (remoteNote.resources() && !remoteNote.resources()->isEmpty()) {
            for (auto & resource: *remoteNote.mutableResources()) {
                QString resourceGuid = resource.guid().value();

                const auto * pResource =
                    m_pFakeNoteStore->findResource(resourceGuid);

                QVERIFY2(
                    pResource != nullptr,
                    "One of remote note's resources was not found");

                if (resource.data()) {
                    resource.mutableData()->setBody(*pResource->data()->body());
                }

                if (resource.recognition()) {
                    resource.mutableRecognition()->setBody(
                        *pResource->recognition()->body());
                }

                if (resource.alternateData()) {
                    resource.mutableAlternateData()->setBody(
                        *pResource->alternateData()->body());
                }

                resource.setNoteLocalId(QString{});
                resource.setLocalId(QString{});
                resource.setLocalData({});
                resource.setLocalOnly(false);
                resource.setLocallyModified(false);
                resource.setLocallyFavorited(false);

                resource.setUpdateSequenceNum(*pResource->updateSequenceNum());
            }
        }

        if (remoteNote != localNote) {
            QNWARNING(
                "tests:synchronization",
                "Found mismatch between local and remote notes: local one: "
                    << localNote
                    << "\nRemote one: " << remoteNote);
        }

        QVERIFY2(
            remoteNote == localNote,
            qPrintable(
                QString::fromUtf8("Found mismatch between local and "
                                  "remote notes: local one: ") +
                ToString(localNote) + QString::fromUtf8("\nRemote one: ") +
                ToString(remoteNote)));
    }
}

void SynchronizationTester::checkPersistentSyncState()
{
    ApplicationSettings appSettings(
        m_testAccount, SYNCHRONIZATION_PERSISTENCE_NAME);

    const QString keyGroup =
        QStringLiteral("Synchronization/www.evernote.com/") +
        QString::number(m_testAccount.id()) + QStringLiteral("/") +
        LAST_SYNC_PARAMS_KEY_GROUP + QStringLiteral("/");

    QVariant persistentUserOwnUpdateCountData =
        appSettings.value(keyGroup + LAST_SYNC_UPDATE_COUNT_KEY);

    bool conversionResult = false;

    qint32 persistentUserOwnUpdateCount =
        persistentUserOwnUpdateCountData.toInt(&conversionResult);

    QVERIFY2(
        conversionResult,
        "Failed to convert persistent user's own update count to int");

    qint32 usersOwnMaxUsn = m_pFakeNoteStore->currentMaxUsn();
    if (Q_UNLIKELY(persistentUserOwnUpdateCount != usersOwnMaxUsn)) {
        QString error = QStringLiteral("Persistent user's own update count (") +
            QString::number(persistentUserOwnUpdateCount) +
            QStringLiteral(") is not equal to fake note store's "
                           "user's own max USN (") +
            QString::number(usersOwnMaxUsn) + QStringLiteral(")");

        QFAIL(qPrintable(error));
    }

    qevercloud::Timestamp currentTimestamp =
        QDateTime::currentMSecsSinceEpoch();

    QVariant lastUserOwnDataSyncTimestampData =
        appSettings.value(keyGroup + LAST_SYNC_TIME_KEY);

    conversionResult = false;

    qevercloud::Timestamp lastUserOwnDataSyncTimestamp =
        lastUserOwnDataSyncTimestampData.toLongLong(&conversionResult);

    QVERIFY2(
        conversionResult,
        "Failed to convert persistent user's own last sync timestamp to int64");

    if (Q_UNLIKELY(lastUserOwnDataSyncTimestamp >= currentTimestamp)) {
        QString error = QStringLiteral(
            "Last user's own data sync timestamp is "
            "greater than the current timestamp: ");

        error += printableDateTimeFromTimestamp(lastUserOwnDataSyncTimestamp);
        error += QStringLiteral(" vs ");
        error += printableDateTimeFromTimestamp(currentTimestamp);
        QFAIL(qPrintable(error));
    }

    qint64 timestampSpan = currentTimestamp - lastUserOwnDataSyncTimestamp;

    QVERIFY2(
        timestampSpan < 3 * gTestMaxMilliseconds,
        "The difference between the current datetime and last user's own "
        "data sync timestamp exceeds half an hour");

    auto linkedNotebooks = m_pFakeNoteStore->linkedNotebooks();

    int numLinkedNotebookSyncEntries = appSettings.beginReadArray(
        keyGroup + LAST_SYNC_LINKED_NOTEBOOKS_PARAMS);

    if (Q_UNLIKELY(numLinkedNotebookSyncEntries != linkedNotebooks.size())) {
        QString error = QStringLiteral(
            "The number of persistent linked notebook "
            "sync entries doesn't match the number of "
            "linked notebooks: ");

        error += QString::number(numLinkedNotebookSyncEntries);
        error += QStringLiteral(" vs ");
        error += QString::number(linkedNotebooks.size());
        QFAIL(qPrintable(error));
    }

    for (int i = 0; i < numLinkedNotebookSyncEntries; ++i) {
        appSettings.setArrayIndex(i);

        QString linkedNotebookGuid =
            appSettings.value(LINKED_NOTEBOOK_GUID_KEY).toString();

        auto linkedNotebookIt = linkedNotebooks.find(linkedNotebookGuid);

        QVERIFY2(
            linkedNotebookIt != linkedNotebooks.end(),
            "Found synchronization persistence for unidentified linked "
            "notebook");

        QVariant linkedNotebookUpdateCountData =
            appSettings.value(LINKED_NOTEBOOK_LAST_UPDATE_COUNT_KEY);

        conversionResult = false;

        qint32 linkedNotebookUpdateCount =
            linkedNotebookUpdateCountData.toInt(&conversionResult);

        QVERIFY2(
            conversionResult,
            "Failed to convert linked notebook update count from "
            "synchronization persistence to int");

        qint32 linkedNotebookMaxUsn =
            m_pFakeNoteStore->currentMaxUsn(linkedNotebookGuid);

        if (Q_UNLIKELY(linkedNotebookUpdateCount != linkedNotebookMaxUsn)) {
            QString error = QStringLiteral(
                                "Persistent linked notebook update count (") +
                QString::number(linkedNotebookUpdateCount) +
                QStringLiteral(") is not equal to fake note store's "
                               "max USN for this linked notebook (") +
                QString::number(linkedNotebookMaxUsn) + QStringLiteral(")");

            QFAIL(qPrintable(error));
        }

        QVariant lastLinkedNotebookSyncTimestampData =
            appSettings.value(LINKED_NOTEBOOK_LAST_SYNC_TIME_KEY);

        conversionResult = false;

        qevercloud::Timestamp lastLinkedNotebookSyncTimestamp =
            lastLinkedNotebookSyncTimestampData.toLongLong(&conversionResult);

        QVERIFY2(
            conversionResult,
            "Failed to convert persistent linked notebook last sync "
            "timestamp to int64");

        if (Q_UNLIKELY(lastLinkedNotebookSyncTimestamp >= currentTimestamp)) {
            QString error = QStringLiteral(
                "Last linked notebook sync timestamp "
                "is greater than the current timestamp: ");

            error +=
                printableDateTimeFromTimestamp(lastLinkedNotebookSyncTimestamp);

            error += QStringLiteral(" vs ");
            error += printableDateTimeFromTimestamp(currentTimestamp);
            QFAIL(qPrintable(error));
        }

        timestampSpan = currentTimestamp - lastLinkedNotebookSyncTimestamp;

        QVERIFY2(
            timestampSpan < 3 * gTestMaxMilliseconds,
            "The difference between the current datetime and last linked "
            "notebook sync timestamp exceeds half an hour");
    }
    appSettings.endArray();
}

void SynchronizationTester::checkExpectedNamesOfConflictingItemsAfterSync()
{
    ErrorString errorDescription;
    bool res = false;
    bool onceChecked = false;

    for (const auto it:
         qevercloud::toRange(qAsConst(m_expectedSavedSearchNamesByGuid)))
    {
        qevercloud::SavedSearch search;
        search.setLocalId(QString());
        search.setGuid(it.key());

        res =
            m_pLocalStorageManagerAsync->localStorageManager()->findSavedSearch(
                search, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        if (Q_UNLIKELY(search.name() != it.value())) {
            errorDescription.setBase(
                QStringLiteral("Found mismatch between saved "
                               "search's actual name and its "
                               "expected name after the sync"));

            errorDescription.details() += QStringLiteral("Expected name: ");
            errorDescription.details() += it.value();
            errorDescription.details() += QStringLiteral(", actual name: ");
            errorDescription.details() += search.name().value();
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }

        onceChecked = true;
    }

    for (const auto it:
         qevercloud::toRange(qAsConst(m_expectedTagNamesByGuid))) {
        qevercloud::Tag tag;
        tag.setLocalId(QString());
        tag.setGuid(it.key());

        res = m_pLocalStorageManagerAsync->localStorageManager()->findTag(
            tag, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        if (Q_UNLIKELY(tag.name() != it.value())) {
            errorDescription.setBase(
                QStringLiteral("Found mismatch between tag's actual name and "
                               "its expected name after the sync"));

            errorDescription.details() += QStringLiteral("Expected name: ");
            errorDescription.details() += it.value();
            errorDescription.details() += QStringLiteral(", actual name: ");
            errorDescription.details() += tag.name().value();
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }

        onceChecked = true;
    }

    for (const auto it:
         qevercloud::toRange(qAsConst(m_expectedNotebookNamesByGuid)))
    {
        qevercloud::Notebook notebook;
        notebook.setLocalId(QString());
        notebook.setGuid(it.key());

        res = m_pLocalStorageManagerAsync->localStorageManager()->findNotebook(
            notebook, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        if (Q_UNLIKELY(notebook.name() != it.value())) {
            errorDescription.setBase(
                QStringLiteral("Found mismatch between notebook's actual name "
                               "and its expected name after the sync"));

            errorDescription.details() += QStringLiteral("Expected name: ");
            errorDescription.details() += it.value();
            errorDescription.details() += QStringLiteral(", actual name: ");
            errorDescription.details() += notebook.name().value();
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }

        onceChecked = true;
    }

    auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    for (const auto it:
         qevercloud::toRange(qAsConst(m_expectedNoteTitlesByGuid))) {
        qevercloud::Note note;
        note.setLocalId(QString());
        note.setGuid(it.key());

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata);

        res = pLocalStorageManager->findNote(note, options, errorDescription);
        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));

        if (Q_UNLIKELY(note.title() != it.value())) {
            errorDescription.setBase(
                QStringLiteral("Found mismatch between note's actual title and "
                               "its expected title after the sync"));

            errorDescription.details() += QStringLiteral("Expected title: ");
            errorDescription.details() += it.value();
            errorDescription.details() += QStringLiteral(", actual title: ");
            errorDescription.details() += note.title().value();
            QFAIL(qPrintable(errorDescription.nonLocalizedString()));
        }

        onceChecked = true;
    }

    QVERIFY2(onceChecked, "Found no expected item names to verify");
}

void SynchronizationTester::checkLocalCopiesOfConflictingNotesWereCreated()
{
    auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    QVERIFY(!m_expectedNoteTitlesByGuid.isEmpty());

    for (const auto it:
         qevercloud::toRange(qAsConst(m_expectedNoteTitlesByGuid))) {
        auto remoteConflictingNotes =
            m_pFakeNoteStore->getNotesByConflictSourceNoteGuid(it.key());

        QVERIFY(remoteConflictingNotes.size() == 1);

        const auto & remoteConfictingNote = remoteConflictingNotes.at(0);

        qevercloud::Note localConflictingNote;
        localConflictingNote.setLocalId(QString());
        localConflictingNote.setGuid(remoteConfictingNote.guid());

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata);

        ErrorString errorDescription;

        bool res = pLocalStorageManager->findNote(
            localConflictingNote, options, errorDescription);

        QVERIFY2(res, qPrintable(errorDescription.nonLocalizedString()));
        QVERIFY(localConflictingNote.title());

        QVERIFY(localConflictingNote.title()->endsWith(
            gModifiedLocallySuffix + QStringLiteral(" - conflicting")));
    }
}

void SynchronizationTester::checkNoConflictingNotesWereCreated()
{
    QVERIFY(!m_expectedNoteTitlesByGuid.isEmpty());

    for (const auto it:
         qevercloud::toRange(qAsConst(m_expectedNoteTitlesByGuid))) {
        auto remoteConflictingNotes =
            m_pFakeNoteStore->getNotesByConflictSourceNoteGuid(it.key());

        QVERIFY(remoteConflictingNotes.isEmpty());
    }
}

void SynchronizationTester::checkSyncStatePersistedRightAfterAPIRateLimitBreach(
    const SynchronizationManagerSignalsCatcher & catcher,
    const int numExpectedSyncStateEntries,
    const int rateLimitTriggeredSyncStateEntryIndex)
{
    const auto & syncStateUpdateCounts =
        catcher.persistedSyncStateUpdateCounts();

    if (Q_UNLIKELY(syncStateUpdateCounts.size() != numExpectedSyncStateEntries))
    {
        QString error = QStringLiteral("Expected to have ");
        error += QString::number(numExpectedSyncStateEntries);
        error +=
            QStringLiteral(" events of sync state persisting. Instead got ");
        error += QString::number(syncStateUpdateCounts.size());
        error += QStringLiteral(" sync state persisting events");
        QFAIL(qPrintable(error));
        return;
    }

    if (rateLimitTriggeredSyncStateEntryIndex < 0) {
        // No need to check any particular sync state
        return;
    }

    if (Q_UNLIKELY(
            rateLimitTriggeredSyncStateEntryIndex >=
            numExpectedSyncStateEntries))
    {
        QFAIL(
            "The index of sync state persisting event is larger than or equal "
            "to the number of expected sync state entries");
        return;
    }

    // The update counts we are interested in here are those corresponding to
    // API rate limit breach, these must be the first one within this
    // two-items vector
    const auto & syncStateUpdateCount =
        syncStateUpdateCounts[rateLimitTriggeredSyncStateEntryIndex];

    qint32 referenceUserOwnUpdateCountBeforeAPILimitBreach =
        m_pFakeNoteStore
            ->smallestUsnOfNotCompletelySentDataItemBeforeRateLimitBreach();

    if (referenceUserOwnUpdateCountBeforeAPILimitBreach < 0) {
        referenceUserOwnUpdateCountBeforeAPILimitBreach =
            m_pFakeNoteStore->maxUsnBeforeAPIRateLimitsExceeding();

        QVERIFY2(
            referenceUserOwnUpdateCountBeforeAPILimitBreach >= 0,
            "FakeNoteStore returned negative smallest USN before API rate "
            "limit breach and negative max USN before API rate limit breach "
            "for user's own data");

        ++referenceUserOwnUpdateCountBeforeAPILimitBreach;
    }

    if (Q_UNLIKELY(
            referenceUserOwnUpdateCountBeforeAPILimitBreach !=
            (syncStateUpdateCount.m_userOwnUpdateCount + 1)))
    {
        QString error = QStringLiteral(
            "Reference update count before API rate "
            "limit breach (");

        error +=
            QString::number(referenceUserOwnUpdateCountBeforeAPILimitBreach);

        error += QStringLiteral(
            ") is not equal to the one present within "
            "the actual sync state (");

        error += QString::number(syncStateUpdateCount.m_userOwnUpdateCount);
        error += QStringLiteral(") + 1");
        QFAIL(qPrintable(error));
        printContentsOfLocalStorageAndFakeNoteStoreToWarnLog(error);
        return;
    }

    QVERIFY(!syncStateUpdateCount
                 .m_linkedNotebookUpdateCountsByLinkedNotebookGuid.isEmpty());

    for (const auto it: qevercloud::toRange(
             qAsConst(syncStateUpdateCount
                          .m_linkedNotebookUpdateCountsByLinkedNotebookGuid)))
    {
        qint32 referenceUsn =
            m_pFakeNoteStore
                ->smallestUsnOfNotCompletelySentDataItemBeforeRateLimitBreach(
                    it.key());

        if (referenceUsn < 0) {
            referenceUsn =
                m_pFakeNoteStore->maxUsnBeforeAPIRateLimitsExceeding(it.key());

            QVERIFY2(
                referenceUsn >= 0,
                "FakeNoteStore returned negative smallest USN "
                "before API rate limit breach and negative max USN before "
                "API rate limit breach for one of linked notebooks");

            ++referenceUsn;
        }

        if (Q_UNLIKELY(referenceUsn != (it.value() + 1))) {
            QString error = QStringLiteral(
                "Reference update count before API "
                "rate limit breach (");

            error += QString::number(referenceUsn);

            error += QStringLiteral(
                ") is not equal to the one present within "
                "the actual sync state (");

            error += QString::number(it.value());
            error += QStringLiteral(") + 1 for linked notebook with guid ");
            error += it.key();
            QFAIL(qPrintable(error));

            printContentsOfLocalStorageAndFakeNoteStoreToWarnLog(
                error, it.key());
            return;
        }
    }
}

void SynchronizationTester::listSavedSearchesFromLocalStorage(
    const qint32 afterUSN,
    QHash<QString, qevercloud::SavedSearch> & savedSearches) const
{
    savedSearches.clear();

    const auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    ErrorString errorDescription;

    auto searches = pLocalStorageManager->listSavedSearches(
        LocalStorageManager::ListObjectsOption::ListAll, errorDescription);

    if (searches.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    savedSearches.reserve(searches.size());
    for (const auto & search: qAsConst(searches)) {
        if (Q_UNLIKELY(!search.guid())) {
            continue;
        }

        if ((afterUSN > 0) &&
            (!search.updateSequenceNum() ||
             (*search.updateSequenceNum() <= afterUSN)))
        {
            continue;
        }

        savedSearches[search.guid().value()] = search;
    }
}

void SynchronizationTester::listTagsFromLocalStorage(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Tag> & tags) const
{
    tags.clear();

    const auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    QString localLinkedNotebookGuid = QLatin1String("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    ErrorString errorDescription;

    auto localTags = pLocalStorageManager->listTags(
        LocalStorageManager::ListObjectsOption::ListAll, errorDescription,
        /* limit = */ 0,
        /* offset = */ 0, LocalStorageManager::ListTagsOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending,
        localLinkedNotebookGuid);

    if (localTags.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    tags.reserve(localTags.size());

    for (const auto & tag: qAsConst(localTags)) {
        if (Q_UNLIKELY(!tag.guid())) {
            continue;
        }

        if ((afterUSN > 0) &&
            (!tag.updateSequenceNum() ||
             (*tag.updateSequenceNum() <= afterUSN)))
        {
            continue;
        }

        tags[tag.guid().value()] = tag;
    }
}

void SynchronizationTester::listNotebooksFromLocalStorage(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Notebook> & notebooks) const
{
    notebooks.clear();

    const auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    QString localLinkedNotebookGuid = QLatin1String("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    ErrorString errorDescription;

    auto localNotebooks = pLocalStorageManager->listNotebooks(
        LocalStorageManager::ListObjectsOption::ListAll, errorDescription,
        /* limit = */ 0,
        /* offset = */ 0, LocalStorageManager::ListNotebooksOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending,
        localLinkedNotebookGuid);

    if (localNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    notebooks.reserve(localNotebooks.size());

    for (const auto & notebook: qAsConst(localNotebooks)) {
        if (Q_UNLIKELY(!notebook.guid())) {
            continue;
        }

        if ((afterUSN > 0) &&
            (!notebook.updateSequenceNum() ||
             (*notebook.updateSequenceNum() <= afterUSN)))
        {
            continue;
        }

        notebooks[notebook.guid().value()] = notebook;
    }
}

void SynchronizationTester::listNotesFromLocalStorage(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Note> & notes) const
{
    notes.clear();

    const auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    QString localLinkedNotebookGuid = QLatin1String("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    LocalStorageManager::GetNoteOptions options(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    ErrorString errorDescription;

    auto localNotes = pLocalStorageManager->listNotes(
        LocalStorageManager::ListObjectsOption::ListAll, options,
        errorDescription,
        /* limit = */ 0,
        /* offset = */ 0, LocalStorageManager::ListNotesOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending,
        localLinkedNotebookGuid);

    if (localNotes.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    notes.reserve(localNotes.size());

    for (const auto & note: qAsConst(localNotes)) {
        if (Q_UNLIKELY(!note.guid())) {
            continue;
        }

        if ((afterUSN > 0) &&
            (!note.updateSequenceNum() ||
             (*note.updateSequenceNum() <= afterUSN)))
        {
            continue;
        }

        notes[note.guid().value()] = note;
    }
}

void SynchronizationTester::listResourcesFromLocalStorage(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Resource> & resources) const
{
    resources.clear();

    const auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    QString localLinkedNotebookGuid = QLatin1String("");
    if (!linkedNotebookGuid.isEmpty()) {
        localLinkedNotebookGuid = linkedNotebookGuid;
    }

    LocalStorageManager::GetNoteOptions options(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    ErrorString errorDescription;

    auto localNotes = pLocalStorageManager->listNotes(
        LocalStorageManager::ListObjectsOption::ListAll, options,
        errorDescription,
        /* limit = */ 0,
        /* offset = */ 0, LocalStorageManager::ListNotesOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending,
        localLinkedNotebookGuid);

    if (localNotes.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    resources.reserve(localNotes.size());

    for (const auto & note: qAsConst(localNotes)) {
        if (!note.resources() || note.resources()->isEmpty()) {
            continue;
        }

        auto localResources = *note.resources();
        for (const auto & localResource: qAsConst(localResources)) {
            if (Q_UNLIKELY(!localResource.guid())) {
                continue;
            }

            if ((afterUSN > 0) &&
                (!localResource.updateSequenceNum() ||
                 (*localResource.updateSequenceNum() <= afterUSN)))
            {
                continue;
            }

            resources[localResource.guid().value()] = localResource;
        }
    }
}

void SynchronizationTester::listLinkedNotebooksFromLocalStorage(
    const qint32 afterUSN,
    QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks) const
{
    linkedNotebooks.clear();

    const auto * pLocalStorageManager =
        m_pLocalStorageManagerAsync->localStorageManager();

    if (Q_UNLIKELY(!pLocalStorageManager)) {
        QFAIL("Local storage manager is null");
    }

    ErrorString errorDescription;

    auto localLinkedNotebooks = pLocalStorageManager->listLinkedNotebooks(
        LocalStorageManager::ListObjectsOption::ListAll, errorDescription,
        /* limit = */ 0,
        /* offset = */ 0,
        LocalStorageManager::ListLinkedNotebooksOrder::NoOrder,
        LocalStorageManager::OrderDirection::Ascending);

    if (localLinkedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
        QFAIL(qPrintable(errorDescription.nonLocalizedString()));
    }

    linkedNotebooks.reserve(localLinkedNotebooks.size());

    for (const auto & linkedNotebook: qAsConst(localLinkedNotebooks)) {
        if (Q_UNLIKELY(!linkedNotebook.guid())) {
            continue;
        }

        if ((afterUSN > 0) &&
            (!linkedNotebook.updateSequenceNum() ||
             (*linkedNotebook.updateSequenceNum() <= afterUSN)))
        {
            continue;
        }

        linkedNotebooks[linkedNotebook.guid().value()] = linkedNotebook;
    }
}

void SynchronizationTester::listSavedSearchesFromFakeNoteStore(
    const qint32 afterUSN,
    QHash<QString, qevercloud::SavedSearch> & savedSearches) const
{
    savedSearches = m_pFakeNoteStore->savedSearches();
    if (afterUSN <= 0) {
        return;
    }

    for (auto it = savedSearches.begin(); it != savedSearches.end();) {
        const auto & search = it.value();

        if (!search.updateSequenceNum() ||
            (*search.updateSequenceNum() <= afterUSN))
        {
            it = savedSearches.erase(it);
            continue;
        }

        ++it;
    }
}

void SynchronizationTester::listTagsFromFakeNoteStore(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Tag> & tags) const
{
    tags = m_pFakeNoteStore->tags();

    for (auto it = tags.begin(); it != tags.end();) {
        const auto & tag = it.value();

        if ((afterUSN > 0) &&
            (!tag.updateSequenceNum() ||
             (*tag.updateSequenceNum() <= afterUSN)))
        {
            it = tags.erase(it);
            continue;
        }

        const auto * pTag = m_pFakeNoteStore->findTag(it.key());
        if (Q_UNLIKELY(!pTag)) {
            it = tags.erase(it);
            continue;
        }

        if ((linkedNotebookGuid.isEmpty() && pTag->linkedNotebookGuid()) ||
            (!linkedNotebookGuid.isEmpty() &&
             (!pTag->linkedNotebookGuid() ||
              (*pTag->linkedNotebookGuid() != linkedNotebookGuid))))
        {
            it = tags.erase(it);
            continue;
        }

        ++it;
    }
}

void SynchronizationTester::listNotebooksFromFakeNoteStore(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Notebook> & notebooks) const
{
    notebooks.clear();

    auto notebookPtrs = m_pFakeNoteStore->findNotebooksForLinkedNotebookGuid(
        linkedNotebookGuid);

    notebooks.reserve(notebookPtrs.size());

    for (const auto * pNotebook: qAsConst(notebookPtrs)) {
        if (Q_UNLIKELY(!pNotebook->guid())) {
            continue;
        }

        if ((afterUSN > 0) &&
            (!pNotebook->updateSequenceNum() ||
             (*pNotebook->updateSequenceNum() <= afterUSN)))
        {
            continue;
        }

        notebooks[pNotebook->guid().value()] = *pNotebook;
    }
}

void SynchronizationTester::listNotesFromFakeNoteStore(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Note> & notes) const
{
    notes = m_pFakeNoteStore->notes();

    for (auto it = notes.begin(); it != notes.end();) {
        const auto & note = it.value();

        if (Q_UNLIKELY(!note.notebookGuid())) {
            it = notes.erase(it);
            continue;
        }

        if ((afterUSN > 0) &&
            (!note.updateSequenceNum() ||
             (*note.updateSequenceNum() <= afterUSN)))
        {
            it = notes.erase(it);
            continue;
        }

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(note.notebookGuid().value());

        if (Q_UNLIKELY(!pNotebook)) {
            it = notes.erase(it);
            continue;
        }

        if ((linkedNotebookGuid.isEmpty() &&
             pNotebook->linkedNotebookGuid()) ||
            (!linkedNotebookGuid.isEmpty() &&
             (!pNotebook->linkedNotebookGuid() ||
              (*pNotebook->linkedNotebookGuid() != linkedNotebookGuid))))
        {
            it = notes.erase(it);
            continue;
        }

        ++it;
    }
}

void SynchronizationTester::listResourcesFromFakeNoteStore(
    const qint32 afterUSN, const QString & linkedNotebookGuid,
    QHash<QString, qevercloud::Resource> & resources) const
{
    resources = m_pFakeNoteStore->resources();

    for (auto it = resources.begin(); it != resources.end();) {
        const auto & resource = it.value();

        if (Q_UNLIKELY(!resource.noteGuid())) {
            it = resources.erase(it);
            continue;
        }

        if ((afterUSN > 0) &&
            (!resource.updateSequenceNum() ||
             (*resource.updateSequenceNum() <= afterUSN)))
        {
            it = resources.erase(it);
            continue;
        }

        const auto * pNote =
            m_pFakeNoteStore->findNote(*resource.noteGuid());

        if (Q_UNLIKELY(!pNote || !pNote->notebookGuid())) {
            it = resources.erase(it);
            continue;
        }

        const auto * pNotebook =
            m_pFakeNoteStore->findNotebook(*pNote->notebookGuid());

        if (Q_UNLIKELY(!pNotebook)) {
            it = resources.erase(it);
            continue;
        }

        if ((linkedNotebookGuid.isEmpty() &&
             pNotebook->linkedNotebookGuid()) ||
            (!linkedNotebookGuid.isEmpty() &&
             (!pNotebook->linkedNotebookGuid() ||
              (*pNotebook->linkedNotebookGuid() != linkedNotebookGuid))))
        {
            it = resources.erase(it);
            continue;
        }

        ++it;
    }
}

void SynchronizationTester::listLinkedNotebooksFromFakeNoteStore(
    const qint32 afterUSN,
    QHash<QString, qevercloud::LinkedNotebook> & linkedNotebooks) const
{
    linkedNotebooks = m_pFakeNoteStore->linkedNotebooks();
    if (afterUSN <= 0) {
        return;
    }

    for (auto it = linkedNotebooks.begin(); it != linkedNotebooks.end();) {
        const auto & linkedNotebook = it.value();

        if (!linkedNotebook.updateSequenceNum() &&
            (*linkedNotebook.updateSequenceNum() <= afterUSN))
        {
            it = linkedNotebooks.erase(it);
            continue;
        }
    }
}

void SynchronizationTester::
    printContentsOfLocalStorageAndFakeNoteStoreToWarnLog(
        const QString & prefix, const QString & linkedNotebookGuid)
{
    QString message;

    if (!prefix.isEmpty()) {
        message += prefix;
    }

    QHash<QString, qevercloud::SavedSearch> localSavedSearches;
    QHash<QString, qevercloud::Tag> localTags;
    QHash<QString, qevercloud::Notebook> localNotebooks;
    QHash<QString, qevercloud::Note> localNotes;
    QHash<QString, qevercloud::Resource> localResources;
    QHash<QString, qevercloud::LinkedNotebook> localLinkedNotebooks;

    QHash<QString, qevercloud::SavedSearch> remoteSavedSearches;
    QHash<QString, qevercloud::Tag> remoteTags;
    QHash<QString, qevercloud::Notebook> remoteNotebooks;
    QHash<QString, qevercloud::Note> remoteNotes;
    QHash<QString, qevercloud::Resource> remoteResources;
    QHash<QString, qevercloud::LinkedNotebook> remoteLinkedNotebooks;

    if (linkedNotebookGuid.isEmpty()) {
        listSavedSearchesFromLocalStorage(0, localSavedSearches);
        listSavedSearchesFromFakeNoteStore(0, remoteSavedSearches);
        listLinkedNotebooksFromLocalStorage(0, localLinkedNotebooks);
        listLinkedNotebooksFromFakeNoteStore(0, remoteLinkedNotebooks);
    }

    listTagsFromLocalStorage(0, linkedNotebookGuid, localTags);
    listTagsFromFakeNoteStore(0, linkedNotebookGuid, remoteTags);

    listNotebooksFromLocalStorage(0, linkedNotebookGuid, localNotebooks);
    listNotebooksFromFakeNoteStore(0, linkedNotebookGuid, remoteNotebooks);

    listNotesFromLocalStorage(0, linkedNotebookGuid, localNotes);
    listNotesFromLocalStorage(0, linkedNotebookGuid, remoteNotes);

    listResourcesFromLocalStorage(0, linkedNotebookGuid, localResources);
    listResourcesFromFakeNoteStore(0, linkedNotebookGuid, remoteResources);

#define PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(container)                        \
    for (const auto it: qevercloud::toRange(::qAsConst(container))) {          \
        message += QStringLiteral("    guid = ") % it.key() %                  \
                QStringLiteral(", USN = ") +                                   \
            (it.value().updateSequenceNum()                                    \
                 ? QString::number(*it.value().updateSequenceNum())            \
                 : QStringLiteral("<not set>")) +                              \
            QStringLiteral("\n");                                              \
    }

    if (linkedNotebookGuid.isEmpty()) {
        message += QStringLiteral("\nLocal saved searches:\n");
        PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(localSavedSearches)
    }

    message += QStringLiteral("\nLocal tags:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(localTags)

    message += QStringLiteral("\nLocal notebooks:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(localNotebooks)

    message += QStringLiteral("\nLocal notes:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(localNotes)

    message += QStringLiteral("\nLocal resources:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(localResources)

    if (linkedNotebookGuid.isEmpty()) {
        message += QStringLiteral("\nLocal linked notebooks:\n");
        PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(localLinkedNotebooks);
    }

    message += QStringLiteral("\n\n");

    if (linkedNotebookGuid.isEmpty()) {
        message += QStringLiteral("Remote saved searches:\n");
        PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(remoteSavedSearches)
    }

    message += QStringLiteral("\nRemote tags:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(remoteTags)

    message += QStringLiteral("\nRemote notebooks:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(remoteNotebooks)

    message += QStringLiteral("\nRemote notes:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(remoteNotes)

    message += QStringLiteral("\nRemote resources:\n");
    PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(remoteResources)

    if (linkedNotebookGuid.isEmpty()) {
        message += QStringLiteral("\nRemote linked notebooks:\n");
        PRINT_CONTAINER_ITEMS_GUIDS_AND_USNS(remoteLinkedNotebooks);
    }

    QNWARNING("tests:synchronization", message);
}

void SynchronizationTester::runTest(
    SynchronizationManagerSignalsCatcher & catcher)
{
    auto status = EventLoopWithExitStatus::ExitStatus::Failure;
    {
        QTimer timer;
        timer.setInterval(gTestMaxMilliseconds);
        timer.setSingleShot(true);

        EventLoopWithExitStatus loop;

        QObject::connect(
            &timer, &QTimer::timeout, &loop,
            &EventLoopWithExitStatus::exitAsTimeout);

        QObject::connect(
            &catcher, &SynchronizationManagerSignalsCatcher::ready, &loop,
            &EventLoopWithExitStatus::exitAsSuccess);

        timer.start();
        QTimer::singleShot(
            0, m_pSynchronizationManager, &SynchronizationManager::synchronize);

        Q_UNUSED(loop.exec())
        status = loop.exitStatus();
    }

    if (status == EventLoopWithExitStatus::ExitStatus::Timeout) {
        QFAIL("Synchronization test failed to finish in time");
    }
    else if (status != EventLoopWithExitStatus::ExitStatus::Success) {
        QFAIL(
            "Internal error: incorrect return status from synchronization "
            "test");
    }

    if (catcher.receivedFailedSignal()) {
        QFAIL(qPrintable(
            QString::fromUtf8("Detected failure during the asynchronous "
                              "synchronization loop: ") +
            catcher.failureErrorDescription().nonLocalizedString()));
    }
}

} // namespace quentier::test
