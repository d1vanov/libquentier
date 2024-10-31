/*
 * Copyright 2023-2024 Dmitry Ivanov
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
#include "TestScenarios.h"
#include "UserStoreServer.h"

#include <quentier/local_storage/Factory.h>
#include <quentier/local_storage/ILocalStorage.h>
#include <quentier/local_storage/ILocalStorageNotifier.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/synchronization/Factory.h>
#include <quentier/synchronization/ISynchronizer.h>
#include <quentier/synchronization/types/IAuthenticationInfoBuilder.h>
#include <quentier/synchronization/types/IDownloadNotesStatus.h>
#include <quentier/synchronization/types/IDownloadResourcesStatus.h>
#include <quentier/synchronization/types/ISendStatus.h>
#include <quentier/synchronization/types/ISyncChunksDataCounters.h>
#include <quentier/synchronization/types/ISyncResult.h>
#include <quentier/synchronization/types/ISyncState.h>
#include <quentier/threading/Factory.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/cancelers/ManualCanceler.h>

#include <qevercloud/RequestContextBuilder.h>
#include <qevercloud/types/builders/SyncStateBuilder.h>
#include <qevercloud/types/builders/UserBuilder.h>
#include <qevercloud/utility/ToRange.h>

#include <QDateTime>
#include <QDebug>
#include <QTest>
#include <QTextStream>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <type_traits>
#include <utility>

namespace quentier::synchronization::tests {

namespace {

using namespace std::string_view_literals;

constexpr auto gConnectionTimeoutEnvVarKey = "CONNECTION_TIMEOUT"sv;

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
    return counters.totalSavedSearches() == 0 &&
        counters.totalExpungedSavedSearches() == 0 &&
        counters.totalTags() == 0 && counters.totalExpungedTags() == 0 &&
        counters.totalLinkedNotebooks() == 0 &&
        counters.totalExpungedLinkedNotebooks() == 0 &&
        counters.totalNotebooks() == 0 &&
        counters.totalExpungedNotebooks() == 0;
}

[[nodiscard]] bool empty(const IDownloadNotesStatus & status) noexcept
{
    return status.totalNewNotes() == 0 && status.totalUpdatedNotes() == 0 &&
        status.totalExpungedNotes() == 0;
}

[[nodiscard]] bool empty(const IDownloadResourcesStatus & status) noexcept
{
    return status.totalNewResources() == 0 &&
        status.totalUpdatedResources() == 0;
}

[[nodiscard]] bool empty(const ISendStatus & status) noexcept
{
    return status.totalAttemptedToSendNotes() == 0 &&
        status.totalAttemptedToSendNotebooks() == 0 &&
        status.totalAttemptedToSendSavedSearches() == 0 &&
        status.totalAttemptedToSendTags() == 0 &&
        status.totalSuccessfullySentNotes() == 0 &&
        status.failedToSendNotes().isEmpty() &&
        status.totalSuccessfullySentNotebooks() == 0 &&
        status.failedToSendNotebooks().isEmpty() &&
        status.totalSuccessfullySentSavedSearches() == 0 &&
        status.failedToSendSavedSearches().isEmpty() &&
        status.totalSuccessfullySentTags() == 0 &&
        status.failedToSendTags().isEmpty() &&
        std::holds_alternative<std::monostate>(
               status.stopSynchronizationError());
}

template <class T>
void copyLocalFields(const T & source, T & dest)
{
    if constexpr (!std::is_same_v<std::decay_t<T>, qevercloud::LinkedNotebook>)
    {
        dest.setLocalId(source.localId());
    }

    dest.setLocallyModified(source.isLocallyModified());
    dest.setLocallyFavorited(source.isLocallyFavorited());
    dest.setLocalOnly(source.isLocalOnly());
    dest.setLocalData(source.localData());

    if constexpr (std::is_same_v<std::decay_t<T>, qevercloud::Tag>) {
        dest.setParentTagLocalId(source.parentTagLocalId());
    }
    else if constexpr (std::is_same_v<std::decay_t<T>, qevercloud::Note>) {
        dest.setNotebookLocalId(source.notebookLocalId());
        dest.setTagLocalIds(source.tagLocalIds());
        if (source.resources()) {
            Q_ASSERT(dest.resources());
            Q_ASSERT(dest.resources()->size() == source.resources()->size());
            for (int i = 0; i < source.resources()->size(); ++i) {
                const auto & sourceResource = (*source.resources())[i];
                auto & destResource = (*dest.mutableResources())[i];
                copyLocalFields(sourceResource, destResource);
            }
        }
    }
    else if constexpr (std::is_same_v<std::decay_t<T>, qevercloud::Resource>) {
        dest.setNoteLocalId(source.noteLocalId());
    }
}

template <class T>
bool compareItems(const T & lhs, const T & rhs)
{
    if constexpr (std::is_same_v<std::decay_t<T>, qevercloud::Note>) {
        const auto createSortedNote = [](const qevercloud::Note & note) {
            auto noteCopy = note;
            if (noteCopy.tagGuids() && !noteCopy.tagGuids()->isEmpty()) {
                QList<qevercloud::Guid> tagGuids = *noteCopy.tagGuids();
                std::sort(tagGuids.begin(), tagGuids.end());
                noteCopy.setTagGuids(std::move(tagGuids));
            }

            if (noteCopy.resources() && !noteCopy.resources()->isEmpty()) {
                QList<qevercloud::Resource> resources = *note.resources();
                std::sort(
                    resources.begin(), resources.end(),
                    [](const qevercloud::Resource & lhs,
                       const qevercloud::Resource & rhs) {
                        return lhs.guid() < rhs.guid();
                    });
                noteCopy.setResources(std::move(resources));
            }

            return noteCopy;
        };

        const auto lhsCopy = createSortedNote(lhs);
        const auto rhsCopy = createSortedNote(rhs);
        return lhsCopy == rhsCopy;
    }

    return lhs == rhs;
}

template <class T>
[[nodiscard]] bool compareLists(
    const QList<T> & lhs, const QList<T> & rhs, QList<T> & onlyLhs,
    QList<T> & onlyRhs, QList<std::pair<T, T>> & diffs)
{
    QSet<qevercloud::Guid> processedRhsGuids;
    for (const auto & lhsItem: std::as_const(lhs)) {
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

        if (!compareItems(lhsItem, rhsItemCopy)) {
            diffs << std::make_pair(lhsItem, rhsItemCopy);
        }
    }

    for (const auto & rhsItem: std::as_const(rhs)) {
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
    for (const auto & item: std::as_const(onlyLhs)) {
        strm << item << "\n\n";
    }

    strm << "Items present only on the server side:\n\n";
    for (const auto & item: std::as_const(onlyRhs)) {
        strm << item << "\n\n";
    }

    strm << "Items which differ from each other:\n\n";
    for (const auto & pair: std::as_const(diffs)) {
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
            [&noteStoreServer] {
                auto tags = noteStoreServer.tags().values();
                auto notes = noteStoreServer.notes().values();
                // Will filter out server tags which are not referenced by any
                // notes because during the last step of downloading step of the
                // sync such notes should be cleared out of the local storage
                for (auto it = tags.begin(); it != tags.end();) {
                    if (!it->linkedNotebookGuid()) {
                        ++it;
                        continue;
                    }
                    Q_ASSERT(it->guid());
                    bool foundNoteReference = false;
                    for (const auto & note: std::as_const(notes)) {
                        if (note.tagGuids() &&
                            note.tagGuids()->contains(*it->guid())) {
                            foundNoteReference = true; // NOLINT
                            break;
                        }
                    }
                    if (foundNoteReference) {
                        ++it;
                        continue;
                    }

                    it = tags.erase(it); // NOLINT
                }
                return tags;
            },
            [&localStorage] { return localStorage.listTags(); },
            errorDescription))
    {
        return false;
    }

    if (!ItemListsChecker<qevercloud::Note>::check(
            [&noteStoreServer] {
                // There are some tricks related to notes and resources in
                // NoteStoreServer:
                // 1. There are two places where resources are stored in
                //    NoteStoreServer: resources which are embedded into notes
                //    and resources which are stored in a separate container
                //    within NoteStoreServer
                // 2. Resources embedded into notes lack binary data which
                //    is needed to perform the full comparison
                // 3. Resources stored separately from notes might be "newer"
                //    than resources stored within notes if, for example,
                //    incremental sync conditions were set up and thus
                //    modified resources were sent to the client separately
                //    from notes owning these resources
                auto notes = noteStoreServer.notes().values();
                auto resources = noteStoreServer.resources();

                for (auto & note: notes) {
                    if (!note.resources() || note.resources()->isEmpty()) {
                        continue;
                    }

                    for (auto & resource: *note.mutableResources()) {
                        Q_ASSERT(resource.guid());
                        const auto it = resources.constFind(*resource.guid());
                        Q_ASSERT(it != resources.constEnd());
                        resource = it.value(); // NOLINT
                    }
                }

                return notes;
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

[[nodiscard]] bool checkNoLocallyModifiedObjectsInLocalStorageAfterSync(
    const local_storage::ILocalStorage & localStorage,
    QString & errorDescription)
{
    const auto listSavedSearchesOptions = [] {
        local_storage::ILocalStorage::ListSavedSearchesOptions options;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    auto listSavedSearchesFuture =
        localStorage.listSavedSearches(listSavedSearchesOptions);
    listSavedSearchesFuture.waitForFinished();
    if (Q_UNLIKELY(listSavedSearchesFuture.resultCount() != 1)) {
        errorDescription = QString::fromUtf8(
                               "Failed to list saved searches from local "
                               "storage, future result count = %1")
                               .arg(listSavedSearchesFuture.resultCount());
        return false;
    }

    const auto savedSearches = listSavedSearchesFuture.result();
    for (const auto & savedSearch: std::as_const(savedSearches)) {
        if (Q_UNLIKELY(savedSearch.isLocallyModified())) {
            errorDescription =
                QString::fromUtf8(
                    "Found locally modified saved search after sync: %1")
                    .arg(savedSearch.toString());
            return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////

    const auto listNotebooksOptions = [] {
        local_storage::ILocalStorage::ListNotebooksOptions options;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    auto listNotebooksFuture = localStorage.listNotebooks(listNotebooksOptions);
    listNotebooksFuture.waitForFinished();
    if (Q_UNLIKELY(listNotebooksFuture.resultCount() != 1)) {
        errorDescription = QString::fromUtf8(
                               "Failed to list notebooks from local storage, "
                               "future result count = %1")
                               .arg(listNotebooksFuture.resultCount());
        return false;
    }

    const auto notebooks = listNotebooksFuture.result();
    for (const auto & notebook: std::as_const(notebooks)) {
        if (Q_UNLIKELY(notebook.isLocallyModified())) {
            errorDescription =
                QString::fromUtf8(
                    "Found locally modified notebook after sync: %1")
                    .arg(notebook.toString());
            return false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////

    const auto listNotesOptions = [] {
        local_storage::ILocalStorage::ListNotesOptions options;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    const auto fetchNoteOptions =
        local_storage::ILocalStorage::FetchNoteOptions{} |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceMetadata |
        local_storage::ILocalStorage::FetchNoteOption::WithResourceBinaryData;

    auto listNotesFuture =
        localStorage.listNotes(fetchNoteOptions, listNotesOptions);
    listNotesFuture.waitForFinished();
    if (Q_UNLIKELY(listNotesFuture.resultCount() != 1)) {
        errorDescription = QString::fromUtf8(
                               "Failed to list notes from local storage, "
                               "future result count = %1")
                               .arg(listNotesFuture.resultCount());
        return false;
    }

    const auto notes = listNotesFuture.result();
    for (const auto & note: std::as_const(notes)) {
        if (Q_UNLIKELY(note.isLocallyModified())) {
            errorDescription =
                QString::fromUtf8("Found locally modified note after sync: %1")
                    .arg(note.toString());
            return false;
        }

        if (note.resources()) {
            for (const auto & resource: std::as_const(*note.resources())) {
                if (Q_UNLIKELY(resource.isLocallyModified())) {
                    errorDescription =
                        QString::fromUtf8(
                            "Found locally modified resource after sync: %1")
                            .arg(resource.toString());
                    return false;
                }
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////////

    const auto listTagsOptions = [] {
        local_storage::ILocalStorage::ListTagsOptions options;
        options.m_filters.m_localOnlyFilter =
            local_storage::ILocalStorage::ListObjectsFilter::Exclude;
        return options;
    }();

    auto listTagsFuture = localStorage.listTags(listTagsOptions);
    listTagsFuture.waitForFinished();
    if (Q_UNLIKELY(listTagsFuture.resultCount() != 1)) {
        errorDescription = QString::fromUtf8(
                               "Failed to list tags from local storage, future "
                               "result count = %1")
                               .arg(listTagsFuture.resultCount());
        return false;
    }

    const auto tags = listTagsFuture.result();
    for (const auto & tag: std::as_const(tags)) {
        if (Q_UNLIKELY(tag.isLocallyModified())) {
            errorDescription =
                QString::fromUtf8("Found locally modified tag after sync: %1")
                    .arg(tag.toString());
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool checkNoNotelessLinkedNotebookTagsInLocalStorageAfterSync(
    const local_storage::ILocalStorage & localStorage,
    QString & errorDescription)
{
    const auto listTagsOptions = [] {
        local_storage::ILocalStorage::ListTagsOptions options;
        options.m_affiliation =
            local_storage::ILocalStorage::Affiliation::AnyLinkedNotebook;
        options.m_tagNotesRelation =
            local_storage::ILocalStorage::TagNotesRelation::WithoutNotes;
        return options;
    }();

    auto listTagsFuture = localStorage.listTags(listTagsOptions);
    listTagsFuture.waitForFinished();
    if (Q_UNLIKELY(listTagsFuture.resultCount() != 1)) {
        errorDescription = QString::fromUtf8(
                               "Failed to list tags from local storage, future "
                               "result count = %1")
                               .arg(listTagsFuture.resultCount());
        return false;
    }

    const auto tags = listTagsFuture.result();
    if (!tags.isEmpty()) {
        errorDescription =
            QString::fromUtf8(
                "Found %1 linked notebook tags not related to any note in "
                "local storage after the sync")
                .arg(tags.size());
        return false;
    }

    return true;
}

} // namespace

TestRunner::TestRunner(QObject * parent) :
    QObject(parent), m_fakeAuthenticator{std::make_shared<FakeAuthenticator>()},
    m_fakeKeychainService{std::make_shared<FakeKeychainService>()}
{}

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
        m_testAccount, QDir{m_tempDir->path()});

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
    createBaseRequestContext();
}

void TestRunner::cleanupTestCase() {}

void TestRunner::runTestScenario()
{
    QFETCH(TestScenarioData, testScenarioData);

    QNINFO(
        "tests::synchronization::TestRunner",
        "TestRunner::runTestScenario: " << testScenarioData.name.data());

    m_userStoreServer->setEdamVersionMajor(testScenarioData.edamVersionMajor);
    m_userStoreServer->setEdamVersionMinor(testScenarioData.edamVersionMinor);

    const DataItemTypes mergedDataItemTypes = [&testScenarioData] {
        DataItemTypes result;
        result |= testScenarioData.serverDataItemTypes;
        result |= testScenarioData.localDataItemTypes;
        return result;
    }();

    const ItemGroups mergedItemGroups = [&testScenarioData] {
        ItemGroups result;
        result |= testScenarioData.serverItemGroups;
        result |= testScenarioData.localItemGroups;
        return result;
    }();

    const ItemSources mergedItemSources = [&testScenarioData] {
        ItemSources result;
        result |= testScenarioData.serverItemSources;
        result |= testScenarioData.localItemSources;
        return result;
    }();

    TestData testData;
    setupTestData(
        mergedDataItemTypes, mergedItemGroups, mergedItemSources,
        testScenarioData.serverExpungedDataItemTypes,
        testScenarioData.serverExpungedDataItemSources,
        m_noteStoreServer->port(), testData);

    setupNoteStoreServer(
        testData, testScenarioData.serverDataItemTypes,
        testScenarioData.serverItemGroups, testScenarioData.serverItemSources,
        *m_noteStoreServer);

    if (testScenarioData.stopSyncErrorTrigger) {
        m_noteStoreServer->setStopSynchronizationError(
            *testScenarioData.stopSyncErrorTrigger,
            testScenarioData.stopSyncError);
    }

    setupLocalStorage(
        testData, testScenarioData.localDataItemTypes,
        testScenarioData.localItemGroups, testScenarioData.localItemSources,
        *m_localStorage);

    const auto now = QDateTime::currentMSecsSinceEpoch();

    QNDEBUG(
        "tests::synchronization::TestRunner", "Setting up local sync state");

    // Will exclude local new items from computing the sync state as local new
    // items don't actually have update sequence numbers from local storage's
    // perspective
    auto localSyncState = setupSyncState(
        testData, testScenarioData.localDataItemTypes,
        testScenarioData.localItemGroups & (~(ItemGroups{} | ItemGroup::New)),
        testScenarioData.localItemSources, DataItemTypes{}, ItemSources{}, now);
    QVERIFY(localSyncState);

    QNINFO(
        "tests::synchronization::TestRunner",
        "Local sync state: " << *localSyncState);

    m_fakeSyncStateStorage->setSyncState(
        m_testAccount, std::move(localSyncState));

    QNDEBUG(
        "tests::synchronization::TestRunner", "Setting up server sync state");

    auto serverSyncState = setupSyncState(
        testData, testScenarioData.serverDataItemTypes,
        testScenarioData.serverItemGroups, testScenarioData.serverItemSources,
        testScenarioData.serverExpungedDataItemTypes,
        testScenarioData.serverExpungedDataItemSources, now);
    QVERIFY(serverSyncState);

    QNINFO(
        "tests::synchronization::TestRunner",
        "Server sync state: " << *serverSyncState);

    m_noteStoreServer->putUserOwnSyncState(
        qevercloud::SyncStateBuilder{}
            .setUpdateCount(serverSyncState->userDataUpdateCount())
            .setUserLastUpdated(serverSyncState->userDataLastSyncTime())
            .setFullSyncBefore(QDateTime::fromMSecsSinceEpoch(
                                   serverSyncState->userDataLastSyncTime())
                                   .addMonths(-1)
                                   .toMSecsSinceEpoch())
            .setCurrentTime(now)
            .build());

    const auto linkedNotebookUpdateCounts =
        serverSyncState->linkedNotebookUpdateCounts();

    const auto linkedNotebookLastSyncTimes =
        serverSyncState->linkedNotebookLastSyncTimes();

    for (const auto it:
         qevercloud::toRange(std::as_const(linkedNotebookUpdateCounts)))
    {
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

    // Using environment variable to alter the default libquentier's persistent
    // storage path
    qputenv(
        LIBQUENTIER_PERSISTENCE_STORAGE_PATH,
        syncPersistenceDirPath.toLocal8Bit());

    auto synchronizer = createSynchronizer(
        userStoreUrl, m_fakeAuthenticator, m_fakeSyncStateStorage,
        m_fakeKeychainService, m_ctx);

    auto canceler = std::make_shared<utility::cancelers::ManualCanceler>();

    auto syncResultPair = synchronizer->synchronizeAccount(
        m_testAccount, m_localStorage, canceler);

    m_syncEventsCollector->connectToNotifier(syncResultPair.second);

    bool caughtException = false;
    try {
        while (!syncResultPair.first.isFinished()) {
            QCoreApplication::sendPostedEvents();
            QCoreApplication::processEvents();
        }
        syncResultPair.first.waitForFinished();
    }
    catch (...) {
        caughtException = true;
    }

    bool syncRepeated = false;
    if (!caughtException) {
        QVERIFY2(
            syncResultPair.first.resultCount() == 1,
            "Empty sync result future");

        const auto syncResult = syncResultPair.first.result();
        QVERIFY(syncResult);

        QVERIFY(
            testScenarioData.stopSyncError.index() ==
            syncResult->stopSynchronizationError().index());

        if (!std::holds_alternative<std::monostate>(
                testScenarioData.stopSyncError))
        {
            QNDEBUG("tests::synchronization::TestRunner", "Retrying the sync");

            m_noteStoreServer->clearStopSynchronizationError();
            m_syncEventsCollector->clear();

            // Repeat the attempt
            syncRepeated = true;

            syncResultPair = synchronizer->synchronizeAccount(
                m_testAccount, m_localStorage, canceler);

            m_syncEventsCollector->connectToNotifier(syncResultPair.second);

            try {
                while (!syncResultPair.first.isFinished()) {
                    QCoreApplication::processEvents();
                }
                syncResultPair.first.waitForFinished();
            }
            catch (...) {
                caughtException = true;
            }
        }
    }

    // process events once more just to be sure that they are all delivered
    // to SyncEventsCollector
    QCoreApplication::processEvents();

    {
        const char * errorMessage = nullptr;
        QVERIFY2(
            m_syncEventsCollector->checkProgressNotificationsOrder(
                errorMessage),
            errorMessage);
    }

    // When sync is achieved through two attempts (due to first attempt
    // resulting in stop synchronization error) will not check sync events
    // correspondence to the expectations because after the first sync attempt
    // the events are cleared so only the second part of these events is
    // captured here and they might not fully correspond to the expectations.
    // It's not so bad since the overall sync result is still fully checked.
    if (!syncRepeated) {
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
            "Linked notebook sync chunks download progress messages count "
            "doesn't correspond to the expectation");

        QVERIFY2(
            !m_syncEventsCollector->userOwnNoteDownloadProgressMessages()
                    .isEmpty() == testScenarioData.expectSomeUserOwnNotes,
            "User own notes download progress messages count doesn't "
            "correspond to the expectation");

        QVERIFY2(
            !m_syncEventsCollector->userOwnResourceDownloadProgressMessages()
                    .isEmpty() == testScenarioData.expectSomeUserOwnResources,
            "User own resources download progress messages count doesn't "
            "correspond to the expectation");

        QVERIFY2(
            !m_syncEventsCollector->linkedNotebookNoteDownloadProgressMessages()
                    .isEmpty() ==
                testScenarioData.expectSomeLinkedNotebookNotes,
            "Linked notebook notes download progress messages count doesn't "
            "correspond to the expectation");

        QVERIFY2(
            !m_syncEventsCollector
                    ->linkedNotebookResourceDownloadProgressMessages()
                    .isEmpty() ==
                testScenarioData.expectSomeLinkedNotebookResources,
            "Linked notebook resources download progress messages count "
            "doesn't correspond to the expectation");

        QVERIFY2(
            !m_syncEventsCollector->userOwnSendStatusMessages().isEmpty() ==
                testScenarioData.expectSomeUserOwnDataSent,
            "User own sent data messages count doesn't correspond to the "
            "expectation");

        QVERIFY2(
            !m_syncEventsCollector->linkedNotebookSendStatusMessages()
                    .isEmpty() ==
                testScenarioData.expectSomeLinkedNotebookDataSent,
            "Linked notebook sent data messages count doesn't correspond to "
            "the expectation");
    }

    if (testScenarioData.expectFailure) {
        QVERIFY2(
            caughtException, "Sync which was expected to fail did not fail");
        return;
    }

    QVERIFY2(
        syncResultPair.first.resultCount() == 1, "Empty sync result future");

    const auto syncResult = syncResultPair.first.result();
    QVERIFY(syncResult);

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
             qevercloud::toRange(std::as_const(linkedNotebookUpdateCounts)))
        {
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
             qevercloud::toRange(std::as_const(linkedNotebookLastSyncTimes)))
        {
            QVERIFY2(
                it.value() > 0,
                "Detected zero last sync time in sync state for some linked "
                "notebook");
        }
    }

    QVERIFY(
        syncResult->userAccountSyncChunksDownloaded() ==
        testScenarioData.expectSomeUserOwnSyncChunks);

    QVERIFY(
        syncResult->linkedNotebookGuidsWithSyncChunksDownloaded().isEmpty() ==
        !testScenarioData.expectSomeLinkedNotebooksSyncChunks);

    const auto userOwnCounters =
        syncResult->userAccountSyncChunksDataCounters();

    QVERIFY(
        (userOwnCounters && !empty(*userOwnCounters)) ==
        testScenarioData.expectSomeUserOwnSyncChunks);

    const auto userOwnNotesStatus =
        syncResult->userAccountDownloadNotesStatus();

    QVERIFY(
        (userOwnNotesStatus && !empty(*userOwnNotesStatus)) ==
            testScenarioData.expectSomeUserOwnNotes ||
        (testScenarioData.serverExpungedDataItemTypes.testFlag(
             DataItemType::Note) &&
         testScenarioData.serverExpungedDataItemSources.testFlag(
             ItemSource::UserOwnAccount)));

    const auto userOwnResourcesStatus =
        syncResult->userAccountDownloadResourcesStatus();

    QVERIFY(
        (userOwnResourcesStatus && !empty(*userOwnResourcesStatus)) ==
        testScenarioData.expectSomeUserOwnResources);

    const auto userOwnSendStatus = syncResult->userAccountSendStatus();

    QVERIFY(
        (userOwnSendStatus && !empty(*userOwnSendStatus)) ==
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

    {
        QString errorMessage;
        const bool res = checkNoLocallyModifiedObjectsInLocalStorageAfterSync(
            *m_localStorage, errorMessage);
        const QByteArray errorMessageData = errorMessage.toLatin1();
        QVERIFY2(res, errorMessageData.data());
    }

    {
        QString errorMessage;
        const bool res =
            checkNoNotelessLinkedNotebookTagsInLocalStorageAfterSync(
                *m_localStorage, errorMessage);
        const QByteArray errorMessageData = errorMessage.toLatin1();
        QVERIFY2(res, errorMessageData.data());
    }
}

void TestRunner::runTestScenario_data()
{
    QTest::addColumn<TestScenarioData>("testScenarioData");

    for (const auto & scenarioData: gTestScenarioData) {
        QTest::newRow(scenarioData.name.data()) << scenarioData;
    }
}

void TestRunner::createBaseRequestContext()
{
    const QByteArray connectionTimeoutEnv =
        qgetenv(gConnectionTimeoutEnvVarKey.data());

    qevercloud::RequestContextBuilder builder;
    builder.setMaxRetryCount(0);
    builder.setIncreaseConnectionTimeoutExponentially(false);

    if (!connectionTimeoutEnv.isEmpty()) {
        bool conversionResult = false;
        const qint64 timeout =
            connectionTimeoutEnv.toLongLong(&conversionResult);
        if (conversionResult) {
            QNINFO(
                "tests::synchronization::TestRunner",
                "Using connection timeout from the environment variable: "
                    << timeout);
            builder.setConnectionTimeout(timeout);
            builder.setMaxConnectionTimeout(timeout);
        }
        else {
            QNWARNING(
                "tests::synchronization::TestRunner",
                "Failed to parse connection timeout from the environment "
                    << "variable: " << connectionTimeoutEnv);
        }
    }

    m_ctx = builder.build();
}

} // namespace quentier::synchronization::tests
