/*
 * Copyright 2022 Dmitry Ivanov
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
#include <quentier/local_storage/tests/mocks/MockILocalStorage.h>
#include <quentier/threading/Future.h>
#include <quentier/types/Account.h>
#include <quentier/utility/UidGenerator.h>

#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
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

    const std::shared_ptr<local_storage::tests::mocks::MockILocalStorage>
        m_mockLocalStorage = std::make_shared<
            StrictMock<local_storage::tests::mocks::MockILocalStorage>>();

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
            m_mockLocalStorage, m_mockAuthenticationInfoProvider,
            m_mockNoteStoreFactory, m_account));
}

TEST_F(NoteStoreProviderTest, CtorNullLocalStorage)
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
            m_mockLocalStorage, nullptr, m_mockNoteStoreFactory, m_account),
        InvalidArgument);
}

TEST_F(NoteStoreProviderTest, CtorNullNoteStoreFactory)
{
    EXPECT_THROW(
        const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
            m_mockLocalStorage, m_mockAuthenticationInfoProvider, nullptr,
            m_account),
        InvalidArgument);
}

TEST_F(NoteStoreProviderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
            m_mockLocalStorage, m_mockAuthenticationInfoProvider,
            m_mockNoteStoreFactory, Account{}),
        InvalidArgument);
}

TEST_F(NoteStoreProviderTest, NoteStoreForUserOwnAccount)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setUpdateSequenceNum(42)
                    .setName(QStringLiteral("Notebook #1"))
                    .build())));

    const auto authInfo = std::make_shared<AuthenticationInfo>();
    authInfo->m_userId = m_account.id();
    authInfo->m_authToken = QStringLiteral("authToken");
    authInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");

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

    auto resultFuture = noteStoreProvider->noteStore(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    const auto result = resultFuture.result();
    EXPECT_EQ(result, noteStore);
}

TEST_F(NoteStoreProviderTest, NoNoteStoreForUserOwnAccountIfCannotFindNotebook)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                std::nullopt)));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture = noteStoreProvider->noteStore(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(
    NoteStoreProviderTest,
    NoNoteStoreForUserOwnAccountIfCannotGetAuthenticationInfo)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setUpdateSequenceNum(42)
                    .setName(QStringLiteral("Notebook #1"))
                    .build())));

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillOnce(
            Return(threading::makeExceptionalFuture<IAuthenticationInfoPtr>(
                RuntimeError{ErrorString{QStringLiteral("error")}})));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture = noteStoreProvider->noteStore(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(NoteStoreProviderTest, NoteStoreForLinkedNotebook)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setLinkedNotebookGuid(linkedNotebookGuid)
                    .setUpdateSequenceNum(42)
                    .setName(QStringLiteral("Notebook #1"))
                    .build())));

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    const auto authInfo = std::make_shared<AuthenticationInfo>();
    authInfo->m_userId = m_account.id();
    authInfo->m_authToken = QStringLiteral("authToken");
    authInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");

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

    auto resultFuture = noteStoreProvider->noteStore(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    ASSERT_EQ(resultFuture.resultCount(), 1);

    const auto result = resultFuture.result();
    EXPECT_EQ(result, noteStore);
}

TEST_F(
    NoteStoreProviderTest,
    NoNoteStoreForLinkedNotebookIfCannotFindLinkedNotebook)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setLinkedNotebookGuid(linkedNotebookGuid)
                    .setUpdateSequenceNum(42)
                    .setName(QStringLiteral("Notebook #1"))
                    .build())));

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(std::nullopt)));

    const auto defaultCtx = qevercloud::newRequestContext();
    const auto defaultRetryPolicy = qevercloud::newRetryPolicy();

    auto resultFuture = noteStoreProvider->noteStore(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(
    NoteStoreProviderTest,
    NoNoteStoreForLinkedNotebookIfCannotGetAuthenticationInfo)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const QString notebookLocalId = UidGenerator::Generate();
    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(*m_mockLocalStorage, findNotebookByLocalId(notebookLocalId))
        .WillOnce(Return(
            threading::makeReadyFuture<std::optional<qevercloud::Notebook>>(
                qevercloud::NotebookBuilder{}
                    .setLocalId(notebookLocalId)
                    .setGuid(UidGenerator::Generate())
                    .setLinkedNotebookGuid(linkedNotebookGuid)
                    .setUpdateSequenceNum(42)
                    .setName(QStringLiteral("Notebook #1"))
                    .build())));

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(linkedNotebookGuid))
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

    auto resultFuture = noteStoreProvider->noteStore(
        notebookLocalId, defaultCtx, defaultRetryPolicy);
    ASSERT_TRUE(resultFuture.isFinished());
    EXPECT_THROW(resultFuture.result(), RuntimeError);
}

TEST_F(NoteStoreProviderTest, LinkedNotebookNoteStore)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(linkedNotebookGuid))
        .WillOnce(
            Return(threading::makeReadyFuture<
                   std::optional<qevercloud::LinkedNotebook>>(linkedNotebook)));

    const auto authInfo = std::make_shared<AuthenticationInfo>();
    authInfo->m_userId = m_account.id();
    authInfo->m_authToken = QStringLiteral("authToken");
    authInfo->m_noteStoreUrl = QStringLiteral("noteStoreUrl");

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

    const auto result = resultFuture.result();
    EXPECT_EQ(result, noteStore);
}

TEST_F(
    NoteStoreProviderTest, NoLinkedNotebookNoteStoreIfCannotFindLinkedNotebook)
{
    const auto noteStoreProvider = std::make_shared<NoteStoreProvider>(
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(linkedNotebookGuid))
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
        m_mockLocalStorage, m_mockAuthenticationInfoProvider,
        m_mockNoteStoreFactory, m_account);

    const qevercloud::Guid linkedNotebookGuid = UidGenerator::Generate();

    const auto linkedNotebook = qevercloud::LinkedNotebookBuilder{}
                                    .setGuid(linkedNotebookGuid)
                                    .setUsername(QStringLiteral("username"))
                                    .setUpdateSequenceNum(43)
                                    .build();

    EXPECT_CALL(
        *m_mockLocalStorage, findLinkedNotebookByGuid(linkedNotebookGuid))
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

} // namespace quentier::synchronization::tests
