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

#include "../ResourcesHandler.h"
#include "../ConnectionPool.h"
#include "../NotebooksHandler.h"
#include "../NotesHandler.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFlags>
#include <QFutureSynchronizer>
#include <QGlobalStatic>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <array>
#include <utility>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

using namespace std::string_literals;

class ResourcesHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit ResourcesHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    [[nodiscard]] const QList<qevercloud::Resource> & putResources()
        const noexcept
    {
        return m_putResources;
    }

    [[nodiscard]] const QList<qevercloud::Resource> & putResourceMetadata()
        const noexcept
    {
        return m_putResourceMetadata;
    }

    [[nodiscard]] const QStringList & expungedResourceLocalIds() const noexcept
    {
        return m_expungedResourceLocalIds;
    }

public Q_SLOTS:
    void onResourcePut(qevercloud::Resource resource) // NOLINT
    {
        m_putResources << resource;
    }

    void onResourceMetadataPut(qevercloud::Resource resource) // NOLINT
    {
        m_putResourceMetadata << resource;
    }

    void onResourceExpunged(QString resourceLocalId) // NOLINT
    {
        m_expungedResourceLocalIds << resourceLocalId;
    }

private:
    QList<qevercloud::Resource> m_putResources;
    QList<qevercloud::Resource> m_putResourceMetadata;
    QStringList m_expungedResourceLocalIds;
};

namespace {

[[nodiscard]] qevercloud::Notebook createNotebook()
{
    qevercloud::Notebook notebook;
    notebook.setGuid(UidGenerator::Generate());
    notebook.setName(QStringLiteral("name"));
    notebook.setUpdateSequenceNum(1);

    const auto now = QDateTime::currentMSecsSinceEpoch();
    notebook.setServiceCreated(now);
    notebook.setServiceUpdated(now);

    return notebook;
}

[[nodiscard]] qevercloud::Note createNote(const qevercloud::Notebook & notebook)
{
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

    const auto now = QDateTime::currentMSecsSinceEpoch();
    note.setCreated(now);
    note.setUpdated(now);

    return note;
}

enum class CreateResourceOption
{
    WithData = 1 << 0,
    WithAlternateData = 1 << 1,
    WithRecognitionData = 1 << 2,
    WithAttributes = 1 << 3
};

Q_DECLARE_FLAGS(CreateResourceOptions, CreateResourceOption);

[[nodiscard]] qevercloud::Resource createResource(
    QString noteLocalId, std::optional<qevercloud::Guid> noteGuid,
    const CreateResourceOptions createResourceOptions = {})
{
    qevercloud::Resource resource;
    resource.setLocallyModified(true);

    if (noteGuid) {
        resource.setGuid(UidGenerator::Generate());
        resource.setUpdateSequenceNum(42);
    }

    resource.setNoteLocalId(std::move(noteLocalId));
    resource.setNoteGuid(std::move(noteGuid));

    resource.setMime("application/text-plain");

    resource.setWidth(10);
    resource.setHeight(20);

    if (createResourceOptions.testFlag(
            CreateResourceOption::WithRecognitionData)) {
        resource.setRecognition(qevercloud::Data{});
        auto & recognition = *resource.mutableRecognition();
        recognition.setBody(QByteArray::fromStdString(
            "<recoIndex docType=\"handwritten\" objType=\"image\" "
            "objID=\"fc83e58282d8059be17debabb69be900\" "
            "engineVersion=\"5.5.22.7\" recoType=\"service\" "
            "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
            "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
            "<t w=\"87\">INFO ?</t>"
            "<t w=\"83\">INFORMATION</t>"
            "<t w=\"82\">LNFOPWATION</t>"
            "<t w=\"71\">LNFOPMATION</t>"
            "<t w=\"67\">LNFOPWATJOM</t>"
            "<t w=\"67\">LMFOPWAFJOM</t>"
            "<t w=\"62\">ΕΊΝΑΙ ένα</t>"
            "</item>"
            "<item x=\"1850\" y=\"1465\" w=\"14\" h=\"12\">"
            "<t w=\"11\">et</t>"
            "<t w=\"10\">TQ</t>"
            "</item>"
            "</recoIndex>"s));
        recognition.setSize(recognition.body()->size());
        recognition.setBodyHash(QCryptographicHash::hash(
            *recognition.body(), QCryptographicHash::Md5));
    }

    if (createResourceOptions.testFlag(CreateResourceOption::WithData)) {
        resource.setData(qevercloud::Data{});
        auto & data = *resource.mutableData();
        data.setBody(QByteArray::fromStdString("test resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));
    }

    if (createResourceOptions.testFlag(CreateResourceOption::WithAlternateData))
    {
        resource.setAlternateData(qevercloud::Data{});
        auto & alternateData = *resource.mutableAlternateData();
        alternateData.setBody(
            QByteArray::fromStdString("test resource alternate data"s));

        alternateData.setSize(alternateData.body()->size());

        alternateData.setBodyHash(QCryptographicHash::hash(
            *alternateData.body(), QCryptographicHash::Md5));
    }

    if (createResourceOptions.testFlag(CreateResourceOption::WithAttributes)) {
        qevercloud::ResourceAttributes resourceAttributes;
        resourceAttributes.setSourceURL(
            QStringLiteral("https://www.example.com"));

        resourceAttributes.setTimestamp(QDateTime::currentMSecsSinceEpoch());
        resourceAttributes.setLatitude(55.0);
        resourceAttributes.setLongitude(38.2);
        resourceAttributes.setAltitude(0.2);
        resourceAttributes.setCameraMake(QStringLiteral("cameraMake"));
        resourceAttributes.setCameraModel(QStringLiteral("cameraModel"));
        resourceAttributes.setClientWillIndex(false);
        resourceAttributes.setFileName(QStringLiteral("resourceFileName"));
        resourceAttributes.setAttachment(false);

        resourceAttributes.setApplicationData(qevercloud::LazyMap{});
        auto & appData = *resourceAttributes.mutableApplicationData();
        appData.setKeysOnly(
            QSet<QString>{} << QStringLiteral("key1")
                            << QStringLiteral("key2"));
        appData.setFullMap(QMap<QString, QString>{});
        auto & fullMap = *appData.mutableFullMap();
        fullMap[QStringLiteral("key1")] = QStringLiteral("value1");
        fullMap[QStringLiteral("key2")] = QStringLiteral("value2");

        resource.setAttributes(std::move(resourceAttributes));
    }

    return resource;
}

class ResourcesHandlerTest : public testing::Test
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
    threading::QThreadPtr m_writerThread;
    threading::QThreadPoolPtr m_threadPool;
    QReadWriteLockPtr m_resourceDataFilesLock;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

} // namespace

TEST_F(ResourcesHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock));
}

