/*
 * Copyright 2021 Dmitry Ivanov
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

#include <local_storage/sql/LocalStorage.h>
#include <local_storage/sql/Notifier.h>

#include <local_storage/sql/tests/mocks/MockILinkedNotebooksHandler.h>
#include <local_storage/sql/tests/mocks/MockINotebooksHandler.h>
#include <local_storage/sql/tests/mocks/MockINotesHandler.h>
#include <local_storage/sql/tests/mocks/MockIResourcesHandler.h>
#include <local_storage/sql/tests/mocks/MockISavedSearchesHandler.h>
#include <local_storage/sql/tests/mocks/MockISynchronizationInfoHandler.h>
#include <local_storage/sql/tests/mocks/MockITagsHandler.h>
#include <local_storage/sql/tests/mocks/MockIUsersHandler.h>
#include <local_storage/sql/tests/mocks/MockIVersionHandler.h>

#include <utility/Threading.h>

#include <quentier/exception/InvalidArgument.h>

#include <gtest/gtest.h>

namespace quentier::local_storage::sql::tests {

using testing::Return;
using testing::StrictMock;

class LocalStorageTest : public testing::Test
{
protected:
    [[nodiscard]] ILocalStoragePtr createLocalStorage()
    {
        return std::make_shared<LocalStorage>(
            m_mockLinkedNotebooksHandler,
            m_mockNotebooksHandler,
            m_mockNotesHandler,
            m_mockResourcesHandler,
            m_mockSavedSearchesHandler,
            m_mockSynchronizationInfoHandler,
            m_mockTagsHandler,
            m_mockVersionHandler,
            m_mockUsersHandler,
            m_notifier.get());
    }

protected:
    std::shared_ptr<mocks::MockILinkedNotebooksHandler> m_mockLinkedNotebooksHandler =
        std::make_shared<StrictMock<mocks::MockILinkedNotebooksHandler>>();

    std::shared_ptr<mocks::MockINotebooksHandler> m_mockNotebooksHandler =
        std::make_shared<StrictMock<mocks::MockINotebooksHandler>>();

    std::shared_ptr<mocks::MockINotesHandler> m_mockNotesHandler =
        std::make_shared<StrictMock<mocks::MockINotesHandler>>();

    std::shared_ptr<mocks::MockIResourcesHandler> m_mockResourcesHandler =
        std::make_shared<StrictMock<mocks::MockIResourcesHandler>>();

    std::shared_ptr<mocks::MockISavedSearchesHandler> m_mockSavedSearchesHandler =
        std::make_shared<StrictMock<mocks::MockISavedSearchesHandler>>();

    std::shared_ptr<mocks::MockISynchronizationInfoHandler> m_mockSynchronizationInfoHandler =
        std::make_shared<StrictMock<mocks::MockISynchronizationInfoHandler>>();

    std::shared_ptr<mocks::MockITagsHandler> m_mockTagsHandler =
        std::make_shared<StrictMock<mocks::MockITagsHandler>>();

    std::shared_ptr<mocks::MockIUsersHandler> m_mockUsersHandler =
        std::make_shared<StrictMock<mocks::MockIUsersHandler>>();

    std::shared_ptr<mocks::MockIVersionHandler> m_mockVersionHandler =
        std::make_shared<StrictMock<mocks::MockIVersionHandler>>();

    std::unique_ptr<Notifier> m_notifier = std::make_unique<Notifier>();
};

TEST_F(LocalStorageTest, Ctor)
{
    EXPECT_NO_THROW((LocalStorage{
        m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
        m_mockNotesHandler, m_mockResourcesHandler, m_mockSavedSearchesHandler,
        m_mockSynchronizationInfoHandler, m_mockTagsHandler,
        m_mockVersionHandler, m_mockUsersHandler, m_notifier.get()}));
}

TEST_F(LocalStorageTest, CtorNullLinkedNotebooksHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            nullptr, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotebooksHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, nullptr,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            nullptr, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullResourcesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, nullptr,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullSavedSearchesHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            nullptr, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullSynchronizationInfoHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, nullptr,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullTagsHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            nullptr, m_mockVersionHandler, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullVersionHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, nullptr, m_mockUsersHandler,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullUsersHandler)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, nullptr,
            m_notifier.get()}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, CtorNullNotifier)
{
    EXPECT_THROW(
        (LocalStorage{
            m_mockLinkedNotebooksHandler, m_mockNotebooksHandler,
            m_mockNotesHandler, m_mockResourcesHandler,
            m_mockSavedSearchesHandler, m_mockSynchronizationInfoHandler,
            m_mockTagsHandler, m_mockVersionHandler, m_mockUsersHandler,
            nullptr}),
        InvalidArgument);
}

TEST_F(LocalStorageTest, ForwardIsVersionTooHighToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, isVersionTooHigh)
        .WillOnce(Return(utility::makeReadyFuture<bool>(false)));

    const auto res = localStorage->isVersionTooHigh();
    EXPECT_TRUE(res.isFinished());
    EXPECT_EQ(res.resultCount(), 1);
    EXPECT_FALSE(res.result());
}

TEST_F(LocalStorageTest, ForwardRequiresUpgradeToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, requiresUpgrade)
        .WillOnce(Return(utility::makeReadyFuture<bool>(true)));

    const auto res = localStorage->requiresUpgrade();
    EXPECT_TRUE(res.isFinished());    EXPECT_EQ(res.resultCount(), 1);
    EXPECT_TRUE(res.result());
}

TEST_F(LocalStorageTest, ForwardRequiredPatchesToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, requiredPatches)
        .WillOnce(Return(utility::makeReadyFuture<QList<IPatchPtr>>({})));

    const auto res = localStorage->requiredPatches();
    EXPECT_TRUE(res.isFinished());
    EXPECT_EQ(res.resultCount(), 1);
    EXPECT_TRUE(res.result().isEmpty());
}

TEST_F(LocalStorageTest, ForwardVersionToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, version)
        .WillOnce(Return(utility::makeReadyFuture<qint32>(3)));

    const auto res = localStorage->version();
    EXPECT_TRUE(res.isFinished());
    EXPECT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), 3);
}

TEST_F(LocalStorageTest, ForwardHighestSupportedVersionToVersionHandler)
{
    const auto localStorage = createLocalStorage();

    EXPECT_CALL(*m_mockVersionHandler, highestSupportedVersion)
        .WillOnce(Return(utility::makeReadyFuture<qint32>(3)));

    const auto res = localStorage->highestSupportedVersion();
    EXPECT_TRUE(res.isFinished());
    EXPECT_EQ(res.resultCount(), 1);
    EXPECT_EQ(res.result(), 3);
}

} // namespace quentier::local_storage::sql::tests
