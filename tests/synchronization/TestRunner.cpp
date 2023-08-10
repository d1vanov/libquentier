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

#include "TestRunner.h"

#include "FakeAuthenticator.h"
#include "FakeKeychainService.h"
#include "FakeSyncStateStorage.h"
#include "NoteStoreServer.h"
#include "SyncEventsCollector.h"
#include "TestScenarioData.h"
#include "UserStoreServer.h"

#include <synchronization/types/AuthenticationInfo.h>

#include <quentier/local_storage/Factory.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/threading/Factory.h>

#include <qevercloud/types/builders/UserBuilder.h>

#include <QDateTime>
#include <QDebug>
#include <QTest>
#include <QTextStream>

#include <array>
#include <cstdio>

namespace quentier::synchronization::tests {

namespace {

inline void messageHandler(
    QtMsgType type, const QMessageLogContext & /* context */,
    const QString & message)
{
    if (type != QtDebugMsg) {
        QTextStream(stderr) << message << "\n";
    }
}

[[nodiscard]] QList<QNetworkCookie> generateUserStoreCookies()
{
    constexpr int cookieCount = 5;

    QList<QNetworkCookie> result;
    result.reserve(cookieCount);

    for (int i = 0; i < cookieCount; ++i) {
        result << QNetworkCookie{
            QString::fromUtf8("sampleCookieName_%1").arg(i + 1).toUtf8(),
            QString::fromUtf8("sampleCookieValue_%1").arg(i + 1).toUtf8()};
    }

    return result;
}

} // namespace

TestRunner::TestRunner(QObject * parent, threading::QThreadPoolPtr threadPool) :
    QObject(parent), m_fakeAuthenticator{std::make_shared<FakeAuthenticator>()},
    m_fakeKeychainService{std::make_shared<FakeKeychainService>()},
    m_threadPool{
        threadPool ? std::move(threadPool) : threading::globalThreadPool()}
{
    Q_ASSERT(m_threadPool);
}

TestRunner::~TestRunner() = default;

void TestRunner::init()
{
    QuentierRestartLogging();

    const QString shardId = QStringLiteral("shardId");

    m_testAccount = Account(
        m_testAccount.name(), Account::Type::Evernote, m_testAccount.id() + 1,
        Account::EvernoteAccountType::Free, QStringLiteral("www.evernote.com"),
        shardId);

    m_tempDir.emplace();
    m_localStorage = local_storage::createSqliteLocalStorage(
        m_testAccount, QDir{m_tempDir->path()}, m_threadPool);

    const QString authToken = QStringLiteral("AuthToken");
    const QString webApiUrlPrefix = QStringLiteral("webApiUrlPrefix");
    const auto now = QDateTime::currentMSecsSinceEpoch();

    const auto userStoreCookies = generateUserStoreCookies();
    m_noteStoreServer = new NoteStoreServer(authToken, userStoreCookies, this);

    auto authenticationInfo = [&] {
        auto info = std::make_shared<AuthenticationInfo>();
        info->m_userId = m_testAccount.id();
        info->m_authToken = authToken;
        info->m_authTokenExpirationTime = now + 999999999999;
        info->m_authenticationTime = now;
        info->m_shardId = shardId;
        info->m_webApiUrlPrefix = webApiUrlPrefix;
        info->m_noteStoreUrl = QString::fromUtf8("http://localhost:%1")
                                   .arg(m_noteStoreServer->port());
        return info;
    }();

    m_fakeAuthenticator->putAccountAuthInfo(
        m_testAccount, std::move(authenticationInfo));

    m_userStoreServer = new UserStoreServer(authToken, userStoreCookies, this);
    m_userStoreServer->setEdamVersionMajor(qevercloud::EDAM_VERSION_MAJOR);
    m_userStoreServer->setEdamVersionMinor(qevercloud::EDAM_VERSION_MINOR);

    m_userStoreServer->putUser(
        authToken,
        qevercloud::UserBuilder{}
            .setId(m_testAccount.id())
            .setUsername(m_testAccount.name())
            .setName(m_testAccount.displayName())
            .setCreated(now)
            .setUpdated(now)
            .setServiceLevel(qevercloud::ServiceLevel::BASIC)
            .build());

    m_fakeSyncStateStorage = new FakeSyncStateStorage(this);
}

void TestRunner::cleanup()
{
    m_localStorage->notifier()->disconnect();
    m_localStorage.reset();

    m_noteStoreServer->disconnect();
    m_noteStoreServer->deleteLater();
    m_noteStoreServer = nullptr;

    m_userStoreServer->disconnect();
    m_userStoreServer->deleteLater();
    m_userStoreServer = nullptr;

    m_fakeSyncStateStorage->disconnect();
    m_fakeSyncStateStorage->deleteLater();
    m_fakeSyncStateStorage = nullptr;

    m_fakeAuthenticator->clear();
    m_fakeKeychainService->clear();
}

void TestRunner::initTestCase()
{
    qInstallMessageHandler(messageHandler);
}

void TestRunner::cleanupTestCase() {}

void TestRunner::runTestScenario()
{
    // TODO: implement
    // QFETCH(TestScenarioData, testScenarioData);
}

void TestRunner::runTestScenario_data()
{
    QTest::addColumn<TestScenarioData>("testScenarioData");

    using namespace std::string_view_literals;

    static const std::array testScenarioData{
        TestScenarioData{
            note_store::DataItemTypes{} |
                note_store::DataItemType::SavedSearch, // serverDataItemTypes
            note_store::ItemGroups{} |
                note_store::ItemGroup::Base, // serverItemGroups
            note_store::ItemSources{} |
                note_store::ItemSource::UserOwnAccount, // serverItemSources
            note_store::DataItemTypes{},                // localDataItemTypes
            note_store::ItemGroups{},                   // localItemGroups
            note_store::ItemSources{},                  // localItemSources
            StopSynchronizationError{std::monostate{}}, // stopSyncError
            false,                                      // expectFailure
            true,  // expectSomeUserOwnSyncChunks
            false, // expectSomeLinkedNotebooksSyncChunks
            false, // expectSomeUserOwnNotes
            false, // expectSomeUserOwnResources
            false, // expectSomeLinkedNotebookNotes
            false, // expectSomeUserOwnDataSent
            false, // expectSomeLinkedNotebookDataSent
            "Full sync with only saved searches"sv, // name
        },
    };

    for (const auto & scenarioData: testScenarioData) {
        QTest::newRow(scenarioData.name.data()) << scenarioData;
    }
}

} // namespace quentier::synchronization::tests