TEST_F(ResourcesHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            nullptr, m_threadPool, m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, nullptr, m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, m_threadPool, nullptr,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            nullptr, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, CtorNullResourceDataFilesLock)
{
    EXPECT_THROW(
        const auto resourcesHandler = std::make_shared<ResourcesHandler>(
            m_connectionPool, m_threadPool, m_notifier,
            m_writerThread, m_temporaryDir.path(), nullptr),
        IQuentierException);
}

TEST_F(ResourcesHandlerTest, ShouldHaveZeroResourceCountWhenThereAreNoResources)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto resourceCountFuture = resourcesHandler->resourceCount(
        ILocalStorage::NoteCountOptions{
            ILocalStorage::NoteCountOption::IncludeDeletedNotes} |
        ILocalStorage::NoteCountOption::IncludeNonDeletedNotes);

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 0U);
}

TEST_F(ResourcesHandlerTest, ShouldNotFindNonexistentResourceByLocalId)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto resourceFuture =
        resourcesHandler->findResourceByLocalId(UidGenerator::Generate());

    resourceFuture.waitForFinished();
    ASSERT_EQ(resourceFuture.resultCount(), 1);
    EXPECT_FALSE(resourceFuture.result());

    resourceFuture = resourcesHandler->findResourceByLocalId(
        UidGenerator::Generate(),
        ILocalStorage::FetchResourceOption::WithBinaryData);

    resourceFuture.waitForFinished();
    ASSERT_EQ(resourceFuture.resultCount(), 1);
    EXPECT_FALSE(resourceFuture.result());
}

TEST_F(ResourcesHandlerTest, ShouldNotFindNonexistentResourceByGuid)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto resourceFuture =
        resourcesHandler->findResourceByGuid(UidGenerator::Generate());

    resourceFuture.waitForFinished();
    ASSERT_EQ(resourceFuture.resultCount(), 1);
    EXPECT_FALSE(resourceFuture.result());

    resourceFuture = resourcesHandler->findResourceByGuid(
        UidGenerator::Generate(),
        ILocalStorage::FetchResourceOption::WithBinaryData);

    resourceFuture.waitForFinished();
    ASSERT_EQ(resourceFuture.resultCount(), 1);
    EXPECT_FALSE(resourceFuture.result());
}

