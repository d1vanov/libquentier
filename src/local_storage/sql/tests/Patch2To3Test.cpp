/*
 * Copyright 2021-2025 Dmitry Ivanov
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

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"
#include "../Fwd.h"
#include "../NotebooksHandler.h"
#include "../NotesHandler.h"
#include "../Notifier.h"
#include "../ResourcesHandler.h"
#include "../TablesInitializer.h"
#include "../TagsHandler.h"
#include "../VersionHandler.h"
#include "../patches/Patch2To3.h"
#include "../utils/ResourceDataFilesUtils.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/UidGenerator.h>

#include <qevercloud/types/builders/NoteBuilder.h>
#include <qevercloud/types/builders/NotebookBuilder.h>
#include <qevercloud/types/builders/ResourceBuilder.h>
#include <qevercloud/types/builders/TagBuilder.h>

#include <QDir>
#include <QFile>
#include <QFlags>
#include <QReadWriteLock>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtGlobal>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

using namespace std::string_literals;

namespace {

const char * gAppPersistentStoragePath = "LIBQUENTIER_PERSISTENCE_STORAGE_PATH";

const QString gTestAccountName = QStringLiteral("testAccountName");

const QString gTestDbConnectionName =
    QStringLiteral("libquentier_local_storage_sql_patch2to3_test_db");

// Changes version in Auxiliary table back from 3 to 2, to ensure that the patch
// would properly update it from 2 to 3
void changeDatabaseVersionTo2(QSqlDatabase & database)
{
    QSqlQuery query{database};
    const bool res = query.exec(
        QStringLiteral("INSERT OR REPLACE INTO Auxiliary (version) VALUES(2)"));

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::patches::2_to_3",
        QStringLiteral(
            "failed to execute SQL query increasing local storage version"));
}

// Removes ResourceDataBodyVersionIds and ResourceAlternateDataBodyVersionIds
// tables from the local storage database in order to set up the situation
// as before applying the 2 to 3 patch
void removeBodyVersionIdTables(QSqlDatabase & database)
{
    QSqlQuery query{database};

    bool res = query.exec(
        QStringLiteral("DROP TABLE IF EXISTS ResourceDataBodyVersionIds"));

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::tests::Patch2To3Test",
        QStringLiteral("Failed to drop ResourceDataBodyVersionIds table"));

    res = query.exec(QStringLiteral(
        "DROP TABLE IF EXISTS ResourceAlternateDataBodyVersionIds"));

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::tests::Patch2To3Test",
        QStringLiteral(
            "Failed to drop ResourceAlternateDataBodyVersionIds table"));
}

} // namespace

class Patch2To3Test : public testing::Test
{
protected:
    void SetUp() override
    {
        m_connectionPool = utils::createConnectionPool();

        auto database = m_connectionPool->database();
        TablesInitializer::initializeTables(database);

        m_thread = std::make_shared<QThread>();
        m_notifier = new Notifier;
        m_notifier->moveToThread(m_thread.get());

        QObject::connect(
            m_thread.get(), &QThread::finished, m_notifier,
            &QObject::deleteLater);

        m_thread->start();

        qputenv(gAppPersistentStoragePath, m_temporaryDir.path().toUtf8());
    }

    void TearDown() override
    {
        qunsetenv(gAppPersistentStoragePath);

        m_thread->quit();
        m_thread->wait();

        // Give lambdas connected to threads finished signal a chance to fire
        QCoreApplication::processEvents();
    }

public:
protected:
    ConnectionPoolPtr m_connectionPool;
    threading::QThreadPtr m_thread;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

TEST_F(Patch2To3Test, Ctor)
{
    Account account{gTestAccountName, Account::Type::Local};

    EXPECT_NO_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), m_connectionPool, m_thread));
}

TEST_F(Patch2To3Test, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto patch =
            std::make_shared<Patch2To3>(Account{}, m_connectionPool, m_thread),
        IQuentierException);
}

TEST_F(Patch2To3Test, CtorNullConnectionPool)
{
    Account account{gTestAccountName, Account::Type::Local};

    EXPECT_THROW(
        const auto patch =
            std::make_shared<Patch2To3>(std::move(account), nullptr, m_thread),
        IQuentierException);
}

TEST_F(Patch2To3Test, CtorNullThread)
{
    Account account{gTestAccountName, Account::Type::Local};

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), m_connectionPool, nullptr),
        IQuentierException);
}

// Resources test data which is put into the local storage on which
// the tested patch is applied
struct ResourcesTestData
{
    qevercloud::Notebook m_notebook;
    qevercloud::Note m_note;
    qevercloud::Resource m_firstResource;
    qevercloud::Resource m_secondResource;
    qevercloud::Resource m_thirdResource;
};

enum PrepareLocalStorageForUpgradeFlag
{
    RemoveResourceVersionIdsTables = 1 << 0,
    MoveResourceBodyFiles = 1 << 1
};

Q_DECLARE_FLAGS(
    PrepareLocalStorageForUpgradeFlags, PrepareLocalStorageForUpgradeFlag);

[[nodiscard]] QString prepareAccountLocalStorageDir(
    const QString & localStorageDirPath, ConnectionPool & connectionPool)
{
    QString path;
    {
        QTextStream strm{&path};
        strm << localStorageDirPath << "/LocalAccounts/" << gTestAccountName;
        strm.flush();
    }

    utils::prepareLocalStorage(path, connectionPool);
    return path;
}

// Prepares local storage database corresponding to version 2 in a temporary
// dir so that it can be upgraded from version 2 to version 3
[[nodiscard]] ResourcesTestData prepareResourcesForVersionsUpgrade(
    const QString & localStorageDirPath,
    const PrepareLocalStorageForUpgradeFlags flags,
    const ConnectionPoolPtr & connectionPool,
    INotebooksHandler & notebooksHandler, INotesHandler & notesHandler,
    IResourcesHandler & resourcesHandler)
{
    // Put some data into the local storage database
    const auto now = QDateTime::currentMSecsSinceEpoch();
    ResourcesTestData testData;

    testData.m_notebook.setGuid(UidGenerator::Generate());
    testData.m_notebook.setName(QStringLiteral("name"));
    testData.m_notebook.setUpdateSequenceNum(1);
    testData.m_notebook.setServiceCreated(now);
    testData.m_notebook.setServiceUpdated(now);

    auto putNotebookFuture = notebooksHandler.putNotebook(testData.m_notebook);
    putNotebookFuture.waitForFinished();

    testData.m_note.setLocallyModified(true);
    testData.m_note.setLocalOnly(false);
    testData.m_note.setLocallyFavorited(true);
    testData.m_note.setNotebookLocalId(testData.m_notebook.localId());
    testData.m_note.setNotebookGuid(testData.m_notebook.guid());
    testData.m_note.setGuid(UidGenerator::Generate());
    testData.m_note.setUpdateSequenceNum(1);
    testData.m_note.setTitle(QStringLiteral("Title"));
    testData.m_note.setContent(
        QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    testData.m_note.setContentHash(QCryptographicHash::hash(
        testData.m_note.content()->toUtf8(), QCryptographicHash::Md5));

    testData.m_note.setContentLength(testData.m_note.content()->size());
    testData.m_note.setCreated(now);
    testData.m_note.setUpdated(now);

    auto putNoteFuture = notesHandler.putNote(testData.m_note);
    putNoteFuture.waitForFinished();

    testData.m_firstResource.setLocallyModified(true);
    testData.m_firstResource.setGuid(UidGenerator::Generate());
    testData.m_firstResource.setUpdateSequenceNum(42);
    testData.m_firstResource.setNoteLocalId(testData.m_note.localId());
    testData.m_firstResource.setNoteGuid(testData.m_note.guid());
    testData.m_firstResource.setMime("application/text-plain");
    testData.m_firstResource.setWidth(10);
    testData.m_firstResource.setHeight(20);

    testData.m_firstResource.setData(qevercloud::Data{});
    {
        auto & data = *testData.m_firstResource.mutableData();
        data.setBody(QByteArray::fromStdString("test first resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    testData.m_secondResource = testData.m_firstResource;
    testData.m_secondResource.setLocalId(UidGenerator::Generate());
    testData.m_secondResource.setGuid(UidGenerator::Generate());
    testData.m_secondResource.setUpdateSequenceNum(
        testData.m_secondResource.updateSequenceNum().value() + 1);

    {
        auto & data = testData.m_secondResource.mutableData().value();
        data.setBody(QByteArray::fromStdString("test second resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    testData.m_secondResource.setAlternateData(qevercloud::Data{});
    {
        auto & data = *testData.m_secondResource.mutableAlternateData();
        data.setBody(
            QByteArray::fromStdString("test second resource alternate data"s));

        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    testData.m_thirdResource = testData.m_secondResource;
    testData.m_thirdResource.setLocalId(UidGenerator::Generate());
    testData.m_thirdResource.setGuid(UidGenerator::Generate());
    testData.m_thirdResource.setUpdateSequenceNum(
        testData.m_thirdResource.updateSequenceNum().value() + 1);

    {
        auto & data = testData.m_thirdResource.mutableData().value();
        data.setBody(QByteArray::fromStdString("test third resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    {
        auto & data = testData.m_thirdResource.mutableAlternateData().value();
        data.setBody(
            QByteArray::fromStdString("test third resource alternate data"s));

        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    testData.m_thirdResource.setRecognition(qevercloud::Data{});
    {
        auto & data = *testData.m_thirdResource.mutableRecognition();
        data.setBody(QByteArray::fromStdString(
            R"___(<?xml version="1.0" encoding="UTF-8"?>
<recoIndex docType="picture" objType="ink"
        objID="a284273e482578224145f2560b67bf45"
        engineVersion="3.0.17.14" recoType="client" lang="en"
        objWidth="1936" objHeight="2592">
    <item x="853" y="1278" w="14" h="17">
        <t w="31">II</t>
        <t w="31">11</t>
        <t w="31">ll</t>
        <t w="31">Il</t>
    </item>
    <item x="501" y="635" w="770" h="254" offset="12" duration="17"
        strokeList="14,28,19,41,54">
        <t w="32">LONG</t>
        <t w="25">LONG</t>
        <t w="23">GOV</t>
        <t w="23">NOV</t>
        <t w="19">Lang</t>
        <t w="18">lane</t>
        <t w="18">CONN</t>
        <t w="17">bono</t>
        <t w="17">mono</t>
        <t w="15">LONON</t>
        <t w="15">LONGE</t>
        <object type="face" w="31"/>
        <object type="lake" w="30"/>
        <object type="snow" w="29"/>
        <object type="road" w="32"/>
        <shape type="circle" w="31"/>
        <shape type="oval" w="29"/>
        <shape type="rectangle" w="30"/>
        <shape type="triangle" w="32"/>
        <barcode w="32">5000600001</barcode>
        <barcode w="25">3000600001</barcode>
        <barcode w="31">2000600001</barcode>
    </item>
</recoIndex>)___"s));

        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    auto putFirstResourceFuture =
        resourcesHandler.putResource(testData.m_firstResource);

    putFirstResourceFuture.waitForFinished();

    auto putSecondResourceFuture =
        resourcesHandler.putResource(testData.m_secondResource);

    putSecondResourceFuture.waitForFinished();

    auto putThirdResourceFuture =
        resourcesHandler.putResource(testData.m_thirdResource);

    putThirdResourceFuture.waitForFinished();

    // Now need to mutate the data to make the local storage files layout
    // look like version 2.

    auto database = connectionPool->database();

    enum class ResourceDataKind
    {
        Data,
        AlternateData
    };

    const auto moveResourceDataFiles =
        [&](const qevercloud::Resource & resource,
            const ResourceDataKind dataKind) {
            if (dataKind == ResourceDataKind::Data &&
                (!resource.data() || !resource.data()->body()))
            {
                return;
            }

            if (dataKind == ResourceDataKind::AlternateData &&
                (!resource.alternateData() ||
                 !resource.alternateData()->body()))
            {
                return;
            }

            QString versionId;
            ErrorString errorDescription;

            const bool res =
                (dataKind == ResourceDataKind::Data
                     ? ::quentier::local_storage::sql::utils::
                           findResourceDataBodyVersionId(
                               resource.localId(), database, versionId,
                               errorDescription)
                     : ::quentier::local_storage::sql::utils::
                           findResourceAlternateDataBodyVersionId(
                               resource.localId(), database, versionId,
                               errorDescription));

            if (!res) {
                FAIL() << errorDescription.nonLocalizedString().toStdString();
                return;
            }

            const QString dataPathPart =
                (dataKind == ResourceDataKind::Data
                     ? QStringLiteral("data")
                     : QStringLiteral("alternateData"));

            const QString pathFrom = [&] {
                QString result;
                QTextStream strm{&result};
                strm << localStorageDirPath << "/Resources/" << dataPathPart
                     << "/" << testData.m_note.localId() << "/"
                     << resource.localId() << "/" << versionId << ".dat";
                return result;
            }();

            const QString pathTo = [&] {
                QString result;
                QTextStream strm{&result};
                strm << localStorageDirPath << "/Resources/" << dataPathPart
                     << "/" << testData.m_note.localId() << "/"
                     << resource.localId() << ".dat";
                return result;
            }();

            errorDescription.clear();
            if (!::quentier::renameFile(pathFrom, pathTo, errorDescription)) {
                FAIL() << errorDescription.nonLocalizedString().toStdString();
                return;
            }

            const QString dirPathToRemove = [&] {
                QString result;
                QTextStream strm{&result};
                strm << localStorageDirPath << "/Resources/" << dataPathPart
                     << "/" << testData.m_note.localId() << "/"
                     << resource.localId();
                return result;
            }();

            if (!::quentier::removeDir(dirPathToRemove)) {
                FAIL() << "Failed to remove dir: "
                       << dirPathToRemove.toStdString();
                return;
            }
        };

    const auto moveResourceFiles = [&](const qevercloud::Resource & resource) {
        moveResourceDataFiles(resource, ResourceDataKind::Data);
        moveResourceDataFiles(resource, ResourceDataKind::AlternateData);
    };

    if (flags.testFlag(
            PrepareLocalStorageForUpgradeFlag::MoveResourceBodyFiles))
    {
        moveResourceFiles(testData.m_firstResource);
        moveResourceFiles(testData.m_secondResource);
        moveResourceFiles(testData.m_thirdResource);
    }

    if (flags.testFlag(
            PrepareLocalStorageForUpgradeFlag::RemoveResourceVersionIdsTables))
    {
        removeBodyVersionIdTables(database);
    }

    changeDatabaseVersionTo2(database);

    return testData;
}

struct Patch2To3ResourcesTestData
{
    PrepareLocalStorageForUpgradeFlags flags;
    std::function<void()> prepareFunc = {}; // NOLINT
};

class Patch2To3ResourcesTest :
    public Patch2To3Test,
    public testing::WithParamInterface<Patch2To3ResourcesTestData>
{};

const std::array gPatch2To3ResourcesTestData{
    Patch2To3ResourcesTestData{
        PrepareLocalStorageForUpgradeFlags{} |
            PrepareLocalStorageForUpgradeFlag::RemoveResourceVersionIdsTables |
            PrepareLocalStorageForUpgradeFlag::MoveResourceBodyFiles,
    },
    Patch2To3ResourcesTestData{
        PrepareLocalStorageForUpgradeFlags{} |
            PrepareLocalStorageForUpgradeFlag::MoveResourceBodyFiles,
        [] {
            Account account{gTestAccountName, Account::Type::Local};

            utility::ApplicationSettings databaseUpgradeInfo{
                account,
                QStringLiteral(
                    "LocalStorageDatabaseUpgradeFromVersion2ToVersion3")};

            databaseUpgradeInfo.setValue(
                QStringLiteral("ResourceBodyVersionIdTablesCreated"), true);

            databaseUpgradeInfo.setValue(
                QStringLiteral("ResourceBodyVersionIdsCommittedToDatabase"),
                true);

            databaseUpgradeInfo.sync();
        }},
    Patch2To3ResourcesTestData{
        PrepareLocalStorageForUpgradeFlags{},
        [] {
            Account account{gTestAccountName, Account::Type::Local};

            utility::ApplicationSettings databaseUpgradeInfo{
                account,
                QStringLiteral(
                    "LocalStorageDatabaseUpgradeFromVersion2ToVersion3")};

            databaseUpgradeInfo.setValue(
                QStringLiteral("ResourceBodyVersionIdTablesCreated"), true);

            databaseUpgradeInfo.setValue(
                QStringLiteral("ResourceBodyVersionIdsCommittedToDatabase"),
                true);

            databaseUpgradeInfo.setValue(
                QStringLiteral("ResourceBodyFilesMovedToVersionIdFolders"),
                true);

            databaseUpgradeInfo.sync();
        }},
};

INSTANTIATE_TEST_SUITE_P(
    Patch2To3DataTestInstance, Patch2To3ResourcesTest,
    testing::ValuesIn(gPatch2To3ResourcesTestData));

TEST_P(Patch2To3ResourcesTest, CheckResourceVersionIdsUpgrade)
{
    Account account{gTestAccountName, Account::Type::Local};

    const auto data = GetParam();

    const QString localStorageDirPath =
        prepareAccountLocalStorageDir(m_temporaryDir.path(), *m_connectionPool);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    const auto testData = prepareResourcesForVersionsUpgrade(
        localStorageDirPath, data.flags, m_connectionPool, *notebooksHandler,
        *notesHandler, *resourcesHandler);
    if (data.prepareFunc) {
        data.prepareFunc();
    }

    const auto versionHandler =
        std::make_shared<VersionHandler>(account, m_connectionPool, m_thread);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 2);

    const auto patch = std::make_shared<Patch2To3>(
        std::move(account), m_connectionPool, m_thread);

    auto applyFuture = patch->apply();
    ASSERT_NO_THROW(applyFuture.waitForFinished());

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 1U);

    auto findNotebookFuture =
        notebooksHandler->findNotebookByLocalId(testData.m_notebook.localId());

    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_EQ(findNotebookFuture.result(), testData.m_notebook);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCount(
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 1U);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    const auto fetchNoteOptions = FetchNoteOptions{} |
        FetchNoteOption::WithResourceMetadata |
        FetchNoteOption::WithResourceBinaryData;

    auto findNoteFuture = notesHandler->findNoteByLocalId(
        testData.m_note.localId(), fetchNoteOptions);

    // Note from test data doesn't contain resources but found note will
    auto testNoteCopy = testData.m_note;
    testNoteCopy.setResources(
        QList<qevercloud::Resource>{} << testData.m_firstResource
                                      << testData.m_secondResource
                                      << testData.m_thirdResource);

    findNoteFuture.waitForFinished();
    ASSERT_EQ(findNoteFuture.resultCount(), 1);
    EXPECT_EQ(findNoteFuture.result(), testNoteCopy);

    auto resourceCountFuture = resourcesHandler->resourceCount(
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 3U);

    using FetchResourceOption = ILocalStorage::FetchResourceOption;
    using FetchResourceOptions = ILocalStorage::FetchResourceOptions;

    const auto fetchResourceOptions =
        FetchResourceOptions{} | FetchResourceOption::WithBinaryData;

    auto findFirstResourceFuture = resourcesHandler->findResourceByLocalId(
        testData.m_firstResource.localId(), fetchResourceOptions);

    findFirstResourceFuture.waitForFinished();
    ASSERT_EQ(findFirstResourceFuture.resultCount(), 1);
    EXPECT_EQ(findFirstResourceFuture.result(), testData.m_firstResource);

    auto findSecondResourceFuture = resourcesHandler->findResourceByLocalId(
        testData.m_secondResource.localId(), fetchResourceOptions);

    findSecondResourceFuture.waitForFinished();
    ASSERT_EQ(findSecondResourceFuture.resultCount(), 1);
    EXPECT_EQ(findSecondResourceFuture.result(), testData.m_secondResource);

    auto findThirdResourceFuture = resourcesHandler->findResourceByLocalId(
        testData.m_thirdResource.localId(), fetchResourceOptions);

    findThirdResourceFuture.waitForFinished();
    ASSERT_EQ(findThirdResourceFuture.resultCount(), 1);
    EXPECT_EQ(findThirdResourceFuture.result(), testData.m_thirdResource);

    versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 3);
}

TEST_F(Patch2To3Test, CheckNoteNotebookGuidsUpgrade)
{
    // Local notebooks and notes - they would have no guids. The patch should
    // not touch them.
    const QList<qevercloud::Notebook> localNotebooks = [] {
        constexpr int localNotebookCount = 5;
        QList<qevercloud::Notebook> result;
        result.reserve(localNotebookCount);
        for (int i = 0; i < localNotebookCount; ++i) {
            result << qevercloud::NotebookBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setName(QString::fromUtf8("Local notebook #%1")
                                       .arg(i + 1))
                          .build();
        }

        return result;
    }();

    const QList<qevercloud::Note> localNotes = [&localNotebooks] {
        constexpr int localNotePerNotebookCount = 5;
        QList<qevercloud::Note> result;
        result.reserve(localNotePerNotebookCount * localNotebooks.size());
        int localNoteCounter = 1;
        for (const auto & localNotebook: std::as_const(localNotebooks)) {
            for (int i = 0; i < localNotePerNotebookCount; ++i) {
                result << qevercloud::NoteBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setTitle(QString::fromUtf8("Local note #%1")
                                            .arg(localNoteCounter++))
                              .setNotebookLocalId(localNotebook.localId())
                              .build();
            }
        }

        return result;
    }();

    qint32 updateSequenceNum = 1;

    // Notebooks with guids and update sequence numbers
    const QList<qevercloud::Notebook> notebooks = [&updateSequenceNum] {
        constexpr int notebookCount = 5;
        QList<qevercloud::Notebook> result;
        result.reserve(notebookCount);
        for (int i = 0; i < notebookCount; ++i) {
            result << qevercloud::NotebookBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(updateSequenceNum++)
                          .setName(QString::fromUtf8("Non-local notebook #%1")
                                       .arg(i + 1))
                          .build();
        }

        return result;
    }();

    // Notes with guids and update sequence numbers and with non-empty notebook
    // guids - patch should not change them in any way.
    const QList<qevercloud::Note> notesWithNotebookGuids = [&updateSequenceNum,
                                                            &notebooks] {
        constexpr int notePerNotebookCount = 5;
        QList<qevercloud::Note> result;
        result.reserve(notePerNotebookCount);
        int noteCounter = 1;
        for (const auto & notebook: std::as_const(notebooks)) {
            for (int i = 0; i < notePerNotebookCount; ++i) {
                result << qevercloud::NoteBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setGuid(UidGenerator::Generate())
                              .setUpdateSequenceNum(updateSequenceNum++)
                              .setTitle(QString::fromUtf8(
                                            "Note with notebook guid #%1")
                                            .arg(noteCounter++))
                              .setNotebookLocalId(notebook.localId())
                              .setNotebookGuid(notebook.guid())
                              .build();
            }
        }

        return result;
    }();

    // Notes with guids and update sequence numbers and with empty notebook
    // guids - patch should set notebook guids for these notes.
    const QList<qevercloud::Note> notesWithoutNotebookGuids =
        [&updateSequenceNum, &notebooks] {
            constexpr int notePerNotebookCount = 5;
            QList<qevercloud::Note> result;
            result.reserve(notePerNotebookCount);
            int noteCounter = 1;
            for (const auto & notebook: std::as_const(notebooks)) {
                for (int i = 0; i < notePerNotebookCount; ++i) {
                    result << qevercloud::NoteBuilder{}
                                  .setLocalId(UidGenerator::Generate())
                                  .setGuid(UidGenerator::Generate())
                                  .setUpdateSequenceNum(updateSequenceNum++)
                                  .setTitle(
                                      QString::fromUtf8(
                                          "Note without notebook guid #%1")
                                          .arg(noteCounter++))
                                  .setNotebookLocalId(notebook.localId())
                                  .build();
                }
            }

            return result;
        }();

    Account account{gTestAccountName, Account::Type::Local};

    const QString localStorageDirPath =
        prepareAccountLocalStorageDir(m_temporaryDir.path(), *m_connectionPool);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    for (const auto & notebook: std::as_const(localNotebooks)) {
        auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
        putNotebookFuture.waitForFinished();
    }

    for (const auto & note: std::as_const(localNotes)) {
        auto putNoteFuture = notesHandler->putNote(note);
        putNoteFuture.waitForFinished();
    }

    for (const auto & notebook: std::as_const(notebooks)) {
        auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
        putNotebookFuture.waitForFinished();
    }

    for (const auto & note: std::as_const(notesWithNotebookGuids)) {
        auto putNoteFuture = notesHandler->putNote(note);
        putNoteFuture.waitForFinished();
    }

    for (const auto & note: std::as_const(notesWithoutNotebookGuids)) {
        auto putNoteFuture = notesHandler->putNote(note);
        putNoteFuture.waitForFinished();
    }

    // Make sure that notes which are meant to not have notebook guids indeed
    // do not have them - after the fixes applied in local storage of version 3
    // they should have notebook guids even if these were missing before putting
    // the note to local storage.
    auto database = m_connectionPool->database();
    for (const auto & note: std::as_const(notesWithoutNotebookGuids)) {
        QSqlQuery query{database};
        bool res = query.prepare(QStringLiteral(
            "UPDATE Notes SET notebookGuid = NULL WHERE localUid = :localUid"));
        ASSERT_TRUE(res);

        query.bindValue(QStringLiteral(":localUid"), note.localId());
        res = query.exec();
        ASSERT_TRUE(res);
    }

    changeDatabaseVersionTo2(database);

    const auto versionHandler =
        std::make_shared<VersionHandler>(account, m_connectionPool, m_thread);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 2);

    const auto patch = std::make_shared<Patch2To3>(
        std::move(account), m_connectionPool, m_thread);

    auto applyFuture = patch->apply();
    ASSERT_NO_THROW(applyFuture.waitForFinished());

    const auto sortNotes = [](QList<qevercloud::Note> & notes) {
        std::sort(
            notes.begin(), notes.end(),
            [](const qevercloud::Note & lhs, const qevercloud::Note & rhs) {
                return lhs.localId() < rhs.localId();
            });
    };

    const QList<qevercloud::Note> expectedNotes = [&] {
        QList<qevercloud::Note> result;
        result.reserve(
            localNotes.size() + notesWithNotebookGuids.size() +
            notesWithoutNotebookGuids.size());

        for (const auto & note: std::as_const(localNotes)) {
            result << note;
        }

        for (const auto & note: std::as_const(notesWithNotebookGuids)) {
            result << note;
        }

        for (auto note: std::as_const(notesWithoutNotebookGuids)) {
            const auto notebookIt = std::find_if(
                notebooks.constBegin(), notebooks.constEnd(),
                [&note](const qevercloud::Notebook & notebook) {
                    return notebook.localId() == note.notebookLocalId();
                });
            EXPECT_NE(notebookIt, notebooks.constEnd());
            if (notebookIt != notebooks.constEnd()) {
                note.setNotebookGuid(notebookIt->guid());
                result << note;
            }
        }

        sortNotes(result);
        return result;
    }();

    auto notesFromLocalStorageFuture = notesHandler->listNotes(
        NotesHandler::FetchNoteOptions{}, NotesHandler::ListNotesOptions{});

    notesFromLocalStorageFuture.waitForFinished();
    ASSERT_EQ(notesFromLocalStorageFuture.resultCount(), 1);

    auto notesFromLocalStorage = notesFromLocalStorageFuture.result();
    sortNotes(notesFromLocalStorage);
    EXPECT_EQ(notesFromLocalStorage, expectedNotes);

    versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 3);
}

TEST_F(Patch2To3Test, CheckParentTagGuidsUpgrade)
{
    // Local tags - they would have no parent guids. The patch should not touch
    // them.
    const QList<qevercloud::Tag> localParentTags = [] {
        constexpr int localParentTagCount = 5;
        QList<qevercloud::Tag> result;
        result.reserve(localParentTagCount);
        for (int i = 0; i < localParentTagCount; ++i) {
            result << qevercloud::TagBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setName(QString::fromUtf8("Local parent tag #%1")
                                       .arg(i + 1))
                          .build();
        }

        return result;
    }();

    // Local child tags. Again, the patch should not touch them.
    const QList<qevercloud::Tag> localChildTags = [&localParentTags] {
        constexpr int localChildTagPerParentTagCount = 5;
        QList<qevercloud::Tag> result;
        result.reserve(localChildTagPerParentTagCount * localParentTags.size());
        int tagCounter = 1;
        for (const auto & parentTag: std::as_const(localParentTags)) {
            for (int i = 0; i < localChildTagPerParentTagCount; ++i) {
                result << qevercloud::TagBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setName(QString::fromUtf8("Local child tag #%1")
                                           .arg(tagCounter++))
                              .setParentTagLocalId(parentTag.localId())
                              .build();
            }
        }

        return result;
    }();

    qint32 updateSequenceNum = 1;

    // Parent tags with guids and update sequence numbers
    const QList<qevercloud::Tag> parentTags = [&updateSequenceNum] {
        constexpr int tagCount = 5;
        QList<qevercloud::Tag> result;
        result.reserve(tagCount);
        for (int i = 0; i < tagCount; ++i) {
            result << qevercloud::TagBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(updateSequenceNum++)
                          .setName(QString::fromUtf8("Non-local parent tag #%1")
                                       .arg(i + 1))
                          .build();
        }

        return result;
    }();

    // Child tags with guids and parent tag guids
    const QList<qevercloud::Tag> childTagsWithParentTagGuids =
        [&updateSequenceNum, &parentTags] {
            constexpr int childTagPerParentTagCount = 5;
            QList<qevercloud::Tag> result;
            result.reserve(childTagPerParentTagCount * parentTags.size());
            int tagCounter = 1;
            for (const auto & parentTag: std::as_const(parentTags)) {
                for (int i = 0; i < childTagPerParentTagCount; ++i) {
                    result << qevercloud::TagBuilder{}
                                  .setLocalId(UidGenerator::Generate())
                                  .setGuid(UidGenerator::Generate())
                                  .setUpdateSequenceNum(updateSequenceNum++)
                                  .setName(
                                      QString::fromUtf8("Non-local child tag "
                                                        "with parent guid #%1")
                                          .arg(tagCounter++))
                                  .setParentTagLocalId(parentTag.localId())
                                  .setParentGuid(parentTag.guid())
                                  .build();
                }
            }

            return result;
        }();

    // Child tags with guids and parent tag local ids but without parent tag
    // guids
    const QList<qevercloud::Tag> childTagsWithoutParentTagGuids =
        [&updateSequenceNum, &parentTags] {
            constexpr int childTagPerParentTagCount = 5;
            QList<qevercloud::Tag> result;
            result.reserve(childTagPerParentTagCount * parentTags.size());
            int tagCounter = 1;
            for (const auto & parentTag: std::as_const(parentTags)) {
                for (int i = 0; i < childTagPerParentTagCount; ++i) {
                    result << qevercloud::TagBuilder{}
                                  .setLocalId(UidGenerator::Generate())
                                  .setGuid(UidGenerator::Generate())
                                  .setUpdateSequenceNum(updateSequenceNum++)
                                  .setName(QString::fromUtf8(
                                               "Non-local child tag without "
                                               "parent guid #%1")
                                               .arg(tagCounter++))
                                  .setParentTagLocalId(parentTag.localId())
                                  .build();
                }
            }

            return result;
        }();

    Account account{gTestAccountName, Account::Type::Local};

    Q_UNUSED(
        prepareAccountLocalStorageDir(m_temporaryDir.path(), *m_connectionPool))

    const auto tagsHandler =
        std::make_shared<TagsHandler>(m_connectionPool, m_notifier, m_thread);

    for (const auto & tag: std::as_const(localParentTags)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    for (const auto & tag: std::as_const(localChildTags)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    for (const auto & tag: std::as_const(parentTags)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    for (const auto & tag: std::as_const(childTagsWithParentTagGuids)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    for (const auto & tag: std::as_const(childTagsWithoutParentTagGuids)) {
        auto putTagFuture = tagsHandler->putTag(tag);
        putTagFuture.waitForFinished();
    }

    // Make sure that tags which are meant to not have parent tag guids indeed
    // do not have them - after the fixes applied in local storage of version 3
    // they should have parent tag guids even if these were missing before
    // putting the tag to local storage.
    auto database = m_connectionPool->database();
    for (const auto & tag: std::as_const(childTagsWithoutParentTagGuids)) {
        QSqlQuery query{database};
        bool res = query.prepare(QStringLiteral(
            "UPDATE Tags SET parentGuid = NULL WHERE localUid = :localUid"));
        ASSERT_TRUE(res);

        query.bindValue(QStringLiteral(":localUid"), tag.localId());
        res = query.exec();
        ASSERT_TRUE(res);
    }

    changeDatabaseVersionTo2(database);

    const auto versionHandler =
        std::make_shared<VersionHandler>(account, m_connectionPool, m_thread);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 2);

    const auto patch = std::make_shared<Patch2To3>(
        std::move(account), m_connectionPool, m_thread);

    auto applyFuture = patch->apply();
    ASSERT_NO_THROW(applyFuture.waitForFinished());

    const auto sortTags = [](QList<qevercloud::Tag> & tags) {
        std::sort(
            tags.begin(), tags.end(),
            [](const qevercloud::Tag & lhs, const qevercloud::Tag & rhs) {
                return lhs.localId() < rhs.localId();
            });
    };

    const QList<qevercloud::Tag> expectedTags = [&] {
        QList<qevercloud::Tag> result;
        result.reserve(
            localParentTags.size() + localChildTags.size() + parentTags.size() +
            childTagsWithParentTagGuids.size() +
            childTagsWithoutParentTagGuids.size());

        for (const auto & tag: std::as_const(localParentTags)) {
            result << tag;
        }

        for (const auto & tag: std::as_const(localChildTags)) {
            result << tag;
        }

        for (const auto & tag: std::as_const(parentTags)) {
            result << tag;
        }

        for (const auto & tag: std::as_const(childTagsWithParentTagGuids)) {
            result << tag;
        }

        for (auto tag: std::as_const(childTagsWithoutParentTagGuids)) {
            const auto parentTagIt = std::find_if(
                parentTags.constBegin(), parentTags.constEnd(),
                [&tag](const qevercloud::Tag & parentTag) {
                    return tag.parentTagLocalId() == parentTag.localId();
                });
            EXPECT_NE(parentTagIt, parentTags.constEnd());
            if (parentTagIt != parentTags.constEnd()) {
                tag.setParentGuid(parentTagIt->guid());
                result << tag;
            }
        }

        sortTags(result);
        return result;
    }();

    auto tagsFromLocalStorageFuture = tagsHandler->listTags();
    tagsFromLocalStorageFuture.waitForFinished();
    ASSERT_EQ(tagsFromLocalStorageFuture.resultCount(), 1);

    auto tagsFromLocalStorage = tagsFromLocalStorageFuture.result();
    sortTags(tagsFromLocalStorage);
    EXPECT_EQ(tagsFromLocalStorage, expectedTags);

    versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 3);
}

TEST_F(Patch2To3Test, CheckResourceNoteGuidsUpgrade)
{
    // Local notebook
    const auto localNotebook =
        qevercloud::NotebookBuilder{}
            .setLocalId(UidGenerator::Generate())
            .setName(QStringLiteral(
                "Local notebook for resource note guids upgrade"))
            .build();

    // Local notes and resources - they would have no guids. The patch should
    // not touch them.
    QList<qevercloud::Note> localNotes = [&] {
        constexpr int localNoteCount = 5;
        QList<qevercloud::Note> result;
        result.reserve(localNoteCount);
        for (int i = 0; i < localNoteCount; ++i) {
            result << qevercloud::NoteBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setNotebookLocalId(localNotebook.localId())
                          .setTitle(
                              QString::fromUtf8("Local note #%1").arg(i + 1))
                          .build();
        }

        return result;
    }();

    const QList<qevercloud::Resource> localResources = [&] {
        constexpr int resourcesPerNoteCount = 5;
        QList<qevercloud::Resource> result;
        result.reserve(localNotes.size() * resourcesPerNoteCount);
        for (auto & note: localNotes) {
            for (int i = 0; i < resourcesPerNoteCount; ++i) {
                result << qevercloud::ResourceBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setNoteLocalId(note.localId())
                              .build();
                if (!note.resources()) {
                    note.mutableResources().emplace(QList{result.constLast()});
                }
                else {
                    note.mutableResources()->append(result.constLast());
                }
            }
        }

        return result;
    }();

    qint32 updateSequenceNum = 1;

    // Non-local notebook
    const auto notebook =
        qevercloud::NotebookBuilder{}
            .setLocalId(UidGenerator::Generate())
            .setGuid(UidGenerator::Generate())
            .setUpdateSequenceNum(updateSequenceNum++)
            .setName("Notebook for resource note guids upgrade")
            .build();

    // Notes with guids and update sequence numbers
    QList<qevercloud::Note> notes = [&] {
        constexpr int noteCount = 5;
        QList<qevercloud::Note> result;
        result.reserve(noteCount);
        for (int i = 0; i < noteCount; ++i) {
            result << qevercloud::NoteBuilder{}
                          .setLocalId(UidGenerator::Generate())
                          .setGuid(UidGenerator::Generate())
                          .setUpdateSequenceNum(updateSequenceNum++)
                          .setNotebookLocalId(notebook.localId())
                          .setNotebookGuid(notebook.guid())
                          .setTitle(QString::fromUtf8("Note #%1").arg(i + 1))
                          .build();
        }

        return result;
    }();

    // Resources with guids and update sequence numbers and with note guids
    QList<qevercloud::Resource> resourcesWithNoteGuids = [&] {
        constexpr int resourcesPerNoteCount = 5;
        QList<qevercloud::Resource> result;
        result.reserve(notes.size() * resourcesPerNoteCount);
        for (auto & note: notes) {
            for (int i = 0; i < resourcesPerNoteCount; ++i) {
                result << qevercloud::ResourceBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setGuid(UidGenerator::Generate())
                              .setNoteLocalId(note.localId())
                              .setNoteGuid(note.guid())
                              .setUpdateSequenceNum(updateSequenceNum++)
                              .build();
                if (!note.resources()) {
                    note.mutableResources().emplace(QList{result.constLast()});
                }
                else {
                    note.mutableResources()->append(result.constLast());
                }
            }
        }

        return result;
    }();

    // Resources with guids and update sequence numbers but without note guids
    QList<qevercloud::Resource> resourcesWithoutNoteGuids = [&] {
        constexpr int resourcesPerNoteCount = 5;
        QList<qevercloud::Resource> result;
        result.reserve(notes.size() * resourcesPerNoteCount);
        for (auto & note: notes) {
            for (int i = 0; i < resourcesPerNoteCount; ++i) {
                result << qevercloud::ResourceBuilder{}
                              .setLocalId(UidGenerator::Generate())
                              .setGuid(UidGenerator::Generate())
                              .setNoteLocalId(note.localId())
                              .setUpdateSequenceNum(updateSequenceNum++)
                              .build();
                if (!note.resources()) {
                    note.mutableResources().emplace(QList{result.constLast()});
                }
                else {
                    note.mutableResources()->append(result.constLast());
                }
            }
        }

        return result;
    }();

    Account account{gTestAccountName, Account::Type::Local};

    const QString localStorageDirPath =
        prepareAccountLocalStorageDir(m_temporaryDir.path(), *m_connectionPool);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_notifier, m_thread, localStorageDirPath);

    {
        auto putNotebookFuture = notebooksHandler->putNotebook(localNotebook);
        putNotebookFuture.waitForFinished();
    }

    {
        auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
        putNotebookFuture.waitForFinished();
    }

    for (const auto & note: std::as_const(localNotes)) {
        auto putNoteFuture = notesHandler->putNote(note);
        putNoteFuture.waitForFinished();
    }

    for (const auto & note: std::as_const(notes)) {
        auto putNoteFuture = notesHandler->putNote(note);
        putNoteFuture.waitForFinished();
    }

    // Make sure that resources which are meant to not have note guids indeed
    // do not have them - after the fixes applied in local storage of version 3
    // they should have note guids even if these were missing before putting
    // the resource to local storage.
    auto database = m_connectionPool->database();
    for (const auto & resource: std::as_const(resourcesWithoutNoteGuids)) {
        QSqlQuery query{database};
        bool res = query.prepare(
            QStringLiteral("UPDATE Resources SET noteGuid = NULL "
                           "WHERE resourceLocalUid = :resourceLocalUid"));
        ASSERT_TRUE(res);

        query.bindValue(
            QStringLiteral(":resourceLocalUid"), resource.localId());
        res = query.exec();
        ASSERT_TRUE(res);
    }

    changeDatabaseVersionTo2(database);

    const auto versionHandler =
        std::make_shared<VersionHandler>(account, m_connectionPool, m_thread);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 2);

    const auto patch = std::make_shared<Patch2To3>(
        std::move(account), m_connectionPool, m_thread);

    auto applyFuture = patch->apply();
    ASSERT_NO_THROW(applyFuture.waitForFinished());

    const auto sortItems = [](auto & items) {
        std::sort(
            items.begin(), items.end(), [](const auto & lhs, const auto & rhs) {
                return lhs.localId() < rhs.localId();
            });
    };

    const QList<qevercloud::Note> expectedNotes = [&] {
        QList<qevercloud::Note> result;
        result.reserve(localNotes.size() + notes.size());
        for (const auto & note: std::as_const(localNotes)) {
            result << note;
        }

        for (auto note: std::as_const(notes)) {
            if (note.resources()) {
                for (auto & resource: *note.mutableResources()) {
                    if (!resource.noteGuid()) {
                        resource.setNoteGuid(note.guid());
                    }
                }
            }
            result << note;
        }

        sortItems(result);
        return result;
    }();

    const QList<qevercloud::Resource> expectedResources = [&] {
        QList<qevercloud::Resource> result;
        result.reserve(
            localResources.size() + resourcesWithNoteGuids.size() +
            resourcesWithoutNoteGuids.size());

        for (const auto & resource: std::as_const(localResources)) {
            result << resource;
        }

        for (const auto & resource: std::as_const(resourcesWithNoteGuids)) {
            result << resource;
        }

        for (auto resource: std::as_const(resourcesWithoutNoteGuids)) {
            const auto noteIt = std::find_if(
                notes.constBegin(), notes.constEnd(),
                [&resource](const qevercloud::Note & note) {
                    return resource.noteLocalId() == note.localId();
                });
            EXPECT_NE(noteIt, notes.constEnd());
            if (noteIt != notes.constEnd()) {
                resource.setNoteGuid(noteIt->guid());
                result << resource;
            }
        }

        sortItems(result);
        return result;
    }();

    auto notesFromLocalStorageFuture = notesHandler->listNotes(
        INotesHandler::FetchNoteOptions{} |
            INotesHandler::FetchNoteOption::WithResourceMetadata,
        INotesHandler::ListNotesOptions{});

    notesFromLocalStorageFuture.waitForFinished();
    ASSERT_EQ(notesFromLocalStorageFuture.resultCount(), 1);

    auto notesFromLocalStorage = notesFromLocalStorageFuture.result();
    sortItems(notesFromLocalStorage);
    EXPECT_EQ(notesFromLocalStorage, expectedNotes);

    const QList<qevercloud::Resource> resourcesFromLocalStorage = [&] {
        QList<qevercloud::Resource> result;
        result.reserve(
            localResources.size() + resourcesWithNoteGuids.size() +
            resourcesWithoutNoteGuids.size());

        QStringList resourceLocalIds;
        resourceLocalIds.reserve(
            localResources.size() + resourcesWithNoteGuids.size() +
            resourcesWithoutNoteGuids.size());

        for (const auto & resource: std::as_const(localResources)) {
            resourceLocalIds << resource.localId();
        }

        for (const auto & resource: std::as_const(resourcesWithNoteGuids)) {
            resourceLocalIds << resource.localId();
        }

        for (const auto & resource: std::as_const(resourcesWithoutNoteGuids)) {
            resourceLocalIds << resource.localId();
        }

        for (const auto & localId: std::as_const(resourceLocalIds)) {
            auto findResourceFuture =
                resourcesHandler->findResourceByLocalId(localId);
            findResourceFuture.waitForFinished();
            EXPECT_EQ(findResourceFuture.resultCount(), 1);
            if (findResourceFuture.resultCount() != 1) {
                continue;
            }

            EXPECT_TRUE(findResourceFuture.result());
            if (!findResourceFuture.result()) {
                continue;
            }

            result << *findResourceFuture.result();
        }

        sortItems(result);
        return result;
    }();

    EXPECT_EQ(resourcesFromLocalStorage, expectedResources);

    versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 3);
}

} // namespace quentier::local_storage::sql::tests
