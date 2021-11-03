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

#include "Utils.h"

#include "../ConnectionPool.h"
#include "../ErrorHandling.h"
#include "../Fwd.h"
#include "../NotebooksHandler.h"
#include "../NotesHandler.h"
#include "../Notifier.h"
#include "../ResourcesHandler.h"
#include "../TablesInitializer.h"
#include "../patches/Patch2To3.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/FileSystem.h>
#include <quentier/utility/StandardPaths.h>
#include <quentier/utility/UidGenerator.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QReadWriteLock>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QtGlobal>

#include <string>

namespace quentier::local_storage::sql::tests {

using namespace std::string_literals;

namespace {

const QString gTestDbConnectionName =
    QStringLiteral("libquentier_local_storage_sql_patch2to3_test_db");

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
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tests::Patch2To3Test",
            "Failed to drop ResourceDataBodyVersionIds table"));

    res = query.exec(QStringLiteral(
        "DROP TABLE IF EXISTS ResourceAlternateDataBodyVersionIds"));

    ENSURE_DB_REQUEST_THROW(
        res, query, "local_storage::sql::tests::Patch2To3Test",
        QT_TRANSLATE_NOOP(
            "quentier::local_storage::sql::tests::Patch2To3Test",
            "Failed to drop ResourceAlternateDataBodyVersionIds table"));
}

// Prepares local storage database corresponding to version 2 in a temporary dir
// so that it can be upgraded from version 2 to version 3
// TODO: remove maybe_unused attribute later when more tests are added
[[maybe_unused]] void prepareLocalStorageForUpgrade(
    const QString & localStorageDirPath, ConnectionPoolPtr connectionPool)
{
    Q_ASSERT(connectionPool);

    // Prepare tables within the database
    utils::prepareLocalStorage(localStorageDirPath, *connectionPool);

    // Put some data into the local storage database
    QThreadPtr writerThread = std::make_shared<QThread>();

    Notifier * notifier = nullptr;
    {
        auto notifierPtr = std::make_unique<Notifier>();
        notifierPtr->moveToThread(writerThread.get());
        notifier = notifierPtr.release();
    }

    QObject::connect(
        writerThread.get(), &QThread::finished, notifier,
        &QObject::deleteLater);

    writerThread->start();

    auto resourceDataFilesLock = std::make_shared<QReadWriteLock>();

    const auto now = QDateTime::currentMSecsSinceEpoch();

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        connectionPool, QThreadPool::globalInstance(), notifier, writerThread,
        localStorageDirPath, resourceDataFilesLock);

    qevercloud::Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("name"));
    notebook.setUpdateSequenceNum(1);

    notebook.setServiceCreated(now);
    notebook.setServiceUpdated(now);

    auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
    putNotebookFuture.waitForFinished();

    qevercloud::Note note;
    note.setLocallyModified(true);
    note.setLocalOnly(false);
    note.setLocallyFavorited(true);

    note.setNotebookLocalId(notebook.localId());
    note.setNotebookGuid(notebook.guid());

    note.setGuid(UidGenerator::Generate());
    note.setUpdateSequenceNum(1);

    note.setTitle(QStringLiteral("Title"));

    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setContentHash(QCryptographicHash::hash(
        note.content()->toUtf8(), QCryptographicHash::Md5));

    note.setContentLength(note.content()->size());
    note.setCreated(now);
    note.setUpdated(now);

    const auto notesHandler = std::make_shared<NotesHandler>(
        connectionPool, QThreadPool::globalInstance(), notifier, writerThread,
        localStorageDirPath, resourceDataFilesLock);

    auto putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    qevercloud::Resource firstResource;
    firstResource.setLocallyModified(true);
    firstResource.setGuid(UidGenerator::Generate());
    firstResource.setUpdateSequenceNum(42);
    firstResource.setNoteLocalId(note.localId());
    firstResource.setNoteGuid(note.guid());
    firstResource.setMime("application/text-plain");
    firstResource.setWidth(10);
    firstResource.setHeight(20);

    firstResource.setData(qevercloud::Data{});
    {
        auto & data = *firstResource.mutableData();
        data.setBody(QByteArray::fromStdString("test first resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    qevercloud::Resource secondResource = firstResource;
    secondResource.setLocalId(UidGenerator::Generate());
    secondResource.setGuid(UidGenerator::Generate());
    secondResource.setUpdateSequenceNum(
        secondResource.updateSequenceNum().value() + 1);

    {
        auto & data = secondResource.mutableData().value();
        data.setBody(QByteArray::fromStdString("test second resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    secondResource.setAlternateData(qevercloud::Data{});
    {
        auto & data = *secondResource.mutableAlternateData();
        data.setBody(
            QByteArray::fromStdString("test second resource alternate data"s));

        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    qevercloud::Resource thirdResource = secondResource;
    thirdResource.setLocalId(UidGenerator::Generate());
    thirdResource.setGuid(UidGenerator::Generate());
    thirdResource.setUpdateSequenceNum(
        thirdResource.updateSequenceNum().value() + 1);

    {
        auto & data = thirdResource.mutableData().value();
        data.setBody(QByteArray::fromStdString("test third resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    {
        auto & data = thirdResource.mutableAlternateData().value();
        data.setBody(
            QByteArray::fromStdString("test third resource alternate data"s));

        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    thirdResource.setRecognition(qevercloud::Data{});
    {
        auto & data = *thirdResource.mutableRecognition();
        data.setBody(QByteArray::fromStdString(
            R"___(
<?xml version="1.0" encoding="UTF-8"?>
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
        connectionPool, QThreadPool::globalInstance(), notifier, writerThread,
        localStorageDirPath, resourceDataFilesLock);

    int indexInNote = 0;

    auto putFirstResourceFuture =
        resourcesHandler->putResource(firstResource, indexInNote);

    putFirstResourceFuture.waitForFinished();

    ++indexInNote;

    auto putSecondResourceFuture =
        resourcesHandler->putResource(secondResource, indexInNote);

    putSecondResourceFuture.waitForFinished();

    ++indexInNote;

    auto putThirdResourceFuture =
        resourcesHandler->putResource(thirdResource, indexInNote);

    putThirdResourceFuture.waitForFinished();

    // Now need to mutate the data to make the local storage files layout
    // look like version 2.

    // TODO: move resource data files out of version id dirs to parent dir

    auto database = connectionPool->database();
    removeBodyVersionIdTables(database);
}

} // namespace

TEST(Patch2To3Test, Ctor)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_NO_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), std::move(connectionPool),
            std::move(pWriterThread)));
}

TEST(Patch2To3Test, CtorEmptyAccount)
{
    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            Account{}, std::move(connectionPool), std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch2To3Test, CtorNullConnectionPool)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto pWriterThread = std::make_shared<QThread>();

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), nullptr, std::move(pWriterThread)),
        IQuentierException);
}

TEST(Patch2To3Test, CtorNullWriterThread)
{
    Account account{*utils::gTestAccountName, Account::Type::Local};

    auto connectionPool = std::make_shared<ConnectionPool>(
        QStringLiteral("localhost"), QStringLiteral("user"),
        QStringLiteral("password"), QStringLiteral(":memory:"),
        QStringLiteral("QSQLITE"));

    EXPECT_THROW(
        const auto patch = std::make_shared<Patch2To3>(
            std::move(account), std::move(connectionPool), nullptr),
        IQuentierException);
}

} // namespace quentier::local_storage::sql::tests