TEST_F(ResourcesHandlerTest, IgnoreAttemptToExpungeNonexistentResourceByLocalId)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeResourceFuture =
        resourcesHandler->expungeResourceByLocalId(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeResourceFuture.waitForFinished());
}

TEST_F(ResourcesHandlerTest, IgnoreAttemptToExpungeNonexistentResourceByGuid)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeResourceFuture =
        resourcesHandler->expungeResourceByGuid(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeResourceFuture.waitForFinished());
}

class ResourcesHandlerSingleResourceTest :
    public ResourcesHandlerTest,
    public testing::WithParamInterface<qevercloud::Resource>
{};

Q_GLOBAL_STATIC_WITH_ARGS(qevercloud::Notebook, gNotebook, (createNotebook()));
Q_GLOBAL_STATIC_WITH_ARGS(qevercloud::Note, gNote, (createNote(*gNotebook)));

const std::array gResourceTestValues{
    createResource(gNote->localId(), gNote->guid()),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithData),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithAlternateData),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithRecognitionData),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithAttributes),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithData |
            CreateResourceOption::WithAlternateData),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithData |
            CreateResourceOption::WithRecognitionData),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithData |
            CreateResourceOption::WithAlternateData |
            CreateResourceOption::WithAttributes),
    createResource(
        gNote->localId(), gNote->guid(),
        CreateResourceOptions{} | CreateResourceOption::WithData |
            CreateResourceOption::WithAlternateData |
            CreateResourceOption::WithRecognitionData |
            CreateResourceOption::WithAttributes),
};

INSTANTIATE_TEST_SUITE_P(
    ResourcesHandlerSingleResourceTestInstance,
    ResourcesHandlerSingleResourceTest, testing::ValuesIn(gResourceTestValues));

