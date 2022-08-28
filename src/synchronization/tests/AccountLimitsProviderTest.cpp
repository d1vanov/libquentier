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

#include <synchronization/AccountLimitsProvider.h>
#include <synchronization/tests/mocks/MockIAuthenticationInfoProvider.h>
#include <synchronization/tests/mocks/qevercloud/services/MockIUserStore.h>
#include <synchronization/types/AuthenticationInfo.h>

#include <quentier/exception/InvalidArgument.h>
#include <quentier/threading/Future.h>
#include <quentier/utility/ApplicationSettings.h>

#include <qevercloud/RequestContext.h>
#include <qevercloud/types/builders/AccountLimitsBuilder.h>

#include <QDateTime>
#include <QTextStream>

#include <gtest/gtest.h>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::synchronization::tests {

using testing::_;
using testing::Return;
using testing::StrictMock;

namespace {

[[nodiscard]] QString appSettingsAccountLimitsGroupName(
    const qevercloud::ServiceLevel serviceLevel)
{
    QString res;
    QTextStream strm{&res};
    strm << "AccountLimits/" << serviceLevel;
    return res;
}

void checkAccountLimitsPersistence(
    const Account & account, const qevercloud::ServiceLevel serviceLevel,
    const qevercloud::AccountLimits & accountLimits)
{
    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(appSettingsAccountLimitsGroupName(serviceLevel));
    const ApplicationSettings::GroupCloser groupCloser{appSettings};

    const auto lastSyncTimestampValue =
        appSettings.value(QStringLiteral("lastSyncTime"));
    ASSERT_FALSE(lastSyncTimestampValue.isNull());

    bool conversionResult = false;
    Q_UNUSED(lastSyncTimestampValue.toLongLong(&conversionResult));
    EXPECT_TRUE(conversionResult);

    const auto userMailLimitDailyValue =
        appSettings.value(QStringLiteral("userMailLimitDaily"));
    if (accountLimits.userMailLimitDaily()) {
        ASSERT_FALSE(userMailLimitDailyValue.isNull());
        conversionResult = false;
        const qint32 userMailLimitDaily =
            userMailLimitDailyValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(userMailLimitDaily, *accountLimits.userMailLimitDaily());
    }
    else {
        EXPECT_TRUE(userMailLimitDailyValue.isNull());
    }

    const auto noteSizeMaxValue =
        appSettings.value(QStringLiteral("noteSizeMax"));
    if (accountLimits.noteSizeMax()) {
        ASSERT_FALSE(noteSizeMaxValue.isNull());
        conversionResult = false;
        const qint64 noteSizeMax =
            noteSizeMaxValue.toLongLong(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(noteSizeMax, *accountLimits.noteSizeMax());
    }
    else {
        EXPECT_TRUE(noteSizeMaxValue.isNull());
    }

    const auto resourceSizeMaxValue =
        appSettings.value(QStringLiteral("resourceSizeMax"));
    if (accountLimits.resourceSizeMax()) {
        ASSERT_FALSE(resourceSizeMaxValue.isNull());
        conversionResult = false;
        const qint64 resourceSizeMax =
            resourceSizeMaxValue.toLongLong(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(resourceSizeMax, *accountLimits.resourceSizeMax());
    }
    else {
        EXPECT_TRUE(resourceSizeMaxValue.isNull());
    }

    const auto userLinkedNotebookMaxValue =
        appSettings.value(QStringLiteral("userLinkedNotebookMax"));
    if (accountLimits.userLinkedNotebookMax()) {
        ASSERT_FALSE(userLinkedNotebookMaxValue.isNull());
        conversionResult = false;
        const qint32 userLinkedNotebookMax =
            userLinkedNotebookMaxValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(
            userLinkedNotebookMax, *accountLimits.userLinkedNotebookMax());
    }
    else {
        EXPECT_TRUE(userLinkedNotebookMaxValue.isNull());
    }

    const auto uploadLimitValue =
        appSettings.value(QStringLiteral("uploadLimit"));
    if (accountLimits.uploadLimit()) {
        ASSERT_FALSE(uploadLimitValue.isNull());
        conversionResult = false;
        const qint64 uploadLimit =
            uploadLimitValue.toLongLong(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(uploadLimit, *accountLimits.uploadLimit());
    }
    else {
        EXPECT_TRUE(uploadLimitValue.isNull());
    }

    const auto userNoteCountMaxValue =
        appSettings.value(QStringLiteral("userNoteCountMax"));
    if (accountLimits.userNoteCountMax()) {
        ASSERT_FALSE(userNoteCountMaxValue.isNull());
        conversionResult = false;
        const qint32 userNoteCountMax =
            userNoteCountMaxValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(userNoteCountMax, *accountLimits.userNoteCountMax());
    }
    else {
        EXPECT_TRUE(userNoteCountMaxValue.isNull());
    }

    const auto userNotebookCountMaxValue =
        appSettings.value(QStringLiteral("userNotebookCountMax"));
    if (accountLimits.userNotebookCountMax()) {
        ASSERT_FALSE(userNotebookCountMaxValue.isNull());
        conversionResult = false;
        const qint32 userNotebookCountMax =
            userNotebookCountMaxValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(userNotebookCountMax, *accountLimits.userNotebookCountMax());
    }
    else {
        EXPECT_TRUE(userNotebookCountMaxValue.isNull());
    }

    const auto userTagCountMaxValue =
        appSettings.value(QStringLiteral("userTagCountMax"));
    if (accountLimits.userTagCountMax()) {
        ASSERT_FALSE(userTagCountMaxValue.isNull());
        conversionResult = false;
        const qint32 userTagCountMax =
            userTagCountMaxValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(userTagCountMax, *accountLimits.userTagCountMax());
    }
    else {
        EXPECT_TRUE(userTagCountMaxValue.isNull());
    }

    const auto userSavedSearchCountMaxValue =
        appSettings.value(QStringLiteral("userSavedSearchCountMax"));
    if (accountLimits.userSavedSearchesMax()) {
        ASSERT_FALSE(userSavedSearchCountMaxValue.isNull());
        conversionResult = false;
        const qint32 userSavedSearchCountMax =
            userSavedSearchCountMaxValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(
            userSavedSearchCountMax, *accountLimits.userSavedSearchesMax());
    }
    else {
        EXPECT_TRUE(userSavedSearchCountMaxValue.isNull());
    }

    const auto noteResourceCountMaxValue =
        appSettings.value(QStringLiteral("noteResourceCountMax"));
    if (accountLimits.noteResourceCountMax()) {
        ASSERT_FALSE(noteResourceCountMaxValue.isNull());
        conversionResult = false;
        const qint32 noteResourceCountMax =
            noteResourceCountMaxValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(noteResourceCountMax, *accountLimits.noteResourceCountMax());
    }
    else {
        EXPECT_TRUE(noteResourceCountMaxValue.isNull());
    }

    const auto noteTagCountMaxValue =
        appSettings.value(QStringLiteral("noteTagCountMax"));
    if (accountLimits.noteTagCountMax()) {
        ASSERT_FALSE(noteTagCountMaxValue.isNull());
        conversionResult = false;
        const qint32 noteTagCountMax =
            noteTagCountMaxValue.toInt(&conversionResult);
        EXPECT_TRUE(conversionResult);
        EXPECT_EQ(noteTagCountMax, *accountLimits.noteTagCountMax());
    }
    else {
        EXPECT_TRUE(noteTagCountMaxValue.isNull());
    }
}

void setupAccountLimitsPersistence(
    const Account & account, const qevercloud::ServiceLevel serviceLevel,
    const qevercloud::AccountLimits & accountLimits,
    const qint64 currentTimestamp)
{
    ApplicationSettings appSettings{
        account, QStringLiteral("SynchronizationPersistence")};

    appSettings.beginGroup(appSettingsAccountLimitsGroupName(serviceLevel));
    const ApplicationSettings::GroupCloser groupCloser{appSettings};

    appSettings.setValue(QStringLiteral("lastSyncTime"), currentTimestamp);

    if (accountLimits.userMailLimitDaily()) {
        appSettings.setValue(
            QStringLiteral("userMailLimitDaily"),
            *accountLimits.userMailLimitDaily());
    }
    else {
        appSettings.remove(QStringLiteral("userMailLimitDaily"));
    }

    if (accountLimits.noteSizeMax()) {
        appSettings.setValue(
            QStringLiteral("noteSizeMax"), *accountLimits.noteSizeMax());
    }
    else {
        appSettings.remove(QStringLiteral("noteSizeMax"));
    }

    if (accountLimits.resourceSizeMax()) {
        appSettings.setValue(
            QStringLiteral("resourceSizeMax"),
            *accountLimits.resourceSizeMax());
    }
    else {
        appSettings.remove(QStringLiteral("resourceSizeMax"));
    }

    if (accountLimits.userLinkedNotebookMax()) {
        appSettings.setValue(
            QStringLiteral("userLinkedNotebookMax"),
            *accountLimits.userLinkedNotebookMax());
    }
    else {
        appSettings.remove(QStringLiteral("userLinkedNotebookMax"));
    }

    if (accountLimits.uploadLimit()) {
        appSettings.setValue(
            QStringLiteral("uploadLimit"), *accountLimits.uploadLimit());
    }
    else {
        appSettings.remove(QStringLiteral("uploadLimit"));
    }

    if (accountLimits.userNoteCountMax()) {
        appSettings.setValue(
            QStringLiteral("userNoteCountMax"),
            *accountLimits.userNoteCountMax());
    }
    else {
        appSettings.remove(QStringLiteral("userNoteCountMax"));
    }

    if (accountLimits.userNotebookCountMax()) {
        appSettings.setValue(
            QStringLiteral("userNotebookCountMax"),
            *accountLimits.userNotebookCountMax());
    }
    else {
        appSettings.remove(QStringLiteral("userNotebookCountMax"));
    }

    if (accountLimits.userTagCountMax()) {
        appSettings.setValue(
            QStringLiteral("userTagCountMax"),
            *accountLimits.userTagCountMax());
    }
    else {
        appSettings.remove(QStringLiteral("userTagCountMax"));
    }

    if (accountLimits.userSavedSearchesMax()) {
        appSettings.setValue(
            QStringLiteral("userSavedSearchCountMax"),
            *accountLimits.userSavedSearchesMax());
    }
    else {
        appSettings.remove(QStringLiteral("userSavedSearchCountMax"));
    }

    if (accountLimits.noteResourceCountMax()) {
        appSettings.setValue(
            QStringLiteral("noteResourceCountMax"),
            *accountLimits.noteResourceCountMax());
    }
    else {
        appSettings.remove(QStringLiteral("noteResourceCountMax"));
    }

    if (accountLimits.noteTagCountMax()) {
        appSettings.setValue(
            QStringLiteral("noteTagCountMax"),
            *accountLimits.noteTagCountMax());
    }
    else {
        appSettings.remove(QStringLiteral("noteTagCountMax"));
    }
}

} // namespace

class AccountLimitsProviderTest : public testing::Test
{
protected:
    void SetUp() override
    {
        clearPersistence();
    }

    void TearDown() override
    {
        clearPersistence();
    };

private:
    void clearPersistence()
    {
        ApplicationSettings appSettings{
            m_account, QStringLiteral("SynchronizationPersistence")};

        appSettings.remove(QString::fromUtf8(""));
        appSettings.sync();
    }

protected:
    const Account m_account = Account{
        QStringLiteral("Full Name"),
        Account::Type::Evernote,
        qevercloud::UserID{42},
        Account::EvernoteAccountType::Free,
        QStringLiteral("https://www.evernote.com"),
        QStringLiteral("shard id")};

    const std::shared_ptr<mocks::MockIAuthenticationInfoProvider>
        m_mockAuthenticationInfoProvider = std::make_shared<
            StrictMock<mocks::MockIAuthenticationInfoProvider>>();

    const std::shared_ptr<mocks::qevercloud::MockIUserStore> m_mockUserStore =
        std::make_shared<StrictMock<mocks::qevercloud::MockIUserStore>>();

    const qevercloud::IRequestContextPtr m_ctx =
        qevercloud::newRequestContext();
};

TEST_F(AccountLimitsProviderTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                m_account, m_mockAuthenticationInfoProvider, m_mockUserStore,
                m_ctx));
}

TEST_F(AccountLimitsProviderTest, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                Account{}, m_mockAuthenticationInfoProvider, m_mockUserStore,
                m_ctx),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, CtorNonEvernoteAccount)
{
    Account account{QStringLiteral("Full Name"), Account::Type::Local};

    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                std::move(account), m_mockAuthenticationInfoProvider,
                m_mockUserStore, m_ctx),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, CtorNullAuthenticationInfoProvider)
{
    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                m_account, nullptr, m_mockUserStore, m_ctx),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, CtorNullUserStore)
{
    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                m_account, m_mockAuthenticationInfoProvider, nullptr, m_ctx),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, CtorNullRequestContext)
{
    EXPECT_THROW(
        const auto accountLimitsProvider =
            std::make_shared<AccountLimitsProvider>(
                m_account, m_mockAuthenticationInfoProvider, m_mockUserStore,
                nullptr),
        InvalidArgument);
}

