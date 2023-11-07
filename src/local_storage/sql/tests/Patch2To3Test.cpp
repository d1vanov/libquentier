/*
 * Copyright 2021-2023 Dmitry Ivanov
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
#include "../VersionHandler.h"
#include "../patches/Patch2To3.h"
#include "../utils/ResourceDataFilesUtils.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/ApplicationSettings.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/UidGenerator.h>

#include <gtest/gtest.h>

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

#include <array>
#include <functional>
#include <string>

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

        m_writerThread = std::make_shared<QThread>();
        {
            auto nullDeleter = []([[maybe_unused]] QThreadPool * threadPool) {};
            m_threadPool = std::shared_ptr<QThreadPool>(
                QThreadPool::globalInstance(), std::move(nullDeleter));
        }

        m_resourceDataFilesLock = std::make_shared<QReadWriteLock>();

        m_notifier = new Notifier;
        m_notifier->moveToThread(m_writerThread.get());

        QObject::connect(
            m_writerThread.get(), &QThread::finished, m_notifier,
            &QObject::deleteLater);

        m_writerThread->start();

        qputenv(gAppPersistentStoragePath, m_temporaryDir.path().toUtf8());
    }

    void TearDown() override
    {
        qunsetenv(gAppPersistentStoragePath);

        m_writerThread->quit();
        m_writerThread->wait();

        // Give lambdas connected to threads finished signal a chance to fire
        QCoreApplication::processEvents();
    }

public:
protected:
    ConnectionPoolPtr m_connectionPool;
    threading::QThreadPtr m_writerThread;
    threading::QThreadPoolPtr m_threadPool;
    QReadWriteLockPtr m_resourceDataFilesLock;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

TEST_F(Patch2To3Test, Ctor)
{
    Account account{gTestAccountName, Account::Type::Local};

    EXPECT_NO_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), m_connectionPool, m_writerThread));
}

TEST_F(Patch2To3Test, CtorEmptyAccount)
{
    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            Account{}, m_connectionPool, m_writerThread),
        IQuentierException);
}

TEST_F(Patch2To3Test, CtorNullConnectionPool)
{
    Account account{gTestAccountName, Account::Type::Local};

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), nullptr, m_writerThread),
        IQuentierException);
}

TEST_F(Patch2To3Test, CtorNullWriterThread)
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
    QString m_localStorageDirPath;
};

enum PrepareLocalStorageForUpgradeFlag
{
    RemoveResourceVersionIdsTables = 1 << 0,
    MoveResourceBodyFiles = 1 << 1
};

Q_DECLARE_FLAGS(
    PrepareLocalStorageForUpgradeFlags, PrepareLocalStorageForUpgradeFlag);

// Prepares local storage database corresponding to version 2 in a temporary
// dir so that it can be upgraded from version 2 to version 3
[[nodiscard]] ResourcesTestData prepareResourcesForUpgrade(
    const QString & localStorageDirPath,
    const PrepareLocalStorageForUpgradeFlags flags,
    const ConnectionPoolPtr & connectionPool,
    const threading::QThreadPoolPtr & threadPool,
    const threading::QThreadPtr & writerThread,
    const QReadWriteLockPtr & resourceDataFilesLock, Notifier * notifier)
{
    // Prepare tables within the database
    utils::prepareLocalStorage(localStorageDirPath, *connectionPool);

    // Put some data into the local storage database
    const auto now = QDateTime::currentMSecsSinceEpoch();
    ResourcesTestData testData;

    testData.m_localStorageDirPath = [&] {
        QString path;
        QTextStream strm{&path};

        strm << localStorageDirPath << "/LocalAccounts/" << gTestAccountName;
        return path;
    }();

    QDir localStorageDir{testData.m_localStorageDirPath};
    if (!localStorageDir.exists()) {
        EXPECT_TRUE(localStorageDir.mkpath(testData.m_localStorageDirPath));
    }

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        connectionPool, threadPool, notifier, writerThread,
        testData.m_localStorageDirPath, resourceDataFilesLock);

    testData.m_notebook.setGuid(UidGenerator::Generate());
    testData.m_notebook.setName(QStringLiteral("name"));
    testData.m_notebook.setUpdateSequenceNum(1);
    testData.m_notebook.setServiceCreated(now);
    testData.m_notebook.setServiceUpdated(now);

    auto putNotebookFuture = notebooksHandler->putNotebook(testData.m_notebook);
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

    const auto notesHandler = std::make_shared<NotesHandler>(
        connectionPool, threadPool, notifier, writerThread,
        testData.m_localStorageDirPath, resourceDataFilesLock);

    auto putNoteFuture = notesHandler->putNote(testData.m_note);
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
<recoIndex docType="picture" objType="ink" objID="a284273e482578224145f2560b67bf45"
        engineVersion="3.0.17.14" recoType="client" lang="en" objWidth="1936" objHeight="2592">
    <item x="853" y="1278" w="14" h="17">
        <t w="31">II</t>
        <t w="31">11</t>
        <t w="31">ll</t>
        <t w="31">Il</t>
    </item>
    <item x="501" y="635" w="770" h="254" offset="12" duration="17" strokeList="14,28,19,41,54">
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

    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        connectionPool, threadPool, notifier, writerThread,
        testData.m_localStorageDirPath, resourceDataFilesLock);

    auto putFirstResourceFuture =
        resourcesHandler->putResource(testData.m_firstResource);

    putFirstResourceFuture.waitForFinished();

    auto putSecondResourceFuture =
        resourcesHandler->putResource(testData.m_secondResource);

    putSecondResourceFuture.waitForFinished();

    auto putThirdResourceFuture =
        resourcesHandler->putResource(testData.m_thirdResource);

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
                strm << testData.m_localStorageDirPath << "/Resources/"
                     << dataPathPart << "/" << testData.m_note.localId() << "/"
                     << resource.localId() << "/" << versionId << ".dat";
                return result;
            }();

            const QString pathTo = [&] {
                QString result;
                QTextStream strm{&result};
                strm << testData.m_localStorageDirPath << "/Resources/"
                     << dataPathPart << "/" << testData.m_note.localId() << "/"
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
                strm << testData.m_localStorageDirPath << "/Resources/"
                     << dataPathPart << "/" << testData.m_note.localId() << "/"
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
            PrepareLocalStorageForUpgradeFlag::MoveResourceBodyFiles)) {
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
    PrepareLocalStorageForUpgradeFlags flags = {};
    std::function<void()> prepareFunc = {};
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

            ApplicationSettings databaseUpgradeInfo{
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

            ApplicationSettings databaseUpgradeInfo{
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

TEST_P(Patch2To3ResourcesTest, ApplyResourcesPatch)
{
    Account account{gTestAccountName, Account::Type::Local};

    const auto data = GetParam();
    const auto testData = prepareResourcesForUpgrade(
        m_temporaryDir.path(), data.flags, m_connectionPool, m_threadPool,
        m_writerThread, m_resourceDataFilesLock, m_notifier);
    if (data.prepareFunc) {
        data.prepareFunc();
    }

    const auto versionHandler = std::make_shared<VersionHandler>(
        account, m_connectionPool, m_threadPool, m_writerThread);

    auto versionFuture = versionHandler->version();
    versionFuture.waitForFinished();
    EXPECT_EQ(versionFuture.result(), 2);

    const auto patch = std::make_shared<Patch2To3>(
        std::move(account), m_connectionPool, m_writerThread);

    auto applyFuture = patch->apply();
    EXPECT_NO_THROW(applyFuture.waitForFinished());

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_threadPool, m_notifier, m_writerThread,
        testData.m_localStorageDirPath, m_resourceDataFilesLock);

    auto notebookCountFuture = notebooksHandler->notebookCount();
    notebookCountFuture.waitForFinished();
    EXPECT_EQ(notebookCountFuture.result(), 1U);

    auto findNotebookFuture =
        notebooksHandler->findNotebookByLocalId(testData.m_notebook.localId());

    findNotebookFuture.waitForFinished();
    ASSERT_EQ(findNotebookFuture.resultCount(), 1);
    EXPECT_EQ(findNotebookFuture.result(), testData.m_notebook);

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_threadPool, m_notifier, m_writerThread,
        testData.m_localStorageDirPath, m_resourceDataFilesLock);

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

    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier, m_writerThread,
        testData.m_localStorageDirPath, m_resourceDataFilesLock);

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

} // namespace quentier::local_storage::sql::tests