TEST_P(ResourcesHandlerSingleResourceTest, HandleSingleResource)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    ResourcesHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::resourcePut, &notifierListener,
        &ResourcesHandlerTestNotifierListener::onResourcePut);

    QObject::connect(
        m_notifier, &Notifier::resourceMetadataPut, &notifierListener,
        &ResourcesHandlerTestNotifierListener::onResourceMetadataPut);

    QObject::connect(
        m_notifier, &Notifier::resourceExpunged, &notifierListener,
        &ResourcesHandlerTestNotifierListener::onResourceExpunged);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNotebookFuture = notebooksHandler->putNotebook(*gNotebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNoteFuture = notesHandler->putNote(*gNote);
    putNoteFuture.waitForFinished();

    auto resource = GetParam();

    auto putResourceFuture = resourcesHandler->putResource(resource);
    putResourceFuture.waitForFinished();

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putResources().size(), 1);
    EXPECT_EQ(notifierListener.putResources()[0], resource);

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    const auto noteCountOptions =
        NoteCountOptions{} | NoteCountOption::IncludeNonDeletedNotes;

    auto resourceCountFuture =
        resourcesHandler->resourceCount(noteCountOptions);

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 1);

    resourceCountFuture =
        resourcesHandler->resourceCountPerNoteLocalId(gNote->localId());

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 1);

    resourceCountFuture =
        resourcesHandler->resourceCountPerNoteLocalId(UidGenerator::Generate());

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 0);

    using FetchResourceOption = ILocalStorage::FetchResourceOption;
    using FetchResourceOptions = ILocalStorage::FetchResourceOptions;

    const auto fetchResourceOptions =
        FetchResourceOptions{} | FetchResourceOption::WithBinaryData;

    auto foundByLocalIdResourceFuture = resourcesHandler->findResourceByLocalId(
        resource.localId(), fetchResourceOptions);

    foundByLocalIdResourceFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdResourceFuture.resultCount(), 1);
    ASSERT_TRUE(foundByLocalIdResourceFuture.result());
    EXPECT_EQ(foundByLocalIdResourceFuture.result(), resource);

    auto foundByGuidResourceFuture = resourcesHandler->findResourceByGuid(
        resource.guid().value(), fetchResourceOptions);

    foundByGuidResourceFuture.waitForFinished();
    ASSERT_EQ(foundByGuidResourceFuture.resultCount(), 1);
    ASSERT_TRUE(foundByGuidResourceFuture.result());
    EXPECT_EQ(foundByGuidResourceFuture.result(), resource);

    resource.setUpdateSequenceNum(resource.updateSequenceNum().value() + 1);

    putResourceFuture = resourcesHandler->putResourceMetadata(resource);
    putResourceFuture.waitForFinished();

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putResourceMetadata().size(), 1);
    EXPECT_EQ(notifierListener.putResourceMetadata()[0], resource);

    resourceCountFuture = resourcesHandler->resourceCount(noteCountOptions);

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 1);

    resourceCountFuture =
        resourcesHandler->resourceCountPerNoteLocalId(gNote->localId());

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 1);

    foundByLocalIdResourceFuture = resourcesHandler->findResourceByLocalId(
        resource.localId(), fetchResourceOptions);

    foundByLocalIdResourceFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdResourceFuture.resultCount(), 1);
    ASSERT_TRUE(foundByLocalIdResourceFuture.result());
    EXPECT_EQ(foundByLocalIdResourceFuture.result(), resource);

    foundByGuidResourceFuture = resourcesHandler->findResourceByGuid(
        resource.guid().value(), fetchResourceOptions);

    foundByGuidResourceFuture.waitForFinished();
    ASSERT_EQ(foundByGuidResourceFuture.resultCount(), 1);
    ASSERT_TRUE(foundByGuidResourceFuture.result());
    EXPECT_EQ(foundByGuidResourceFuture.result(), resource);

    auto expungeResourceByLocalIdFuture =
        resourcesHandler->expungeResourceByLocalId(resource.localId());
    expungeResourceByLocalIdFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedResourceLocalIds().size(), 1);
    EXPECT_EQ(
        notifierListener.expungedResourceLocalIds().at(0), resource.localId());

    const auto checkResourceExpunged = [&] {
        resourceCountFuture = resourcesHandler->resourceCount(noteCountOptions);

        resourceCountFuture.waitForFinished();
        EXPECT_EQ(resourceCountFuture.result(), 0);

        resourceCountFuture =
            resourcesHandler->resourceCountPerNoteLocalId(gNote->localId());

        resourceCountFuture.waitForFinished();
        EXPECT_EQ(resourceCountFuture.result(), 0);

        foundByLocalIdResourceFuture = resourcesHandler->findResourceByLocalId(
            resource.localId(), fetchResourceOptions);

        foundByLocalIdResourceFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdResourceFuture.resultCount(), 1);
        EXPECT_FALSE(foundByLocalIdResourceFuture.result());

        foundByGuidResourceFuture = resourcesHandler->findResourceByGuid(
            resource.guid().value(), fetchResourceOptions);

        foundByGuidResourceFuture.waitForFinished();
        ASSERT_EQ(foundByGuidResourceFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidResourceFuture.result());
    };

    checkResourceExpunged();

    putResourceFuture = resourcesHandler->putResource(resource);
    putResourceFuture.waitForFinished();

    auto expungeResourceByGuidFuture =
        resourcesHandler->expungeResourceByGuid(resource.guid().value());

    expungeResourceByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedResourceLocalIds().size(), 2);
    EXPECT_EQ(
        notifierListener.expungedResourceLocalIds().at(1), resource.localId());

    checkResourceExpunged();
}