TEST_F(AccountLimitsProviderTest, GetAccountLimitsWithoutPreexistingStorage)
{
    const auto accountLimitsProvider = std::make_shared<AccountLimitsProvider>(
        m_account, m_mockAuthenticationInfoProvider, m_mockUserStore, m_ctx);

    const auto accountLimits = qevercloud::AccountLimitsBuilder{}
                                   .setNoteTagCountMax(42)
                                   .setUploadLimit(200)
                                   .setUserTagCountMax(30)
                                   .setNoteSizeMax(2000)
                                   .setNoteResourceCountMax(30)
                                   .build();

    const QString authToken = QStringLiteral("authToken");

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillRepeatedly(
            [&](const Account &, const IAuthenticationInfoProvider::Mode) {
                auto info = std::make_shared<AuthenticationInfo>();
                info->m_userId = m_account.id();
                info->m_authToken = authToken;
                return threading::makeReadyFuture<IAuthenticationInfoPtr>(
                    std::move(info));
            });

    EXPECT_CALL(
        *m_mockUserStore,
        getAccountLimitsAsync(qevercloud::ServiceLevel::BASIC, _))
        .WillOnce([&](const qevercloud::ServiceLevel,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_EQ(ctx->authenticationToken(), authToken);
            return threading::makeReadyFuture<qevercloud::AccountLimits>(
                accountLimits);
        });

    auto future =
        accountLimitsProvider->accountLimits(qevercloud::ServiceLevel::BASIC);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    auto result = future.result();
    EXPECT_EQ(result, accountLimits);

    checkAccountLimitsPersistence(
        m_account, qevercloud::ServiceLevel::BASIC, accountLimits);

    // The second call with the same argument should not trigger the call of
    // IUserStore as the result of the first call should be cached
    future =
        accountLimitsProvider->accountLimits(qevercloud::ServiceLevel::BASIC);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    result = future.result();
    EXPECT_EQ(result, accountLimits);

    checkAccountLimitsPersistence(
        m_account, qevercloud::ServiceLevel::BASIC, accountLimits);

    // The call with another argument value should trigger the call of
    // IUSerStore method
    EXPECT_CALL(
        *m_mockUserStore,
        getAccountLimitsAsync(qevercloud::ServiceLevel::PLUS, _))
        .WillOnce([&](const qevercloud::ServiceLevel,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_EQ(ctx->authenticationToken(), authToken);
            return threading::makeReadyFuture<qevercloud::AccountLimits>(
                accountLimits);
        });

    future =
        accountLimitsProvider->accountLimits(qevercloud::ServiceLevel::PLUS);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    result = future.result();
    EXPECT_EQ(result, accountLimits);

    checkAccountLimitsPersistence(
        m_account, qevercloud::ServiceLevel::PLUS, accountLimits);
}

