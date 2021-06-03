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

#include "../ConnectionPool.h"
#include "../TablesInitializer.h"
#include "../UsersHandler.h"

#include <quentier/exception/IQuentierException.h>

#include <QCoreApplication>
#include <QFlags>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <array>

namespace quentier::local_storage::sql::tests {

namespace {

[[nodiscard]] qevercloud::UserAttributes createUserAttributes()
{
    qevercloud::UserAttributes userAttributes;

    userAttributes.setDefaultLocationName(
        QStringLiteral("defaultLocationName"));

    userAttributes.setDefaultLatitude(55.0);
    userAttributes.setDefaultLongitude(36.0);
    userAttributes.setPreactivation(false);

    userAttributes.setViewedPromotions(
        QStringList{} << QStringLiteral("promotion1")
        << QStringLiteral("promotion2"));

    userAttributes.setIncomingEmailAddress(QStringLiteral("example@mail.com"));

    userAttributes.setRecentMailedAddresses(
        QStringList{} << QStringLiteral("recentMailedAddress1@example.com")
        << QStringLiteral("recentMailedAddress2@example.com"));

    userAttributes.setComments(QStringLiteral("comments"));
    userAttributes.setDateAgreedToTermsOfService(qevercloud::Timestamp{2});
    userAttributes.setMaxReferrals(32);
    userAttributes.setReferralCount(10);
    userAttributes.setRefererCode(QStringLiteral("refererCode"));
    userAttributes.setSentEmailDate(qevercloud::Timestamp{3});
    userAttributes.setSentEmailCount(20);
    userAttributes.setDailyEmailLimit(40);
    userAttributes.setEmailOptOutDate(qevercloud::Timestamp{4});
    userAttributes.setPartnerEmailOptInDate(qevercloud::Timestamp{5});
    userAttributes.setPreferredLanguage(QStringLiteral("En"));
    userAttributes.setPreferredCountry(QStringLiteral("New Zealand"));
    userAttributes.setClipFullPage(true);
    userAttributes.setTwitterUserName(QStringLiteral("twitterUserName"));
    userAttributes.setTwitterId(QStringLiteral("twitterId"));
    userAttributes.setGroupName(QStringLiteral("groupName"));
    userAttributes.setRecognitionLanguage(QStringLiteral("Ru"));
    userAttributes.setReferralProof(QStringLiteral("referralProof"));
    userAttributes.setEducationalDiscount(false);
    userAttributes.setBusinessAddress(QStringLiteral("business@example.com"));
    userAttributes.setHideSponsorBilling(true);
    userAttributes.setUseEmailAutoFiling(true);

    userAttributes.setReminderEmailConfig(
        qevercloud::ReminderEmailConfig::DO_NOT_SEND);

    userAttributes.setEmailAddressLastConfirmed(qevercloud::Timestamp{6});
    userAttributes.setPasswordUpdated(qevercloud::Timestamp{7});
    userAttributes.setSalesforcePushEnabled(false);
    userAttributes.setShouldLogClientEvent(false);

    return userAttributes;
}

[[nodiscard]] qevercloud::Accounting createAccounting()
{
    qevercloud::Accounting accounting;
    accounting.setUploadLimitEnd(qevercloud::Timestamp{1});
    accounting.setUploadLimitNextMonth(100L);
    accounting.setPremiumServiceStatus(qevercloud::PremiumOrderStatus::ACTIVE);
    accounting.setPremiumOrderNumber(QStringLiteral("premiumOrderNumber"));

    accounting.setPremiumCommerceService(
        QStringLiteral("premiumCommerceService"));

    accounting.setPremiumServiceStart(qevercloud::Timestamp{2});
    accounting.setPremiumServiceSKU(QStringLiteral("premiumServiceSKU"));
    accounting.setLastSuccessfulCharge(qevercloud::Timestamp{3});
    accounting.setLastFailedCharge(qevercloud::Timestamp{4});

    accounting.setLastFailedChargeReason(
        QStringLiteral("lastFailedChargeReason"));

    accounting.setNextPaymentDue(qevercloud::Timestamp{5});
    accounting.setPremiumLockUntil(qevercloud::Timestamp{6});
    accounting.setUpdated(qevercloud::Timestamp{7});

    accounting.setPremiumSubscriptionNumber(
        QStringLiteral("premiumSubscriptionNumber"));

    accounting.setLastRequestedCharge(qevercloud::Timestamp{8});
    accounting.setCurrency(QStringLiteral("USD"));
    accounting.setUnitPrice(90);
    accounting.setUnitDiscount(2);
    accounting.setNextChargeDate(qevercloud::Timestamp{9});
    accounting.setAvailablePoints(3);

    return accounting;
}

[[nodiscard]] qevercloud::AccountLimits createAccountLimits()
{
    qevercloud::AccountLimits accountLimits;
    accountLimits.setUserMailLimitDaily(1);
    accountLimits.setNoteSizeMax(2);
    accountLimits.setResourceSizeMax(3);
    accountLimits.setUserLinkedNotebookMax(4);
    accountLimits.setUploadLimit(5);
    accountLimits.setUserNoteCountMax(6);
    accountLimits.setUserNotebookCountMax(7);
    accountLimits.setUserTagCountMax(8);
    accountLimits.setNoteTagCountMax(9);
    accountLimits.setUserSavedSearchesMax(10);
    accountLimits.setNoteResourceCountMax(11);
    return accountLimits;
}

[[nodiscard]] qevercloud::BusinessUserInfo createBusinessUserInfo()
{
    qevercloud::BusinessUserInfo businessUserInfo;
    businessUserInfo.setBusinessId(1);
    businessUserInfo.setBusinessName(QStringLiteral("businessName"));
    businessUserInfo.setRole(qevercloud::BusinessUserRole::NORMAL);
    businessUserInfo.setEmail(QStringLiteral("email"));
    return businessUserInfo;
}

enum class CreateUserOption
{
    WithUserAttributes,
    WithAccounting,
    WithAccountLimits,
    WithBusinessUserInfo
};

Q_DECLARE_FLAGS(CreateUserOptions, CreateUserOption);

[[nodiscard]] qevercloud::User createUser(
    const CreateUserOptions & createUserOptions = {})
{
    qevercloud::User user;
    user.setId(1);
    user.setUsername(QStringLiteral("fake_user_username"));
    user.setEmail(QStringLiteral("fake_user _mail"));
    user.setName(QStringLiteral("fake_user_name"));
    user.setTimezone(QStringLiteral("fake_user_timezone"));
    user.setPrivilege(qevercloud::PrivilegeLevel::NORMAL);
    user.setCreated(2);
    user.setUpdated(3);
    user.setActive(true);

    if (createUserOptions & CreateUserOption::WithUserAttributes) {
        user.setAttributes(createUserAttributes());
    }

    if (createUserOptions & CreateUserOption::WithAccounting) {
        user.setAccounting(createAccounting());
    }

    if (createUserOptions & CreateUserOption::WithAccountLimits) {
        user.setAccountLimits(createAccountLimits());
    }

    if (createUserOptions & CreateUserOption::WithBusinessUserInfo) {
        user.setBusinessUserInfo(createBusinessUserInfo());
    }

    return user;
}

class UsersHandlerTest : public testing::Test
{
protected:
    void SetUp() override
    {
        m_connectionPool = std::make_shared<ConnectionPool>(
            QStringLiteral("localhost"), QStringLiteral("user"),
            QStringLiteral("password"), QStringLiteral("file::memory:"),
            QStringLiteral("QSQLITE"),
            QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_ENABLE_SHARED_CACHE"));

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_writerThread = std::make_shared<QThread>();
        m_writerThread->start();
    }

    void TearDown() override
    {
        m_writerThread->quit();
        m_writerThread->wait();

        // Give lambdas connected to threads finished signal a chance to fire
        QCoreApplication::processEvents();
    }

protected:
    ConnectionPoolPtr m_connectionPool;
    QThreadPtr m_writerThread;
};

} // namespace

TEST_F(UsersHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto usersHandler = std::make_shared<UsersHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_writerThread));
}

