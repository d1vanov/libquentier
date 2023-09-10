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
#include "Setup.h"
#include "SyncEventsCollector.h"
#include "TestData.h"
#include "TestScenarioData.h"
#include "UserStoreServer.h"

#include <synchronization/types/AuthenticationInfo.h>

#include <quentier/local_storage/Factory.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/Factory.h>
#include <quentier/synchronization/ISyncChunksDataCounters.h>
#include <quentier/synchronization/ISynchronizer.h>
#include <quentier/synchronization/types/IAuthenticationInfoBuilder.h>
#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/synchronization/types/IDownloadResourcesStatus.h>
#include <quentier/synchronization/types/ISyncResult.h>
#include <quentier/threading/Factory.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/types/builders/SyncStateBuilder.h>
#include <qevercloud/types/builders/UserBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QDateTime>
#include <QDebug>
#include <QTest>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>

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
            QString::fromUtf8("webSampleCookieName_%1_PreUserGuid")
                .arg(i + 1)
                .toUtf8(),
            QString::fromUtf8("sampleCookieValue_%1").arg(i + 1).toUtf8()};
    }

    return result;
}

[[nodiscard]] bool empty(const ISyncChunksDataCounters & counters) noexcept
{
    return counters.totalSavedSearches() != 0 ||
        counters.totalExpungedSavedSearches() != 0 ||
        counters.totalTags() != 0 || counters.totalExpungedTags() != 0 ||
        counters.totalLinkedNotebooks() != 0 ||
        counters.totalExpungedLinkedNotebooks() != 0 ||
        counters.totalNotebooks() != 0 ||
        counters.totalExpungedNotebooks() != 0;
}

[[nodiscard]] bool empty(const IDownloadNotesStatus & status) noexcept
{
    return status.totalNewNotes() != 0 || status.totalUpdatedNotes() != 0 ||
        status.totalExpungedNotes() != 0;
}

[[nodiscard]] bool empty(const IDownloadResourcesStatus & status) noexcept
{
    return status.totalNewResources() != 0 ||
        status.totalUpdatedResources() != 0;
}

template <class T>
void copyLocalFields(const T & source, T & dest)
{
    dest.setLocalId(source.localId());
    dest.setLocallyModified(source.isLocallyModified());
    dest.setLocallyFavorited(source.isLocallyFavorited());
    dest.setLocalOnly(source.isLocalOnly());
    dest.setLocalData(source.localData());
}

template <>
void copyLocalFields(
    const qevercloud::LinkedNotebook & source,
    qevercloud::LinkedNotebook & dest)
{
    dest.setLocallyModified(source.isLocallyModified());
    dest.setLocallyFavorited(source.isLocallyFavorited());
    dest.setLocalOnly(source.isLocalOnly());
    dest.setLocalData(source.localData());
}

template <class T>
[[nodiscard]] bool compareLists(
    const QList<T> & lhs, const QList<T> & rhs, QList<T> & onlyLhs,
    QList<T> & onlyRhs, QList<std::pair<T, T>> & diffs)
{
    QSet<qevercloud::Guid> processedRhsGuids;
    for (const auto & lhsItem: qAsConst(lhs)) {
        Q_ASSERT(lhsItem.guid());
        const auto it = std::find_if(
            rhs.constBegin(), rhs.constEnd(), [&lhsItem](const T & rhsItem) {
                return lhsItem.guid() == rhsItem.guid();
            });
        if (it == rhs.constEnd()) {
            onlyLhs << lhsItem;
            continue;
        }

        Q_ASSERT(it->guid());
        processedRhsGuids << it->guid().value();

        auto rhsItemCopy = *it;
        copyLocalFields(lhsItem, rhsItemCopy);

        if (lhsItem != rhsItemCopy) {
            diffs << std::make_pair(lhsItem, *it);
        }
    }

    for (const auto & rhsItem: qAsConst(rhs)) {
        Q_ASSERT(rhsItem.guid());
        if (processedRhsGuids.contains(*rhsItem.guid())) {
            continue;
        }

        const auto it = std::find_if(
            lhs.constBegin(), lhs.constEnd(), [&rhsItem](const T & lhsItem) {
                return rhsItem.guid() == lhsItem.guid();
            });
        if (it == lhs.constEnd()) {
            onlyRhs << rhsItem;
        }
    }

    return onlyLhs.isEmpty() && onlyRhs.isEmpty() && diffs.isEmpty();
}