TEST_F(
    AccountLimitsProviderTest, GetAccountLimitsWithRelevantPreexistingStorage)
{
    const auto accountLimitsProvider = std::make_shared<AccountLimitsProvider>(
        m_account, m_mockAuthenticationInfoProvider, m_mockUserStore, m_ctx);

    const auto accountLimits = qevercloud::AccountLimitsBuilder{}
                                   .setNoteTagCountMax(42)
                                   .setUploadLimit(200)
                                   .setUserTagCountMax(30)
                                   .setNoteSizeMax(2000)
                                   .setNoteResourceCountMax(30)
                                   .build();

    // If persistently stored account limits already exist and are still
    // relevant, there's no need to call
    // qevercloud::IUserStore::getAccountLimitsAsync
    const auto now = QDateTime::currentMSecsSinceEpoch();
    setupAccountLimitsPersistence(
        m_account, qevercloud::ServiceLevel::PREMIUM, accountLimits, now);

    auto future =
        accountLimitsProvider->accountLimits(qevercloud::ServiceLevel::PREMIUM);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto result = future.result();
    EXPECT_EQ(result, accountLimits);

    checkAccountLimitsPersistence(
        m_account, qevercloud::ServiceLevel::PREMIUM, accountLimits);
}

TEST_F(
    AccountLimitsProviderTest, GetAccountLimitsWithIrrelevantPreexistingStorage)
{
    const auto accountLimitsProvider = std::make_shared<AccountLimitsProvider>(
        m_account, m_mockAuthenticationInfoProvider, m_mockUserStore, m_ctx);

    const auto accountLimits = qevercloud::AccountLimitsBuilder{}
                                   .setNoteTagCountMax(42)
                                   .setUploadLimit(200)
                                   .setUserTagCountMax(30)
                                   .setNoteSizeMax(2000)
                                   .setNoteResourceCountMax(30)
                                   .build();

    // If persistently stored account limits are too old, they should
    // be requested from Evernote via the call to
    // qevercloud::IUserStore::getAccountLimitsAsync
    constexpr qint64 thirty_days_in_msec = 2592000000;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    const auto oldTimestamp = now - thirty_days_in_msec - 1000000;

    setupAccountLimitsPersistence(
        m_account, qevercloud::ServiceLevel::BUSINESS, accountLimits,
        oldTimestamp);

    const QString authToken = QStringLiteral("authToken");

    EXPECT_CALL(
        *m_mockAuthenticationInfoProvider,
        authenticateAccount(
            m_account, IAuthenticationInfoProvider::Mode::Cache))
        .WillRepeatedly(
            [&](const Account &, const IAuthenticationInfoProvider::Mode) {
                auto info = std::make_shared<AuthenticationInfo>();
                info->m_userId = m_account.id();
                info->m_authToken = authToken;
                return threading::makeReadyFuture<IAuthenticationInfoPtr>(
                    std::move(info));
            });

    EXPECT_CALL(
        *m_mockUserStore,
        getAccountLimitsAsync(qevercloud::ServiceLevel::BUSINESS, _))
        .WillOnce([&](const qevercloud::ServiceLevel,
                      const qevercloud::IRequestContextPtr & ctx) {
            EXPECT_EQ(ctx->authenticationToken(), authToken);
            return threading::makeReadyFuture<qevercloud::AccountLimits>(
                accountLimits);
        });

    auto future = accountLimitsProvider->accountLimits(
        qevercloud::ServiceLevel::BUSINESS);

    ASSERT_TRUE(future.isFinished());

    ASSERT_EQ(future.resultCount(), 1);
    const auto result = future.result();
    EXPECT_EQ(result, accountLimits);

    checkAccountLimitsPersistence(
        m_account, qevercloud::ServiceLevel::BUSINESS, accountLimits);
}

} // namespace quentier::synchronization::tests