TEST_F(UsersHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto usersHandler = std::make_shared<UsersHandler>(
            nullptr, QThreadPool::globalInstance(), m_writerThread),
        IQuentierException);
}

TEST_F(UsersHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto usersHandler = std::make_shared<UsersHandler>(
            m_connectionPool, nullptr, m_writerThread),
        IQuentierException);
}

TEST_F(UsersHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto usersHandler = std::make_shared<UsersHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr),
        IQuentierException);
}

TEST_F(UsersHandlerTest, ShouldHaveZeroUserCountWhenThereAreNoUsers)
{
    const auto usersHandler = std::make_shared<UsersHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

    auto userCountFuture = usersHandler->userCount();
    userCountFuture.waitForFinished();
    EXPECT_EQ(userCountFuture.result(), 0U);
}

TEST_F(UsersHandlerTest, ShouldNotFindNonexistentUser)
{
    const auto usersHandler = std::make_shared<UsersHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

    auto userFuture = usersHandler->findUserById(qevercloud::UserID{1});
    userFuture.waitForFinished();
    EXPECT_EQ(userFuture.resultCount(), 0);
}

TEST_F(UsersHandlerTest, IgnoreAttemptToExpungeNonexistentUser)
{
    const auto usersHandler = std::make_shared<UsersHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

    auto expungeUserFuture = usersHandler->findUserById(qevercloud::UserID{1});
    EXPECT_NO_THROW(expungeUserFuture.waitForFinished());
}