TEST_F(ResourcesHandlerTest, HandleMultipleResources)
{
    const auto resourcesHandler = std::make_shared<ResourcesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    ResourcesHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::resourcePut, &notifierListener,
        &ResourcesHandlerTestNotifierListener::onResourcePut);

    QObject::connect(
        m_notifier, &Notifier::resourceMetadataPut, &notifierListener,
        &ResourcesHandlerTestNotifierListener::onResourceMetadataPut);

    QObject::connect(
        m_notifier, &Notifier::resourceExpunged, &notifierListener,
        &ResourcesHandlerTestNotifierListener::onResourceExpunged);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNotebookFuture = notebooksHandler->putNotebook(*gNotebook);
    putNotebookFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, m_threadPool, m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNoteFuture = notesHandler->putNote(*gNote);
    putNoteFuture.waitForFinished();

    auto resources = gResourceTestValues;
    auto resourceCounter = 2;
    for (auto it = std::next(resources.begin()); // NOLINT
         it != resources.end(); ++it)
    {
        auto & resource = *it;
        resource.setLocalId(UidGenerator::Generate());
        resource.setGuid(UidGenerator::Generate());

        resource.setUpdateSequenceNum(resourceCounter);
        ++resourceCounter;
    }

    QFutureSynchronizer<void> putResourcesSynchronizer;
    for (auto resource: resources) {
        auto putResourceFuture =
            resourcesHandler->putResource(std::move(resource));
        putResourcesSynchronizer.addFuture(putResourceFuture);
    }

    EXPECT_NO_THROW(putResourcesSynchronizer.waitForFinished());

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putResources().size(), resources.size());

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    const auto noteCountOptions =
        NoteCountOptions{} | NoteCountOption::IncludeNonDeletedNotes;

    auto resourceCountFuture =
        resourcesHandler->resourceCount(noteCountOptions);

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), resources.size());

    resourceCountFuture =
        resourcesHandler->resourceCountPerNoteLocalId(gNote->localId());

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), resources.size());

    resourceCountFuture =
        resourcesHandler->resourceCountPerNoteLocalId(UidGenerator::Generate());

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 0);

    using FetchResourceOption = ILocalStorage::FetchResourceOption;
    using FetchResourceOptions = ILocalStorage::FetchResourceOptions;

    auto fetchResourceOptions =
        FetchResourceOptions{} | FetchResourceOption::WithBinaryData;

    for (const auto & resource: std::as_const(resources)) {
        auto foundByLocalIdResourceFuture =
            resourcesHandler->findResourceByLocalId(
                resource.localId(), fetchResourceOptions);

        foundByLocalIdResourceFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdResourceFuture.resultCount(), 1);
        ASSERT_TRUE(foundByLocalIdResourceFuture.result());
        EXPECT_EQ(foundByLocalIdResourceFuture.result(), resource);

        auto foundByGuidResourceFuture = resourcesHandler->findResourceByGuid(
            resource.guid().value(), fetchResourceOptions);

        foundByGuidResourceFuture.waitForFinished();
        ASSERT_EQ(foundByGuidResourceFuture.resultCount(), 1);
        ASSERT_TRUE(foundByGuidResourceFuture.result());
        EXPECT_EQ(foundByGuidResourceFuture.result(), resource);
    }

    fetchResourceOptions = FetchResourceOptions{};

    for (auto resource: std::as_const(resources)) {
        if (resource.data()) {
            resource.mutableData()->setBody(std::nullopt);
        }

        if (resource.alternateData()) {
            resource.mutableAlternateData()->setBody(std::nullopt);
        }

        auto foundByLocalIdResourceFuture =
            resourcesHandler->findResourceByLocalId(
                resource.localId(), fetchResourceOptions);

        foundByLocalIdResourceFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdResourceFuture.resultCount(), 1);
        ASSERT_TRUE(foundByLocalIdResourceFuture.result());
        EXPECT_EQ(foundByLocalIdResourceFuture.result(), resource);

        auto foundByGuidResourceFuture = resourcesHandler->findResourceByGuid(
            resource.guid().value(), fetchResourceOptions);

        foundByGuidResourceFuture.waitForFinished();
        ASSERT_EQ(foundByGuidResourceFuture.resultCount(), 1);
        ASSERT_TRUE(foundByGuidResourceFuture.result());
        EXPECT_EQ(foundByGuidResourceFuture.result(), resource);
    }

    QFutureSynchronizer<void> expungeResourcesSynchronizer;
    for (const auto & resource: std::as_const(resources)) {
        auto expungeResourceByLocalIdFuture =
            resourcesHandler->expungeResourceByLocalId(resource.localId());
        expungeResourcesSynchronizer.addFuture(expungeResourceByLocalIdFuture);
    }

    expungeResourcesSynchronizer.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(
        notifierListener.expungedResourceLocalIds().size(), resources.size());

    resourceCountFuture = resourcesHandler->resourceCount(noteCountOptions);
    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 0);

    resourceCountFuture =
        resourcesHandler->resourceCountPerNoteLocalId(gNote->localId());

    resourceCountFuture.waitForFinished();
    EXPECT_EQ(resourceCountFuture.result(), 0);
}

} // namespace quentier::local_storage::sql::tests

#include "ResourcesHandlerTest.moc"
