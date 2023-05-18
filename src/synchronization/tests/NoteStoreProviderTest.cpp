/*
 * Copyright 2022-2023 Dmitry Ivanov
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

#include <synchronization/NoteStoreProvider.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/types/Account.h>
#include <quentier/utility/UidGenerator.h>

#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/MockILinkedNotebookFinder.h>
#include <synchronization/tests/mocks/MockINoteStoreFactory.h>
#include <synchronization/tests/mocks/qevercloud/services/MockINoteStore.h>
#include <synchronization/types/AuthenticationInfo.h>

#include <qevercloud/DurableService.h>
#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/LinkedNotebookBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>

#include <gtest/gtest.h>

#include <memory>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::Return;
using testing::StrictMock;

class NoteStoreProviderTest : public testing::Test
{
protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockILinkedNotebookFinder>
        m_mockLinkedNotebookFinder =
            std::make_shared<StrictMock<mocks::MockILinkedNotebookFinder>>();

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const std::shared_ptr<mocks::MockINoteStoreFactory> m_mockNoteStoreFactory =
        std::make_shared<StrictMock<mocks::MockINoteStoreFactory>>();
};

TEST_F(NoteStoreProviderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
            m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
            m_mockNoteStoreFactory, m_account));
}

TEST_F(NoteStoreProviderTest, CtorNullLinkedNotebookFinder)
{
    EXPECT_THROW(
        const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
            nullptr, m_mockAuthenticationInfoProvider, m_mockNoteStoreFactory,
            m_account),
        InvalidArgument);
}

TEST_F(NoteStoreProviderTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
            m_mockLinkedNotebookFinder, nullptr, m_mockNoteStoreFactory,
            m_account),
        InvalidArgument);
}

TEST_F(NoteStoreProviderTest, CtorNullNoteStoreFactory)
{
    EXPECT_THROW(
        const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
            m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
            nullptr, m_account),
        InvalidArgument);
}

TEST_F(NoteStoreProviderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
            m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
            m_mockNoteStoreFactory, Account{}),
        InvalidArgument);
}

TEST_F(NoteStoreProviderTest, NoteStoreForUserOwnAccount)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillRepeatedly(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    const auto authInfo = std::make_shared<AuthenticationInfo>();
    authInfo->m_userId = m_account.id();
    authInfo->m_authToken = QStringLiteral("authToken");
    authInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");
    authInfo->m_authTokenExpirationTime =
        QDateTime::currentMSecsSinceEpoch() + 999999999;

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    const auto noteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    EXPECT_CALL(*noteStore, defaultRequestContext)
        .WillRepeatedly(Return(defaultCtx));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce(
            [&](const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
                EXPECT_FALSE(linkedNotebookGuid);
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(), authInfo->authToken());
                }
                EXPECT_NE(ctx.get(), defaultCtx.get());
                EXPECT_EQ(defaultRetryPolicy, retryPolicy);
                return noteStore;
            });

    auto resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    auto result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The second call should use cached information
    resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The third call after the call to clearCaches should again create new
    // note store
    noteStoreProvider->clearCaches();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce(
            [&](const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
                EXPECT_FALSE(linkedNotebookGuid);
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(), authInfo->authToken());
                }
                EXPECT_NE(ctx.get(), defaultCtx.get());
                EXPECT_EQ(defaultRetryPolicy, retryPolicy);
                return noteStore;
            });

    resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);
}

TEST_F(
    NoteStoreProviderTest,
    NoNoteStoreForUserOwnAccountIfCannotGetAuthenticationInfo)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{ErrorString{QStringLiteral("error")}})));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(NoteStoreProviderTest, NoteStoreForLinkedNotebook)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillRepeatedly(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    const auto authInfo = std::make_shared<AuthenticationInfo>();
    authInfo->m_userId = m_account.id();
    authInfo->m_authToken = QStringLiteral("authToken");
    authInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");
    authInfo->m_authTokenExpirationTime =
        QDateTime::currentMSecsSinceEpoch() + 999999999;

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    const auto noteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    EXPECT_CALL(*noteStore, defaultRequestContext)
        .WillRepeatedly(Return(defaultCtx));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce([&](const QString & noteStoreUrl,
                      const std::optional<qevercloud::Guid> & guid,
                      const qevercloud::IRequestContextPtr & ctx,
                      const qevercloud::IRetryPolicyPtr & retryPolicy) {
            EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
            EXPECT_EQ(guid, linkedNotebookGuid);
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(ctx->authenticationToken(), authInfo->authToken());
            }
            EXPECT_NE(ctx.get(), defaultCtx.get());
            EXPECT_EQ(defaultRetryPolicy, retryPolicy);
            return noteStore;
        });

    auto resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    auto result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The second call should use cached information
    resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The third call after the call to clearCaches should again create new
    // note store
    noteStoreProvider->clearCaches();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce([&](const QString & noteStoreUrl,
                      const std::optional<qevercloud::Guid> & guid,
                      const qevercloud::IRequestContextPtr & ctx,
                      const qevercloud::IRetryPolicyPtr & retryPolicy) {
            EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
            EXPECT_EQ(guid, linkedNotebookGuid);
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(ctx->authenticationToken(), authInfo->authToken());
            }
            EXPECT_NE(ctx.get(), defaultCtx.get());
            EXPECT_EQ(defaultRetryPolicy, retryPolicy);
            return noteStore;
        });

    resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);
}

TEST_F(
    NoteStoreProviderTest,
    NoNoteStoreForLinkedNotebookIfCannotGetAuthenticationInfo)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(UidGenerator::Generate())
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByNotebookLocalId(notebookLocalId))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{ErrorString{QStringLiteral("error")}})));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture = noteStoreProvider->noteStoreForNotebook(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(NoteStoreProviderTest, LinkedNotebookNoteStore)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByGuid(linkedNotebookGuid))
        .WillRepeatedly(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    const auto authInfo = std::make_shared<AuthenticationInfo>();
    authInfo->m_userId = m_account.id();
    authInfo->m_authToken = QStringLiteral("authToken");
    authInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");
    authInfo->m_authTokenExpirationTime =
        QDateTime::currentMSecsSinceEpoch() + 999999999;

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    const auto noteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    EXPECT_CALL(*noteStore, defaultRequestContext)
        .WillRepeatedly(Return(defaultCtx));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce([&](const QString & noteStoreUrl,
                      const std::optional<qevercloud::Guid> & guid,
                      const qevercloud::IRequestContextPtr & ctx,
                      const qevercloud::IRetryPolicyPtr & retryPolicy) {
            EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
            EXPECT_EQ(guid, linkedNotebookGuid);
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(ctx->authenticationToken(), authInfo->authToken());
            }
            EXPECT_NE(ctx.get(), defaultCtx.get());
            EXPECT_EQ(defaultRetryPolicy, retryPolicy);
            return noteStore;
        });

    auto resultFuture = noteStoreProvider->linkedNotebookNoteStore(
        linkedNotebookGuid, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    auto result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The second call should use cached information
    resultFuture = noteStoreProvider->linkedNotebookNoteStore(
        linkedNotebookGuid, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The third call after the call to clearCaches should again create new
    // note store
    noteStoreProvider->clearCaches();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByGuid(linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce([&](const QString & noteStoreUrl,
                      const std::optional<qevercloud::Guid> & guid,
                      const qevercloud::IRequestContextPtr & ctx,
                      const qevercloud::IRetryPolicyPtr & retryPolicy) {
            EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
            EXPECT_EQ(guid, linkedNotebookGuid);
            EXPECT_TRUE(ctx);
            if (ctx) {
                EXPECT_EQ(ctx->authenticationToken(), authInfo->authToken());
            }
            EXPECT_NE(ctx.get(), defaultCtx.get());
            EXPECT_EQ(defaultRetryPolicy, retryPolicy);
            return noteStore;
        });

    resultFuture = noteStoreProvider->linkedNotebookNoteStore(
        linkedNotebookGuid, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);
}

TEST_F(
    NoteStoreProviderTest, NoLinkedNotebookNoteStoreIfCannotFindLinkedNotebook)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByGuid(linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture = noteStoreProvider->linkedNotebookNoteStore(
        linkedNotebookGuid, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(
    NoteStoreProviderTest,
    NoLinkedNotebookNoteStoreIfCannotGetAuthenticationInfo)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLinkedNotebookFinder,
        findLinkedNotebookByGuid(linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateToLinkedNotebook(
            m_account, linkedNotebook,
            IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{ErrorString{QStringLiteral("error")}})));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture = noteStoreProvider->linkedNotebookNoteStore(
        linkedNotebookGuid, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(NoteStoreProviderTest, UserOwnNoteStore)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const auto authInfo = std::make_shared<AuthenticationInfo>();
    authInfo->m_userId = m_account.id();
    authInfo->m_authToken = QStringLiteral("authToken");
    authInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");
    authInfo->m_authTokenExpirationTime =
        QDateTime::currentMSecsSinceEpoch() + 999999999;

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    const auto noteStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockINoteStore>>();

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    EXPECT_CALL(*noteStore, defaultRequestContext)
        .WillRepeatedly(Return(defaultCtx));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce(
            [&](const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
                EXPECT_FALSE(linkedNotebookGuid);
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(), authInfo->authToken());
                }
                EXPECT_NE(ctx.get(), defaultCtx.get());
                EXPECT_EQ(defaultRetryPolicy, retryPolicy);
                return noteStore;
            });

    auto resultFuture =
        noteStoreProvider->userOwnNoteStore(defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    auto result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The second call should use cached information
    resultFuture =
        noteStoreProvider->userOwnNoteStore(defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);

    // The third call after the call to clearCaches should again create new
    // note store
    noteStoreProvider->clearCaches();

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(Return(
            threading::makeReadyFuture<IAuthenticationInfoPtr>(authInfo)));

    EXPECT_CALL(*m_mockNoteStoreFactory, noteStore)
        .WillOnce(
            [&](const QString & noteStoreUrl,
                const std::optional<qevercloud::Guid> & linkedNotebookGuid,
                const qevercloud::IRequestContextPtr & ctx,
                const qevercloud::IRetryPolicyPtr & retryPolicy) {
                EXPECT_EQ(noteStoreUrl, authInfo->m_noteStoreUrl);
                EXPECT_FALSE(linkedNotebookGuid);
                EXPECT_TRUE(ctx);
                if (ctx) {
                    EXPECT_EQ(
                        ctx->authenticationToken(), authInfo->authToken());
                }
                EXPECT_NE(ctx.get(), defaultCtx.get());
                EXPECT_EQ(defaultRetryPolicy, retryPolicy);
                return noteStore;
            });

    resultFuture =
        noteStoreProvider->userOwnNoteStore(defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    result = resultFuture.result();
    EXPECT_EQ(result, noteStore);
}

TEST_F(NoteStoreProviderTest, NoUserOwnNoteStoreIfCannotGetAuthenticationInfo)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLinkedNotebookFinder, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{ErrorString{QStringLiteral("error")}})));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture =
        noteStoreProvider->userOwnNoteStore(defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

} // namespace quentier::synchronization::tests