template <class T>
QString composeDifferentListsErrorMessage(
    const QList<T> & onlyLhs, const QList<T> & onlyRhs,
    const QList<std::pair<T, T>> & diffs)
{
    QString res;
    QTextStream strm{&res};

    strm << "Found differences in item lists:\n\n";

    strm << "Items present only on the local side:\n\n";
    for (const auto & item: qAsConst(onlyLhs)) {
        strm << item << "\n\n";
    }

    strm << "Items present only on the server side:\n\n";
    for (const auto & item: qAsConst(onlyRhs)) {
        strm << item << "\n\n";
    }

    strm << "Items which differ from each other:\n\n";
    for (const auto & pair: qAsConst(diffs)) {
        strm << pair.first << "\n";
        strm << pair.second << "\n\n";
    }

    return res;
}

template <class T>
struct ItemListsChecker
{
    using NoteStoreServerItemsProvider = std::function<QList<T>()>;
    using LocalStorageItemsProvider = std::function<QFuture<QList<T>>()>;

    static bool check(
        const NoteStoreServerItemsProvider & noteStoreServerItemsProvider,
        const LocalStorageItemsProvider & localStorageItemsProvider,
        QString & errorDescription)
    {
        Q_ASSERT(noteStoreServerItemsProvider);
        Q_ASSERT(localStorageItemsProvider);

        QList<T> onlyServerItems;
        QList<T> onlyLocalItems;
        QList<std::pair<T, T>> differentItems;

        const auto serverItems = noteStoreServerItemsProvider();
        auto localItemsFuture = localStorageItemsProvider();
        localItemsFuture.waitForFinished();
        Q_ASSERT(localItemsFuture.resultCount() == 1);
        const auto localItems = localItemsFuture.result();

        bool res = compareLists(
            localItems, serverItems, onlyLocalItems, onlyServerItems,
            differentItems);
        if (!res) {
            errorDescription = composeDifferentListsErrorMessage(
                onlyLocalItems, onlyServerItems, differentItems);
            return false;
        }

        return true;
    }
};

