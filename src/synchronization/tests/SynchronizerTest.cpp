/*
 * Copyright 2023 Dmitry Ivanov
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

#include "Utils.h"

#include <synchronization/Synchronizer.h>

#include <synchronization/tests/mocks/MockIAccountSynchronizer.h>
#include <synchronization/tests/mocks/MockIAccountSynchronizerFactory.h>
#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockIProtocolVersionChecker.h>
#include <synchronization/types/AuthenticationInfo.h>
#include <synchronization/types/SyncOptionsBuilder.h>
#include <synchronization/types/SyncResult.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/synchronization/ISyncEventsNotifier.h>
#include <quentier/synchronization/tests/mocks/MockISyncConflictResolver.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <QCoreApplication>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QPromise>
#else
#include <quentier/threading/Qt5Promise.h>
#endif

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression
// clazy:excludeall=lambda-in-connect

namespace quentier::synchronization::tests {

using testing::_;
using testing::Return;
using testing::StrictMock;

class SynchronizerTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockIAccountSynchronizer>
        m_mockAccountSynchronizer =
            std::make_shared<StrictMock<mocks::MockIAccountSynchronizer>>();

    const std::shared_ptr<mocks::MockIAccountSynchronizerFactory>
        m_mockAccountSynchronizerFactory = std::make_shared<
            StrictMock<mocks::MockIAccountSynchronizerFactory>>();

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const std::shared_ptr<mocks::MockIProtocolVersionChecker>
        m_mockProtocolVersionChecker =
            std::make_shared<StrictMock<mocks::MockIProtocolVersionChecker>>();

    const std::shared_ptr<mocks::MockISyncConflictResolver>
        m_mockSyncConflictResolver =
            std::make_shared<StrictMock<mocks::MockISyncConflictResolver>>();

    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

    const utility::cancelers::ManualCancelerPtr m_canceler =
        std::make_shared<utility::cancelers::ManualCanceler>();
};

TEST_F(SynchronizerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto synchronizer = std::make_shared<Synchronizer>(
            m_mockAccountSynchronizerFactory, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker));
}

TEST_F(SynchronizerTest, CtorNullAccountSynchronizerFactory)
{
    EXPECT_THROW(
        const auto synchronizer = std::make_shared<Synchronizer>(
            nullptr, m_mockAuthenticationInfoProvider,
            m_mockProtocolVersionChecker),
        InvalidArgument);
}

TEST_F(SynchronizerTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto synchronizer = std::make_shared<Synchronizer>(
            m_mockAccountSynchronizerFactory, nullptr,
            m_mockProtocolVersionChecker),
        InvalidArgument);
}

TEST_F(SynchronizerTest, CtorNullProtocolVersionChecker)
{
    EXPECT_THROW(
        const auto synchronizer = std::make_shared<Synchronizer>(
            m_mockAccountSynchronizerFactory, m_mockAuthenticationInfoProvider,
            nullptr),
        InvalidArgument);
}

TEST_F(SynchronizerTest, AuthenticateNewAccount)
{
    const auto synchronizer = std::make_shared<Synchronizer>(
        m_mockAccountSynchronizerFactory, m_mockAuthenticationInfoProvider,
        m_mockProtocolVersionChecker);

    const auto authenticationInfo = std::make_shared<AuthenticationInfo>();

    EXPECT_CALL(*m_mockAuthenticationInfoProvider, authenticateNewAccount)
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticationInfo)));

    auto future = synchronizer->authenticateNewAccount();
    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);
    EXPECT_EQ(future.result(), authenticationInfo);
}

TEST_F(SynchronizerTest, AuthenticateAccount)
{
    const auto synchronizer = std::make_shared<Synchronizer>(
        m_mockAccountSynchronizerFactory, m_mockAuthenticationInfoProvider,
        m_mockProtocolVersionChecker);

    const auto authenticationInfo = std::make_shared<AuthenticationInfo>();

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticationInfo)));

    auto future = synchronizer->authenticateAccount(m_account);
    waitForFuture(future);
    ASSERT_EQ(future.resultCount(), 1);
    EXPECT_EQ(future.result(), authenticationInfo);
}

TEST_F(SynchronizerTest, RevokeAuthentication)
{
    const auto synchronizer = std::make_shared<Synchronizer>(
        m_mockAccountSynchronizerFactory, m_mockAuthenticationInfoProvider,
        m_mockProtocolVersionChecker);

    const qevercloud::UserID userId = 42;

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        clearCaches(IAuthenticationInfoProvider::ClearCacheOptions{
            IAuthenticationInfoProvider::ClearCacheOption::User{userId}}));

    synchronizer->revokeAuthentication(userId);
}

TEST_F(SynchronizerTest, SynchronizeAccount)
{
    const auto synchronizer = std::make_shared<Synchronizer>(
        m_mockAccountSynchronizerFactory, m_mockAuthenticationInfoProvider,
        m_mockProtocolVersionChecker);

    bool onSyncChunksDownloadProgressCalled = false;
    bool onSyncChunksDownloadedCalled = false;
    bool onSyncChunksDataProcessingProgressCalled = false;
    bool onStartLinkedNotebooksDataDownloadingCalled = false;
    bool onLinkedNotebookSyncChunksDownloadProgressCalled = false;
    bool onLinkedNotebookSyncChunksDownloadedCalled = false;
    bool onLinkedNotebookSyncChunksDataProcessingProgressCalled = false;
    bool onNotesDownloadProgressCalled = false;
    bool onLinkedNotebookNotesDownloadProgressCalled = false;
    bool onResourcesDownloadProgressCalled = false;
    bool onLinkedNotebookResourcesDownloadProgressCalled = false;
    bool onUserOwnSendStatusUpdateCalled = false;
    bool onLinkedNotebookSendStatusUpdateCalled = false;

    auto promise = std::make_shared<QPromise<ISyncResultPtr>>();
    promise->start();
    auto future = promise->future();

    const auto syncOptions = SyncOptionsBuilder{}.build();
    const auto authenticationInfo = std::make_shared<AuthenticationInfo>();

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(threading::makeReadyFuture<IAuthenticationInfoPtr>(
            authenticationInfo)));

    EXPECT_CALL(*m_mockProtocolVersionChecker, checkProtocolVersion)
        .WillOnce(Return(threading::makeReadyFuture()));

    EXPECT_CALL(
        *m_mockAccountSynchronizerFactory,
        createAccountSynchronizer(m_account, _, _, syncOptions))
        .WillOnce(Return(m_mockAccountSynchronizer));

    std::shared_ptr<IAccountSynchronizer::ICallback> callback;

    EXPECT_CALL(*m_mockAccountSynchronizer, synchronize)
        .WillOnce(
            [&](const IAccountSynchronizer::ICallbackWeakPtr & callbackWeak,
                [[maybe_unused]] const utility::cancelers::ICancelerPtr &
                    canceler) {
                callback = callbackWeak.lock();
                return future;
            });

    auto result = synchronizer->synchronizeAccount(
        m_account, m_mockLocalStorage, m_canceler, syncOptions,
        m_mockSyncConflictResolver);

    const auto * notifier = result.second;

    QObject::connect(
        notifier, &ISyncEventsNotifier::syncChunksDownloadProgress, notifier,
        [&]([[maybe_unused]] const qint32 highestDownloadedUsn,
            [[maybe_unused]] const qint32 highestServerUsn,
            [[maybe_unused]] const qint32 lastPreviousUsn) {
            onSyncChunksDownloadProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::syncChunksDownloaded, notifier,
        [&] { onSyncChunksDownloadedCalled = true; });

    QObject::connect(
        notifier, &ISyncEventsNotifier::syncChunksDataProcessingProgress,
        notifier,
        [&]([[maybe_unused]] const ISyncChunksDataCountersPtr & counters) {
            onSyncChunksDataProcessingProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::startLinkedNotebooksDataDownloading,
        notifier,
        [&]([[maybe_unused]] const QList<qevercloud::LinkedNotebook> &
                linkedNotebooks) {
            onStartLinkedNotebooksDataDownloadingCalled = true;
        });

    QObject::connect(
        notifier,
        &ISyncEventsNotifier::linkedNotebookSyncChunksDownloadProgress,
        notifier,
        [&]([[maybe_unused]] qint32 highestDownloadedUsn,
            [[maybe_unused]] qint32 highestServerUsn,
            [[maybe_unused]] qint32 lastPreviousUsn,
            [[maybe_unused]] const qevercloud::LinkedNotebook &
                linkedNotebook) {
            onLinkedNotebookSyncChunksDownloadProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookSyncChunksDownloaded,
        notifier,
        [&]([[maybe_unused]] const qevercloud::LinkedNotebook &
                linkedNotebook) {
            onLinkedNotebookSyncChunksDownloadedCalled = true;
        });

    QObject::connect(
        notifier,
        &ISyncEventsNotifier::linkedNotebookSyncChunksDataProcessingProgress,
        notifier,
        [&]([[maybe_unused]] const ISyncChunksDataCountersPtr & counters,
            [[maybe_unused]] const qevercloud::LinkedNotebook &
                linkedNotebook) {
            onLinkedNotebookSyncChunksDataProcessingProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::notesDownloadProgress, notifier,
        [&]([[maybe_unused]] const quint32 notesDownloaded,
            [[maybe_unused]] const quint32 totalNotesToDownload) {
            onNotesDownloadProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookNotesDownloadProgress,
        notifier,
        [&]([[maybe_unused]] const quint32 notesDownloaded,
            [[maybe_unused]] const quint32 totalNotesToDownload,
            [[maybe_unused]] const qevercloud::LinkedNotebook &
                linkedNotebook) {
            onLinkedNotebookNotesDownloadProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::resourcesDownloadProgress, notifier,
        [&]([[maybe_unused]] const quint32 resourcesDownloaded,
            [[maybe_unused]] const quint32 totalResourcesToDownload) {
            onResourcesDownloadProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookResourcesDownloadProgress,
        notifier,
        [&]([[maybe_unused]] const quint32 resourcesDownloaded,
            [[maybe_unused]] const quint32 totalResourcesToDownload,
            [[maybe_unused]] const qevercloud::LinkedNotebook &
                linkedNotebook) {
            onLinkedNotebookResourcesDownloadProgressCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::userOwnSendStatusUpdate, notifier,
        [&]([[maybe_unused]] const ISendStatusPtr & sendStatus) {
            onUserOwnSendStatusUpdateCalled = true;
        });

    QObject::connect(
        notifier, &ISyncEventsNotifier::linkedNotebookSendStatusUpdate,
        notifier,
        [&]([[maybe_unused]] const qevercloud::Guid & linkedNotebookGuid,
            [[maybe_unused]] const ISendStatusPtr & sendStatus) {
            onLinkedNotebookSendStatusUpdateCalled = true;
        });

    while (!callback) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }

    callback->onSyncChunksDownloadProgress(42, 42, 42);
    callback->onSyncChunksDownloaded();
    callback->onSyncChunksDataProcessingProgress(nullptr);
    callback->onStartLinkedNotebooksDataDownloading(
        QList<qevercloud::LinkedNotebook>{});

    callback->onLinkedNotebookSyncChunksDownloadProgress(
        42, 42, 42, qevercloud::LinkedNotebook{});

    callback->onLinkedNotebookSyncChunksDownloaded(
        qevercloud::LinkedNotebook{});

    callback->onLinkedNotebookSyncChunksDataProcessingProgress(
        nullptr, qevercloud::LinkedNotebook{});

    callback->onNotesDownloadProgress(42, 42);
    callback->onLinkedNotebookNotesDownloadProgress(
        42, 42, qevercloud::LinkedNotebook{});

    callback->onResourcesDownloadProgress(42, 42);
    callback->onLinkedNotebookResourcesDownloadProgress(
        42, 42, qevercloud::LinkedNotebook{});

    callback->onUserOwnSendStatusUpdate(nullptr);
    callback->onLinkedNotebookSendStatusUpdate(qevercloud::Guid{}, nullptr);

    auto syncResult = std::make_shared<SyncResult>();
    promise->addResult(syncResult);
    promise->finish();

    waitForFuture(result.first);
    ASSERT_EQ(result.first.resultCount(), 1);
    EXPECT_EQ(result.first.result(), syncResult);

    EXPECT_TRUE(onSyncChunksDownloadProgressCalled);
    EXPECT_TRUE(onSyncChunksDownloadedCalled);
    EXPECT_TRUE(onSyncChunksDataProcessingProgressCalled);
    EXPECT_TRUE(onStartLinkedNotebooksDataDownloadingCalled);
    EXPECT_TRUE(onLinkedNotebookSyncChunksDownloadProgressCalled);
    EXPECT_TRUE(onLinkedNotebookSyncChunksDownloadedCalled);
    EXPECT_TRUE(onLinkedNotebookSyncChunksDataProcessingProgressCalled);
    EXPECT_TRUE(onNotesDownloadProgressCalled);
    EXPECT_TRUE(onLinkedNotebookNotesDownloadProgressCalled);
    EXPECT_TRUE(onResourcesDownloadProgressCalled);
    EXPECT_TRUE(onLinkedNotebookResourcesDownloadProgressCalled);
    EXPECT_TRUE(onUserOwnSendStatusUpdateCalled);
    EXPECT_TRUE(onLinkedNotebookSendStatusUpdateCalled);
}

} // namespace quentier::synchronization::tests