class UsersHandlerPutNewUserTest :
    public UsersHandlerTest,
    public testing::WithParamInterface<qevercloud::User>
{};

const std::array put_new_user_test_values{
    createUser(),
    createUser(CreateUserOptions{CreateUserOption::WithUserAttributes}),
    createUser(CreateUserOptions{CreateUserOption::WithAccounting}),
    createUser(CreateUserOptions{CreateUserOption::WithAccountLimits}),
    createUser(CreateUserOptions{CreateUserOption::WithBusinessUserInfo}),
    createUser(CreateUserOptions{
        CreateUserOption::WithAccounting} |
        CreateUserOption::WithUserAttributes),
    createUser(CreateUserOptions{
        CreateUserOption::WithAccounting} |
        CreateUserOption::WithBusinessUserInfo),
    createUser(CreateUserOptions{
        CreateUserOption::WithAccounting} |
        CreateUserOption::WithAccountLimits),
    createUser(CreateUserOptions{
        CreateUserOption::WithUserAttributes} |
        CreateUserOption::WithBusinessUserInfo),
    createUser(CreateUserOptions{
        CreateUserOption::WithUserAttributes} |
        CreateUserOption::WithAccountLimits),
    createUser(CreateUserOptions{
        CreateUserOption::WithBusinessUserInfo} |
        CreateUserOption::WithAccountLimits),
    createUser(CreateUserOptions{
        CreateUserOption::WithAccounting} |
        CreateUserOption::WithBusinessUserInfo |
        CreateUserOption::WithUserAttributes),
    createUser(CreateUserOptions{
        CreateUserOption::WithAccounting} |
        CreateUserOption::WithBusinessUserInfo |
        CreateUserOption::WithAccountLimits),
    createUser(CreateUserOptions{
        CreateUserOption::WithUserAttributes} |
        CreateUserOption::WithBusinessUserInfo |
        CreateUserOption::WithAccountLimits),
    createUser(CreateUserOptions{
        CreateUserOption::WithAccounting} |
        CreateUserOption::WithAccountLimits |
        CreateUserOption::WithUserAttributes),
    createUser(CreateUserOptions{
        CreateUserOption::WithAccounting} |
        CreateUserOption::WithAccountLimits |
        CreateUserOption::WithBusinessUserInfo |
        CreateUserOption::WithUserAttributes),
};

INSTANTIATE_TEST_SUITE_P(
    UsersHandlerPutNewUserTestInstance,
    UsersHandlerPutNewUserTest,
    testing::ValuesIn(put_new_user_test_values));

TEST_P(UsersHandlerPutNewUserTest, PutNewUser)
{
    const auto usersHandler = std::make_shared<UsersHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_writerThread);

    const auto user = GetParam();
    auto putUserFuture = usersHandler->putUser(user);
    EXPECT_NO_THROW(putUserFuture.waitForFinished());

    auto userCountFuture = usersHandler->userCount();
    userCountFuture.waitForFinished();
    EXPECT_EQ(userCountFuture.result(), 1U);

    auto foundUserFuture = usersHandler->findUserById(*user.id());
    foundUserFuture.waitForFinished();
    const auto foundUser = foundUserFuture.result();
    EXPECT_TRUE(foundUser);
    EXPECT_EQ(*foundUser, user);
}

} // namespace quentier::local_storage::sql::tests