[[nodiscard]] bool checkNoteStoreServerAndLocalStorageContentsEquality(
    const NoteStoreServer & noteStoreServer,
    const local_storage::ILocalStorage & localStorage,
    QString & errorDescription)
{
    if (!ItemListsChecker<qevercloud::SavedSearch>::check(
            [&noteStoreServer] {
                return noteStoreServer.savedSearches().values();
            },
            [&localStorage] { return localStorage.listSavedSearches(); },
            errorDescription))
    {
        return false;
    }

    if (!ItemListsChecker<qevercloud::LinkedNotebook>::check(
            [&noteStoreServer] {
                return noteStoreServer.linkedNotebooks().values();
            },
            [&localStorage] { return localStorage.listLinkedNotebooks(); },
            errorDescription))
    {
        return false;
    }

    if (!ItemListsChecker<qevercloud::Notebook>::check(
            [&noteStoreServer] { return noteStoreServer.notebooks().values(); },
            [&localStorage] { return localStorage.listNotebooks(); },
            errorDescription))
    {
        return false;
    }

    if (!ItemListsChecker<qevercloud::Tag>::check(
            [&noteStoreServer] { return noteStoreServer.tags().values(); },
            [&localStorage] { return localStorage.listTags(); },
            errorDescription))
    {
        return false;
    }

    if (!ItemListsChecker<qevercloud::Note>::check(
            [&noteStoreServer] {
                // TODO: might need to merge modified resources within
                // NoteStoreServer into their corresponding notes here
                return noteStoreServer.notes().values();
            },
            [&localStorage] {
                return localStorage.listNotes(
                    local_storage::ILocalStorage::FetchNoteOptions{} |
                    local_storage::ILocalStorage::FetchNoteOption::
                        WithResourceMetadata |
                    local_storage::ILocalStorage::FetchNoteOption::
                        WithResourceBinaryData);
            },
            errorDescription))
    {
        return false;
    }

    return true;
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
        QStringLiteral("Sync integrational tests"), Account::Type::Evernote,
        m_testAccount.id() + 1, Account::EvernoteAccountType::Free,
        QStringLiteral("www.evernote.com"), shardId);

    m_tempDir.emplace();
    m_localStorage = local_storage::createSqliteLocalStorage(
        m_testAccount, QDir{m_tempDir->path()}, m_threadPool);

    const QString authToken = QStringLiteral("AuthToken");
    const QString webApiUrlPrefix = QStringLiteral("webApiUrlPrefix");
    const auto now = QDateTime::currentMSecsSinceEpoch();

    const auto userStoreCookies = generateUserStoreCookies();
    m_noteStoreServer = new NoteStoreServer(authToken, userStoreCookies, this);

    auto authenticationInfo = [&] {
        auto builder = createAuthenticationInfoBuilder();
        return builder->setUserId(m_testAccount.id())
            .setAuthToken(authToken)
            .setAuthTokenExpirationTime(now + 999999999999)
            .setAuthenticationTime(now)
            .setShardId(shardId)
            .setWebApiUrlPrefix(webApiUrlPrefix)
            .setNoteStoreUrl(QString::fromUtf8("http://127.0.0.1:%1")
                                 .arg(m_noteStoreServer->port()))
            .setUserStoreCookies(userStoreCookies)
            .build();
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

    m_fakeSyncStateStorage = std::shared_ptr<FakeSyncStateStorage>(
        new FakeSyncStateStorage(this), [](FakeSyncStateStorage * storage) {
            storage->disconnect();
            storage->deleteLater();
        });

    m_syncEventsCollector = new SyncEventsCollector(this);
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

    m_syncEventsCollector->disconnect();
    m_syncEventsCollector->deleteLater();
    m_syncEventsCollector = nullptr;

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
    QFETCH(TestScenarioData, testScenarioData);

    TestData testData;
    setupTestData(
        testScenarioData.serverDataItemTypes, testScenarioData.serverItemGroups,
        testScenarioData.serverItemSources,
        testScenarioData.serverExpungedDataItemTypes,
        testScenarioData.serverExpungedDataItemSources, testData);

    setupNoteStoreServer(testData, *m_noteStoreServer);

    setupLocalStorage(
        testData, testScenarioData.localDataItemTypes,
        testScenarioData.localItemGroups, testScenarioData.localItemSources,
        *m_localStorage);

    auto localSyncState = setupSyncState(
        testData, testScenarioData.localDataItemTypes,
        testScenarioData.localItemGroups, testScenarioData.localItemSources);
    QVERIFY(localSyncState);

    m_fakeSyncStateStorage->setSyncState(
        m_testAccount, std::move(localSyncState));

    auto serverSyncState = setupSyncState(
        testData, testScenarioData.serverDataItemTypes,
        testScenarioData.serverItemGroups, testScenarioData.serverItemSources);
    QVERIFY(serverSyncState);

    const auto now = QDateTime::currentMSecsSinceEpoch();

    m_noteStoreServer->putUserOwnSyncState(
        qevercloud::SyncStateBuilder{}
            .setUpdateCount(serverSyncState->userDataUpdateCount())
            .setUserLastUpdated(serverSyncState->userDataLastSyncTime())
            .setFullSyncBefore(
                serverSyncState->userDataLastSyncTime() + 9999999999)
            .setCurrentTime(now)
            .build());

    const auto linkedNotebookUpdateCounts =
        serverSyncState->linkedNotebookUpdateCounts();

    const auto linkedNotebookLastSyncTimes =
        serverSyncState->linkedNotebookLastSyncTimes();

    for (const auto it:
         qevercloud::toRange(qAsConst(linkedNotebookUpdateCounts))) {
        const auto lastSyncTime = linkedNotebookLastSyncTimes.value(it.key());
        m_noteStoreServer->putLinkedNotebookSyncState(
            it.key(),
            qevercloud::SyncStateBuilder{}
                .setUpdateCount(it.value())
                .setCurrentTime(now)
                .setUserLastUpdated(lastSyncTime)
                .setFullSyncBefore(lastSyncTime + 9999999999)
                .build());
    }

    const QUrl userStoreUrl =
        QUrl::fromEncoded(QString::fromUtf8("http://127.0.0.1:%1")
                              .arg(m_userStoreServer->port())
                              .toUtf8());
    QVERIFY(userStoreUrl.isValid());

    const QString syncPersistenceDirPath =
        m_tempDir->path() + QStringLiteral("/syncPersistence");

    QDir syncPersistenceDir{syncPersistenceDirPath};
    if (!syncPersistenceDir.exists()) {
        QVERIFY(syncPersistenceDir.mkpath(syncPersistenceDirPath));
    }

    auto synchronizer = createSynchronizer(
        userStoreUrl, syncPersistenceDir, m_fakeAuthenticator,
        m_fakeSyncStateStorage, m_fakeKeychainService);

    auto canceler = std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResultPair = synchronizer->synchronizeAccount(
        m_testAccount, m_localStorage, canceler);

    m_syncEventsCollector->connectToNotifier(syncResultPair.second);

    bool caughtException = false;
    try {
        while (!syncResultPair.first.isFinished()) {
            QCoreApplication::processEvents();
        }
    }
    catch (...) {
        caughtException = true;
    }

    {
        const char * errorMessage = nullptr;
        QVERIFY2(
            m_syncEventsCollector->checkProgressNotificationsOrder(
                errorMessage),
            errorMessage);
    }

    QVERIFY2(
        !m_syncEventsCollector->userOwnSyncChunksDownloadProgressMessages()
                .isEmpty() == testScenarioData.expectSomeUserOwnSyncChunks,
        "User own sync chunks download progress messages count doesn't "
        "correspond to the expectation");

    QVERIFY2(
        !m_syncEventsCollector
                ->linkedNotebookSyncChunksDownloadProgressMessages()
                .isEmpty() ==
            testScenarioData.expectSomeLinkedNotebooksSyncChunks,
        "Linked notebook sync chunks download progress messages count doesn't "
        "correspond to the expectation");

    QVERIFY2(
        !m_syncEventsCollector->userOwnNoteDownloadProgressMessages()
                .isEmpty() == testScenarioData.expectSomeUserOwnNotes,
        "User own notes download progress messages count doesn't correspond "
        "to the expectation");

    QVERIFY2(
        !m_syncEventsCollector->userOwnResourceDownloadProgressMessages()
                .isEmpty() == testScenarioData.expectSomeUserOwnResources,
        "User own resources download progress messages count doesn't "
        "correspond to the expectation");

    QVERIFY2(
        !m_syncEventsCollector->linkedNotebookNoteDownloadProgressMessages()
                .isEmpty() == testScenarioData.expectSomeLinkedNotebookNotes,
        "Linked notebook notes download progress messages count doesn't "
        "correspond to the expectation");

    QVERIFY2(
        !m_syncEventsCollector->linkedNotebookResourceDownloadProgressMessages()
                .isEmpty() ==
            testScenarioData.expectSomeLinkedNotebookResources,
        "Linked notebook resources download progress messages count doesn't "
        "correspond to the expectation");

    QVERIFY2(
        !m_syncEventsCollector->userOwnSendStatusMessages().isEmpty() ==
            testScenarioData.expectSomeUserOwnDataSent,
        "User own sent data messages count doesn't correspond to the "
        "expectation");

    QVERIFY2(
        !m_syncEventsCollector->linkedNotebookSendStatusMessages().isEmpty() ==
            testScenarioData.expectSomeLinkedNotebookDataSent,
        "Linked notebook sent data messages count doesn't correspond to the "
        "expectation");

    if (testScenarioData.expectFailure) {
        QVERIFY2(
            caughtException, "Sync which was expected to fail did not fail");
        return;
    }

    QVERIFY2(
        syncResultPair.first.resultCount() == 1, "Empty sync result future");

    const auto syncResult = syncResultPair.first.result();
    QVERIFY(syncResult);

    // TODO: check whether the first synchronization attempt should yield stop
    // synchronization error. If so, check the presence of the error and restart
    // the sync, the next attempt should be successful.

    if (testScenarioData.expectSomeUserOwnSyncChunks ||
        testScenarioData.expectSomeLinkedNotebooksSyncChunks)
    {
        const auto syncState = syncResult->syncState();
        QVERIFY2(syncState, "Null pointer to sync state in sync result");

        if (testScenarioData.expectSomeUserOwnSyncChunks) {
            QVERIFY2(
                syncState->userDataLastSyncTime() > 0,
                "Detected zero last sync time for user own account in sync "
                "state");
        }

        QVERIFY2(
            syncState->userDataUpdateCount() ==
                m_noteStoreServer->currentUserOwnMaxUsn(),
            "Max user own USN in sync state doesn't correspond to the USN "
            "recorded by note store server");

        const auto linkedNotebookUpdateCounts =
            syncState->linkedNotebookUpdateCounts();

        for (const auto it:
             qevercloud::toRange(qAsConst(linkedNotebookUpdateCounts))) {
            const auto serverMaxUsn =
                m_noteStoreServer->currentLinkedNotebookMaxUsn(it.key());

            QVERIFY2(
                serverMaxUsn,
                "Could not find max USN for one of linked notebook guids "
                "from sync state in sync result");

            QVERIFY2(
                *serverMaxUsn == it.value(),
                "Max USN for one of linked notebooks doesn't match update "
                "count for this linked notebook on the server");
        }

        const auto linkedNotebookLastSyncTimes =
            syncState->linkedNotebookLastSyncTimes();

        QVERIFY(
            linkedNotebookLastSyncTimes.size() ==
            linkedNotebookUpdateCounts.size());

        for (const auto it:
             qevercloud::toRange(qAsConst(linkedNotebookLastSyncTimes))) {
            QVERIFY2(
                it.value() > 0,
                "Detected zero last sync time in sync state for some linked "
                "notebook");
        }
    }

    const auto userOwnCounters =
        syncResult->userAccountSyncChunksDataCounters();

    QVERIFY(
        (userOwnCounters && !empty(*userOwnCounters)) ==
        testScenarioData.expectSomeUserOwnSyncChunks);

    const auto userOwnNotesStatus =
        syncResult->userAccountDownloadNotesStatus();

    QVERIFY(
        (userOwnNotesStatus && !empty(*userOwnNotesStatus)) ==
        testScenarioData.expectSomeUserOwnNotes);

    const auto userOwnResourcesStatus =
        syncResult->userAccountDownloadResourcesStatus();

    QVERIFY(
        (userOwnResourcesStatus && !empty(*userOwnResourcesStatus)) ==
        testScenarioData.expectSomeUserOwnResources);

    const auto userOwnSendStatus = syncResult->userAccountSendStatus();
    QVERIFY(
        static_cast<bool>(userOwnSendStatus) ==
        testScenarioData.expectSomeUserOwnDataSent);

    const auto linkedNotebookSendStatuses =
        syncResult->linkedNotebookSendStatuses();
    QVERIFY(
        !linkedNotebookSendStatuses.isEmpty() ==
        testScenarioData.expectSomeLinkedNotebookDataSent);

    {
        QString errorMessage;
        const bool res = checkNoteStoreServerAndLocalStorageContentsEquality(
            *m_noteStoreServer, *m_localStorage, errorMessage);
        const QByteArray errorMessageData = errorMessage.toLatin1();
        QVERIFY2(res, errorMessageData.data());
    }
}

void TestRunner::runTestScenario_data()
{
    QTest::addColumn<TestScenarioData>("testScenarioData");

    using namespace std::string_view_literals;

    static const std::array testScenarioData{
        TestScenarioData{
            DataItemTypes{} | DataItemType::SavedSearch, // serverDataItemTypes
            ItemGroups{} | ItemGroup::Base,              // serverItemGroups
            ItemSources{} | ItemSource::UserOwnAccount,  // serverItemSources
            DataItemTypes{}, // serverExpungedDataItemTypes
            ItemSources{},   // serverExpungedDataItemSources
            DataItemTypes{}, // localDataItemTypes
            ItemGroups{},    // localItemGroups
            ItemSources{},   // localItemSources
            StopSynchronizationError{std::monostate{}}, // stopSyncError
            false,                                      // expectFailure
            true,  // expectSomeUserOwnSyncChunks
            false, // expectSomeLinkedNotebooksSyncChunks
            false, // expectSomeUserOwnNotes
            false, // expectSomeUserOwnResources
            false, // expectSomeLinkedNotebookNotes
            false, // expectSomeLinkedNotebookResources
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
