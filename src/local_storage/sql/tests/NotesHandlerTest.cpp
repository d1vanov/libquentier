/*
 * Copyright 2021-2022 Dmitry Ivanov
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
#include "../NotebooksHandler.h"
#include "../NotesHandler.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"
#include "../TagsHandler.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFlags>
#include <QFutureSynchronizer>
#include <QGlobalStatic>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThreadPool>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <string>

// clazy:excludeall=non-pod-global-static
// clazy:excludeall=returning-void-expression

namespace quentier::local_storage::sql::tests {

using namespace std::string_literals;

class NotesHandlerTestNotifierListener : public QObject
{
    Q_OBJECT
public:
    explicit NotesHandlerTestNotifierListener(QObject * parent = nullptr) :
        QObject(parent)
    {}

    using UpdatedNoteWithOptions =
        std::pair<qevercloud::Note, NotesHandler::UpdateNoteOptions>;

    [[nodiscard]] const QList<qevercloud::Note> & putNotes() const noexcept
    {
        return m_putNotes;
    }

    [[nodiscard]] const QList<UpdatedNoteWithOptions> &
        updatedNotesWithOptions() const noexcept
    {
        return m_updatedNotesWithOptions;
    }

    [[nodiscard]] const QStringList & expungedNoteLocalIds() const noexcept
    {
        return m_expungedNoteLocalIds;
    }

public Q_SLOTS:
    void onNotePut(qevercloud::Note note) // NOLINT
    {
        m_putNotes << note;
    }

    void onNoteUpdated(
        qevercloud::Note note, ILocalStorage::UpdateNoteOptions options)
    {
        m_updatedNotesWithOptions << std::make_pair(std::move(note), options);
    }

    void onNoteExpunged(QString noteLocalId) // NOLINT
    {
        m_expungedNoteLocalIds << noteLocalId;
    }

private:
    QList<qevercloud::Note> m_putNotes;
    QList<UpdatedNoteWithOptions> m_updatedNotesWithOptions;
    QStringList m_expungedNoteLocalIds;
};

namespace {

[[nodiscard]] QList<qevercloud::SharedNote> createSharedNotes(
    const qevercloud::Guid & noteGuid)
{
    const int sharedNoteCount = 5;
    QList<qevercloud::SharedNote> sharedNotes;
    sharedNotes.reserve(sharedNoteCount);
    for (int i = 0; i < sharedNoteCount; ++i) {
        qevercloud::SharedNote sharedNote;
        sharedNote.setSharerUserID(qevercloud::UserID{10});

        if (i % 2 == 0) {
            qevercloud::Identity recipientIdentity;
            recipientIdentity.setId(qevercloud::IdentityID{i * 20}); // NOLINT

            if (i % 4 == 0) {
                qevercloud::Contact contact;
                contact.setName(QStringLiteral("contactName"));
                contact.setId(QStringLiteral("contactId"));
                contact.setType(qevercloud::ContactType::EVERNOTE);
                contact.setPhotoUrl(QStringLiteral("https://www.example.com"));

                contact.setPhotoLastUpdated(
                    QDateTime::currentMSecsSinceEpoch());

                contact.setMessagingPermit(QByteArray::fromStdString("aaaa"s));

                contact.setMessagingPermitExpires(
                    QDateTime::currentMSecsSinceEpoch());

                recipientIdentity.setContact(std::move(contact));
            }

            recipientIdentity.setUserId(qevercloud::UserID{i * 50});
            recipientIdentity.setDeactivated(false);
            recipientIdentity.setSameBusiness(false);
            recipientIdentity.setBlocked(false);
            recipientIdentity.setUserConnected(true);
            recipientIdentity.setEventId(qevercloud::MessageEventID{35});

            sharedNote.setRecipientIdentity(std::move(recipientIdentity));
        }

        sharedNote.setPrivilege(
            qevercloud::SharedNotePrivilegeLevel::FULL_ACCESS);

        auto now = QDateTime::currentMSecsSinceEpoch();
        sharedNote.setServiceCreated(now - 2);
        sharedNote.setServiceUpdated(now - 1);
        sharedNote.setServiceAssigned(now);

        sharedNote.setNoteGuid(noteGuid);

        sharedNotes << sharedNote;
    }

    return sharedNotes;
}

[[nodiscard]] qevercloud::NoteRestrictions createNoteRestrictions()
{
    qevercloud::NoteRestrictions noteRestrictions;
    noteRestrictions.setNoUpdateTitle(false);
    noteRestrictions.setNoUpdateContent(true);
    noteRestrictions.setNoEmail(false);
    noteRestrictions.setNoShare(true);
    noteRestrictions.setNoSharePublicly(false);
    return noteRestrictions;
}

[[nodiscard]] qevercloud::NoteLimits createNoteLimits()
{
    qevercloud::NoteLimits noteLimits;
    noteLimits.setNoteResourceCountMax(10);
    noteLimits.setUploadLimit(10000);
    noteLimits.setResourceSizeMax(5000);
    noteLimits.setNoteSizeMax(8000);
    noteLimits.setUploaded(2000);
    return noteLimits;
}

[[nodiscard]] QList<qevercloud::Resource> createNoteResources(
    const QString & noteLocalId,
    const std::optional<qevercloud::Guid> & noteGuid)
{
    const int resourceCount = 5;
    QList<qevercloud::Resource> resources;
    resources.reserve(resourceCount);

    for (int i = 0; i < resourceCount; ++i) {
        qevercloud::Resource resource;
        resource.setLocallyModified(true);

        resource.setData(qevercloud::Data{});
        auto & data = *resource.mutableData();
        data.setBody(QByteArray::fromStdString("test resource data"s));
        data.setSize(data.body()->size());
        data.setBodyHash(
            QCryptographicHash::hash(*data.body(), QCryptographicHash::Md5));

        resource.setAlternateData(qevercloud::Data{});
        auto & alternateData = *resource.mutableAlternateData();
        alternateData.setBody(
            QByteArray::fromStdString("test resource alternate data"s));

        alternateData.setSize(alternateData.body()->size());

        alternateData.setBodyHash(QCryptographicHash::hash(
            *alternateData.body(), QCryptographicHash::Md5));

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

        resource.setMime("application/text-plain");

        resource.setWidth(10);
        resource.setHeight(20);

        resource.setNoteLocalId(noteLocalId);
        resource.setNoteGuid(noteGuid);
        if (noteGuid) {
            resource.setGuid(UidGenerator::Generate());
            resource.setUpdateSequenceNum(10 + i);
        }

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
        appData.setKeysOnly(QSet<QString>{} << QStringLiteral("key1"));
        appData.setFullMap(QMap<QString, QString>{});
        auto & fullMap = *appData.mutableFullMap();
        fullMap[QStringLiteral("key1")] = QStringLiteral("value1");

        resource.setAttributes(std::move(resourceAttributes));

        resources.push_back(resource);
    }

    return resources;
}

enum class CreateNoteOption
{
    WithTagLocalIds = 1 << 0,
    WithTagGuids = 1 << 1,
    WithSharedNotes = 1 << 2,
    WithRestrictions = 1 << 3,
    WithLimits = 1 << 4,
    WithResources = 1 << 5,
    Deleted = 1 << 6,
};

Q_DECLARE_FLAGS(CreateNoteOptions, CreateNoteOption);

[[nodiscard]] qevercloud::Note createNote(
    const qevercloud::Notebook & notebook,
    const CreateNoteOptions createNoteOptions = {})
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

    if (createNoteOptions.testFlag(CreateNoteOption::WithTagLocalIds)) {
        note.setTagLocalIds(
            QStringList{} << UidGenerator::Generate()
                          << UidGenerator::Generate());
    }

    if (createNoteOptions.testFlag(CreateNoteOption::WithTagGuids)) {
        note.setTagGuids(
            QList<qevercloud::Guid>{} << UidGenerator::Generate()
                                      << UidGenerator::Generate());
    }

    if (createNoteOptions.testFlag(CreateNoteOption::WithSharedNotes)) {
        note.setSharedNotes(createSharedNotes(note.guid().value()));
    }

    if (createNoteOptions.testFlag(CreateNoteOption::WithRestrictions)) {
        note.setRestrictions(createNoteRestrictions());
    }

    if (createNoteOptions.testFlag(CreateNoteOption::WithLimits)) {
        note.setLimits(createNoteLimits());
    }

    if (createNoteOptions.testFlag(CreateNoteOption::WithResources)) {
        note.setResources(createNoteResources(note.localId(), note.guid()));
    }

    if (createNoteOptions.testFlag(CreateNoteOption::Deleted)) {
        note.setDeleted(QDateTime::currentMSecsSinceEpoch());
    }

    return note;
}

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

class NotesHandlerTest : public testing::Test
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
    QThreadPtr m_writerThread;
    QReadWriteLockPtr m_resourceDataFilesLock;
    QTemporaryDir m_temporaryDir;
    Notifier * m_notifier;
};

} // namespace

TEST_F(NotesHandlerTest, Ctor)
{
    EXPECT_NO_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock));
}

TEST_F(NotesHandlerTest, CtorNullConnectionPool)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            nullptr, QThreadPool::globalInstance(), m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullThreadPool)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, nullptr, m_notifier, m_writerThread,
            m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullNotifier)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), nullptr,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullWriterThread)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            nullptr, m_temporaryDir.path(), m_resourceDataFilesLock),
        IQuentierException);
}

TEST_F(NotesHandlerTest, CtorNullResourceDataFilesLock)
{
    EXPECT_THROW(
        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), nullptr),
        IQuentierException);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroNonDeletedNoteCountWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCount(
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroDeletedNoteCountWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCount(
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroNoteCountWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCount(
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes} |
        NoteCountOption::IncludeNonDeletedNotes);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNonDeletedNoteCountPerNotebookLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroDeletedNoteCountPerNotebookLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNoteCountPerNotebookLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNonDeletedNoteCountPerTagLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerTagLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroDeletedNoteCountPerTagLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerTagLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeDeletedNotes});

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest, ShouldHaveZeroNoteCountPerTagLocalIdWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerTagLocalId(
        UidGenerator::Generate(),
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNoteCountsPerTagsWhenThereAreNeitherNotesNorTags)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto listTagsOptions = ILocalStorage::ListTagsOptions{};

    listTagsOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto noteCountsFuture = notesHandler->noteCountsPerTags(
        listTagsOptions,
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountsFuture.waitForFinished();
    EXPECT_EQ(noteCountsFuture.result().size(), 0);
}

TEST_F(
    NotesHandlerTest,
    ShouldHaveZeroNoteCountPerNotebookAndTagLocalidsWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto noteCountFuture = notesHandler->noteCountPerNotebookAndTagLocalIds(
        QStringList{}, QStringList{},
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);
}

TEST_F(NotesHandlerTest, ShouldNotFindNonexistentNoteByLocalId)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    auto noteFuture = notesHandler->findNoteByLocalId(
        UidGenerator::Generate(),
        FetchNoteOptions{FetchNoteOption::WithResourceMetadata});

    noteFuture.waitForFinished();
    ASSERT_EQ(noteFuture.resultCount(), 1);
    EXPECT_FALSE(noteFuture.result());
}

TEST_F(NotesHandlerTest, ShouldNotFindNonexistentNoteByGuid)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    auto noteFuture = notesHandler->findNoteByGuid(
        UidGenerator::Generate(),
        FetchNoteOptions{FetchNoteOption::WithResourceMetadata});

    noteFuture.waitForFinished();
    ASSERT_EQ(noteFuture.resultCount(), 1);
    EXPECT_FALSE(noteFuture.result());
}

TEST_F(NotesHandlerTest, IgnoreAttemptToExpungeNonexistentNoteByLocalId)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeNoteFuture =
        notesHandler->expungeNoteByLocalId(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNoteFuture.waitForFinished());
}

TEST_F(NotesHandlerTest, IgnoreAttemptToExpungeNonexistentNoteByGuid)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeNoteFuture =
        notesHandler->expungeNoteByGuid(UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNoteFuture.waitForFinished());
}

TEST_F(NotesHandlerTest, ShouldNotListSharedNotesForNonexistentNote)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto listSharedNotesFuture =
        notesHandler->listSharedNotes(UidGenerator::Generate());

    listSharedNotesFuture.waitForFinished();
    EXPECT_TRUE(listSharedNotesFuture.result().isEmpty());
}

TEST_F(NotesHandlerTest, ShouldNotListNotesWhenThereAreNoNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    auto listNotesOptions = ILocalStorage::ListNotesOptions{};

    listNotesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto notesFuture = notesHandler->listNotes(
        FetchNoteOptions{FetchNoteOption::WithResourceMetadata},
        listNotesOptions);

    notesFuture.waitForFinished();
    EXPECT_TRUE(notesFuture.result().isEmpty());
}

TEST_F(NotesHandlerTest, ShouldNotListNotesPerNonexistentNotebookLocalId)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    auto listNotesOptions = ILocalStorage::ListNotesOptions{};

    listNotesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto notesFuture = notesHandler->listNotesPerNotebookLocalId(
        UidGenerator::Generate(),
        FetchNoteOptions{FetchNoteOption::WithResourceMetadata},
        listNotesOptions);

    notesFuture.waitForFinished();
    EXPECT_TRUE(notesFuture.result().isEmpty());
}

TEST_F(NotesHandlerTest, ShouldNotListNotesPerNonexistentTagLocalId)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    auto listNotesOptions = ILocalStorage::ListNotesOptions{};

    listNotesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto notesFuture = notesHandler->listNotesPerTagLocalId(
        UidGenerator::Generate(),
        FetchNoteOptions{FetchNoteOption::WithResourceMetadata},
        listNotesOptions);

    notesFuture.waitForFinished();
    EXPECT_TRUE(notesFuture.result().isEmpty());
}

TEST_F(NotesHandlerTest, ShouldNotListNotesPerNonexistentNotebookAndTagLocalIds)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    auto listNotesOptions = ILocalStorage::ListNotesOptions{};

    listNotesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto notesFuture = notesHandler->listNotesPerNotebookAndTagLocalIds(
        QStringList{} << UidGenerator::Generate(),
        QStringList{} << UidGenerator::Generate(),
        FetchNoteOptions{FetchNoteOption::WithResourceMetadata},
        listNotesOptions);

    notesFuture.waitForFinished();
    EXPECT_TRUE(notesFuture.result().isEmpty());
}

TEST_F(NotesHandlerTest, ShouldNotListNotesForNonexistentNoteLocalIds)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    auto listNotesOptions = ILocalStorage::ListNotesOptions{};

    listNotesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto notesFuture = notesHandler->listNotesByLocalIds(
        QStringList{} << UidGenerator::Generate() << UidGenerator::Generate(),
        FetchNoteOptions{FetchNoteOption::WithResourceMetadata},
        listNotesOptions);

    notesFuture.waitForFinished();
    EXPECT_TRUE(notesFuture.result().isEmpty());
}

class NotesHandlerSingleNoteTest :
    public NotesHandlerTest,
    public testing::WithParamInterface<qevercloud::Note>
{};

Q_GLOBAL_STATIC_WITH_ARGS(qevercloud::Notebook, gNotebook, (createNotebook()));

const std::array gNoteTestValues{
    createNote(*gNotebook),
    createNote(
        *gNotebook, CreateNoteOptions{} | CreateNoteOption::WithTagLocalIds),
    createNote(
        *gNotebook, CreateNoteOptions{} | CreateNoteOption::WithTagGuids),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithTagLocalIds |
            CreateNoteOption::WithTagGuids),
    createNote(
        *gNotebook, CreateNoteOptions{} | CreateNoteOption::WithSharedNotes),
    createNote(
        *gNotebook, CreateNoteOptions{} | CreateNoteOption::WithRestrictions),
    createNote(*gNotebook, CreateNoteOptions{} | CreateNoteOption::WithLimits),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithSharedNotes |
            CreateNoteOption::WithRestrictions),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithSharedNotes |
            CreateNoteOption::WithLimits),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithRestrictions |
            CreateNoteOption::WithLimits),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithSharedNotes |
            CreateNoteOption::WithRestrictions | CreateNoteOption::WithLimits),
    createNote(
        *gNotebook, CreateNoteOptions{} | CreateNoteOption::WithResources),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithTagLocalIds |
            CreateNoteOption::WithResources),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithTagLocalIds |
            CreateNoteOption::WithTagGuids | CreateNoteOption::WithResources |
            CreateNoteOption::WithSharedNotes |
            CreateNoteOption::WithRestrictions | CreateNoteOption::WithLimits),
    createNote(*gNotebook, CreateNoteOption::Deleted),
    createNote(
        *gNotebook,
        CreateNoteOptions{} | CreateNoteOption::WithTagLocalIds |
            CreateNoteOption::WithTagGuids | CreateNoteOption::WithResources |
            CreateNoteOption::WithSharedNotes |
            CreateNoteOption::WithRestrictions | CreateNoteOption::WithLimits |
            CreateNoteOption::Deleted),
};

INSTANTIATE_TEST_SUITE_P(
    NotesHandlerSingleNoteTestInstance, NotesHandlerSingleNoteTest,
    testing::ValuesIn(gNoteTestValues));

TEST_P(NotesHandlerSingleNoteTest, HandleSingleNote)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    NotesHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::notePut, &notifierListener,
        &NotesHandlerTestNotifierListener::onNotePut);

    QObject::connect(
        m_notifier, &Notifier::noteUpdated, &notifierListener,
        &NotesHandlerTestNotifierListener::onNoteUpdated);

    QObject::connect(
        m_notifier, &Notifier::noteExpunged, &notifierListener,
        &NotesHandlerTestNotifierListener::onNoteExpunged);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNotebookFuture = notebooksHandler->putNotebook(*gNotebook);
    putNotebookFuture.waitForFinished();

    auto note = GetParam();

    if (!note.tagLocalIds().isEmpty() ||
        (note.tagGuids() && !note.tagGuids()->isEmpty()))
    {
        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread);

        if (!note.tagLocalIds().isEmpty()) {
            int index = 0;
            for (const auto & tagLocalId: qAsConst(note.tagLocalIds())) {
                qevercloud::Tag tag;
                tag.setLocalId(tagLocalId);
                if (note.tagGuids() && !note.tagGuids()->isEmpty()) {
                    tag.setGuid(note.tagGuids()->at(index));
                }

                auto putTagFuture = tagsHandler->putTag(tag);
                putTagFuture.waitForFinished();

                ++index;
            }
        }
        else if (note.tagGuids() && !note.tagGuids()->isEmpty()) {
            for (const auto & tagGuid: qAsConst(*note.tagGuids())) {
                qevercloud::Tag tag;
                tag.setGuid(tagGuid);

                auto putTagFuture = tagsHandler->putTag(tag);
                putTagFuture.waitForFinished();
            }
        }
    }

    auto putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putNotes().size(), 1);
    EXPECT_EQ(notifierListener.putNotes()[0], note);

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    auto noteCountOptions =
        (note.deleted()
             ? NoteCountOptions{NoteCountOption::IncludeDeletedNotes}
             : NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    auto noteCountFuture = notesHandler->noteCount(noteCountOptions);
    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 1U);

    noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        gNotebook->localId(), noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 1U);

    for (const auto & tagLocalId: qAsConst(note.tagLocalIds())) {
        noteCountFuture =
            notesHandler->noteCountPerTagLocalId(tagLocalId, noteCountOptions);

        noteCountFuture.waitForFinished();
        EXPECT_EQ(noteCountFuture.result(), 1U);
    }

    noteCountFuture = notesHandler->noteCountPerNotebookAndTagLocalIds(
        QStringList{} << gNotebook->localId(), note.tagLocalIds(),
        noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 1U);

    using FetchNoteOption = ILocalStorage::FetchNoteOption;
    using FetchNoteOptions = ILocalStorage::FetchNoteOptions;

    const auto fetchNoteOptions = FetchNoteOptions{} |
        FetchNoteOption::WithResourceMetadata |
        FetchNoteOption::WithResourceBinaryData;

    auto foundByLocalIdNoteFuture =
        notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

    foundByLocalIdNoteFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdNoteFuture.resultCount(), 1);
    ASSERT_TRUE(foundByLocalIdNoteFuture.result());

    if (note.tagLocalIds().isEmpty() && note.tagGuids() &&
        !note.tagGuids()->isEmpty())
    {
        EXPECT_FALSE(
            foundByLocalIdNoteFuture.result()->tagLocalIds().isEmpty());
        note.setTagLocalIds(foundByLocalIdNoteFuture.result()->tagLocalIds());
    }

    EXPECT_EQ(foundByLocalIdNoteFuture.result(), note);

    auto foundByGuidNoteFuture =
        notesHandler->findNoteByGuid(note.guid().value(), fetchNoteOptions);

    foundByGuidNoteFuture.waitForFinished();
    ASSERT_EQ(foundByGuidNoteFuture.resultCount(), 1);
    EXPECT_EQ(foundByGuidNoteFuture.result(), note);

    auto listNotesOptions = ILocalStorage::ListNotesOptions{};

    listNotesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listNotesFuture =
        notesHandler->listNotes(fetchNoteOptions, listNotesOptions);

    listNotesFuture.waitForFinished();
    ASSERT_EQ(listNotesFuture.result().size(), 1);
    EXPECT_EQ(listNotesFuture.result().at(0), note);

    auto sharedNotesFuture = notesHandler->listSharedNotes(note.guid().value());
    sharedNotesFuture.waitForFinished();

    EXPECT_EQ(
        sharedNotesFuture.result(),
        note.sharedNotes().value_or(QList<qevercloud::SharedNote>{}));

    listNotesFuture = notesHandler->listNotesPerNotebookLocalId(
        gNotebook->localId(), fetchNoteOptions, listNotesOptions);

    listNotesFuture.waitForFinished();
    ASSERT_EQ(listNotesFuture.result().size(), 1);
    EXPECT_EQ(listNotesFuture.result().at(0), note);

    for (const auto & tagLocalId: note.tagLocalIds()) {
        listNotesFuture = notesHandler->listNotesPerTagLocalId(
            tagLocalId, fetchNoteOptions, listNotesOptions);

        listNotesFuture.waitForFinished();
        ASSERT_EQ(listNotesFuture.result().size(), 1);
        EXPECT_EQ(listNotesFuture.result().at(0), note);
    }

    auto expungeNoteByLocalIdFuture =
        notesHandler->expungeNoteByLocalId(note.localId());

    expungeNoteByLocalIdFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedNoteLocalIds().size(), 1);
    EXPECT_EQ(notifierListener.expungedNoteLocalIds().at(0), note.localId());

    const auto checkNoteExpunged = [&] {
        noteCountOptions = NoteCountOptions{} |
            NoteCountOption::IncludeNonDeletedNotes |
            NoteCountOption::IncludeDeletedNotes;

        noteCountFuture = notesHandler->noteCount(noteCountOptions);
        noteCountFuture.waitForFinished();
        EXPECT_EQ(noteCountFuture.result(), 0U);

        noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
            gNotebook->localId(), noteCountOptions);

        noteCountFuture.waitForFinished();
        EXPECT_EQ(noteCountFuture.result(), 0U);

        for (const auto & tagLocalId: qAsConst(note.tagLocalIds())) {
            noteCountFuture = notesHandler->noteCountPerTagLocalId(
                tagLocalId, noteCountOptions);

            noteCountFuture.waitForFinished();
            EXPECT_EQ(noteCountFuture.result(), 0U);
        }

        noteCountFuture = notesHandler->noteCountPerNotebookAndTagLocalIds(
            QStringList{} << gNotebook->localId(), note.tagLocalIds(),
            noteCountOptions);

        noteCountFuture.waitForFinished();
        EXPECT_EQ(noteCountFuture.result(), 0U);

        auto foundByLocalIdNoteFuture =
            notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

        foundByLocalIdNoteFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdNoteFuture.resultCount(), 1);
        EXPECT_FALSE(foundByLocalIdNoteFuture.result());

        auto foundByGuidNoteFuture =
            notesHandler->findNoteByGuid(note.guid().value(), fetchNoteOptions);

        foundByGuidNoteFuture.waitForFinished();
        ASSERT_EQ(foundByGuidNoteFuture.resultCount(), 1);
        EXPECT_FALSE(foundByGuidNoteFuture.result());

        auto listNotesFuture =
            notesHandler->listNotes(fetchNoteOptions, listNotesOptions);

        listNotesFuture.waitForFinished();
        ASSERT_EQ(listNotesFuture.resultCount(), 1);
        EXPECT_EQ(listNotesFuture.result().size(), 0);
    };

    checkNoteExpunged();

    putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putNotes().size(), 2);
    EXPECT_EQ(notifierListener.putNotes()[1], note);

    auto expungeNoteByGuidFuture =
        notesHandler->expungeNoteByGuid(note.guid().value());

    expungeNoteByGuidFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedNoteLocalIds().size(), 2);
    EXPECT_EQ(notifierListener.expungedNoteLocalIds().at(1), note.localId());

    checkNoteExpunged();

    putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putNotes().size(), 3);
    EXPECT_EQ(notifierListener.putNotes()[2], note);

    auto updatedNote = note;

    updatedNote.setTitle(
        updatedNote.title().value() + QStringLiteral("_updated"));

    using UpdateNoteOptions = NotesHandler::UpdateNoteOptions;
    using UpdateNoteOption = NotesHandler::UpdateNoteOption;

    const UpdateNoteOptions updateNoteOptions = [&] {
        UpdateNoteOptions options;
        if (!note.tagLocalIds().isEmpty()) {
            options.setFlag(UpdateNoteOption::UpdateTags);
        }
        if (note.resources() && !note.resources()->isEmpty()) {
            options.setFlag(UpdateNoteOption::UpdateResourceMetadata);
            options.setFlag(UpdateNoteOption::UpdateResourceBinaryData);
        }
        return options;
    }();

    auto updateNoteFuture =
        notesHandler->updateNote(updatedNote, updateNoteOptions);

    updateNoteFuture.waitForFinished();

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.updatedNotesWithOptions().size(), 1);
    EXPECT_EQ(notifierListener.updatedNotesWithOptions()[0].first, updatedNote);
    EXPECT_EQ(
        notifierListener.updatedNotesWithOptions()[0].second,
        updateNoteOptions);

    foundByLocalIdNoteFuture = notesHandler->findNoteByLocalId(
        updatedNote.localId(), fetchNoteOptions);

    foundByLocalIdNoteFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdNoteFuture.resultCount(), 1);
    ASSERT_TRUE(foundByLocalIdNoteFuture.result());
    EXPECT_EQ(foundByLocalIdNoteFuture.result(), updatedNote);

    foundByGuidNoteFuture = notesHandler->findNoteByGuid(
        updatedNote.guid().value(), fetchNoteOptions);

    foundByGuidNoteFuture.waitForFinished();
    ASSERT_EQ(foundByGuidNoteFuture.resultCount(), 1);
    ASSERT_TRUE(foundByGuidNoteFuture.result());
    EXPECT_EQ(foundByGuidNoteFuture.result(), updatedNote);
}

TEST_F(NotesHandlerTest, HandleMultipleNotes)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    NotesHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier, &Notifier::notePut, &notifierListener,
        &NotesHandlerTestNotifierListener::onNotePut);

    QObject::connect(
        m_notifier, &Notifier::noteUpdated, &notifierListener,
        &NotesHandlerTestNotifierListener::onNoteUpdated);

    QObject::connect(
        m_notifier, &Notifier::noteExpunged, &notifierListener,
        &NotesHandlerTestNotifierListener::onNoteExpunged);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNotebookFuture = notebooksHandler->putNotebook(*gNotebook);
    putNotebookFuture.waitForFinished();

    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    auto notes = gNoteTestValues;
    int noteCounter = 2;
    int tagCounter = 1;
    int sharedNoteCounter = 1;
    for (auto it = std::next(notes.begin()); it != notes.end(); ++it) // NOLINT
    {
        auto & note = *it;
        note.setLocalId(UidGenerator::Generate());
        note.setGuid(UidGenerator::Generate());

        note.setTitle(
            note.title().value() + QStringLiteral(" #") +
            QString::number(noteCounter));

        note.setUpdateSequenceNum(noteCounter);
        ++noteCounter;

        if (note.sharedNotes() && !note.sharedNotes()->isEmpty()) {
            for (auto & sharedNote: *note.mutableSharedNotes()) {
                sharedNote.setNoteGuid(note.guid().value());

                if (sharedNote.recipientIdentity()) {
                    sharedNote.mutableRecipientIdentity()->setId(
                        qevercloud::IdentityID{
                            sharedNoteCounter * 20}); // NOLINT
                }

                ++sharedNoteCounter;
            }
        }

        if (note.resources() && !note.resources()->isEmpty()) {
            for (auto & resource: *note.mutableResources()) {
                resource.setNoteLocalId(note.localId());
                resource.setNoteGuid(note.guid());
            }
        }

        if (!note.tagLocalIds().isEmpty() ||
            (note.tagGuids() && !note.tagGuids()->isEmpty()))
        {
            if (!note.tagLocalIds().isEmpty()) {
                int index = 0;
                for (const auto & tagLocalId: qAsConst(note.tagLocalIds())) {
                    qevercloud::Tag tag;
                    tag.setLocalId(tagLocalId);
                    if (note.tagGuids() && !note.tagGuids()->isEmpty()) {
                        tag.setGuid(note.tagGuids()->at(index));
                    }

                    tag.setName(
                        QStringLiteral("Tag #") + QString::number(tagCounter));

                    ++tagCounter;

                    auto putTagFuture = tagsHandler->putTag(tag);
                    putTagFuture.waitForFinished();

                    ++index;
                }
            }
            else if (note.tagGuids() && !note.tagGuids()->isEmpty()) {
                for (const auto & tagGuid: qAsConst(*note.tagGuids())) {
                    qevercloud::Tag tag;
                    tag.setGuid(tagGuid);

                    tag.setName(
                        QStringLiteral("Tag #") + QString::number(tagCounter));

                    ++tagCounter;

                    auto putTagFuture = tagsHandler->putTag(tag);
                    putTagFuture.waitForFinished();
                }
            }
        }
    }

    QFutureSynchronizer<void> putNotesSynchronizer;
    for (auto note: notes) {
        auto putNoteFuture = notesHandler->putNote(std::move(note));
        putNotesSynchronizer.addFuture(putNoteFuture);
    }

    EXPECT_NO_THROW(putNotesSynchronizer.waitForFinished());

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putNotes().size(), notes.size());

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    const auto noteCountOptions = NoteCountOptions{} |
        NoteCountOption::IncludeNonDeletedNotes |
        NoteCountOption::IncludeDeletedNotes;

    auto noteCountFuture = notesHandler->noteCount(noteCountOptions);
    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), notes.size());

    QStringList allTagLocalIds;
    int notesWithTagLocalIds = 0;
    for (const auto & note: qAsConst(notes)) {
        allTagLocalIds << note.tagLocalIds();
        if (!note.tagLocalIds().isEmpty()) {
            ++notesWithTagLocalIds;
        }
    }

    for (const auto & tagLocalId: qAsConst(allTagLocalIds)) {
        noteCountFuture =
            notesHandler->noteCountPerTagLocalId(tagLocalId, noteCountOptions);

        noteCountFuture.waitForFinished();
        EXPECT_EQ(noteCountFuture.result(), 1U);
    }

    noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        gNotebook->localId(), noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), notes.size());

    noteCountFuture = notesHandler->noteCountPerNotebookAndTagLocalIds(
        QStringList{} << gNotebook->localId(), allTagLocalIds,
        noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), notesWithTagLocalIds);

    using FetchNoteOption = ILocalStorage::FetchNoteOption;
    using FetchNoteOptions = ILocalStorage::FetchNoteOptions;

    const auto fetchNoteOptions = FetchNoteOptions{} |
        FetchNoteOption::WithResourceMetadata |
        FetchNoteOption::WithResourceBinaryData;

    for (auto note: qAsConst(notes)) {
        auto foundByLocalIdNoteFuture =
            notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

        foundByLocalIdNoteFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdNoteFuture.resultCount(), 1);
        ASSERT_TRUE(foundByLocalIdNoteFuture.result());

        if (note.tagLocalIds().isEmpty() && note.tagGuids() &&
            !note.tagGuids()->isEmpty())
        {
            EXPECT_FALSE(
                foundByLocalIdNoteFuture.result()->tagLocalIds().isEmpty());

            note.setTagLocalIds(
                foundByLocalIdNoteFuture.result()->tagLocalIds());
        }

        EXPECT_EQ(foundByLocalIdNoteFuture.result(), note);

        auto foundByGuidNoteFuture =
            notesHandler->findNoteByGuid(note.guid().value(), fetchNoteOptions);

        foundByGuidNoteFuture.waitForFinished();
        ASSERT_EQ(foundByGuidNoteFuture.resultCount(), 1);
        ASSERT_TRUE(foundByGuidNoteFuture.result());
        EXPECT_EQ(foundByGuidNoteFuture.result(), note);
    }

    for (const auto & note: qAsConst(notes)) {
        auto expungeNoteByLocalIdFuture =
            notesHandler->expungeNoteByLocalId(note.localId());
        expungeNoteByLocalIdFuture.waitForFinished();
    }

    QCoreApplication::processEvents();
    EXPECT_EQ(notifierListener.expungedNoteLocalIds().size(), notes.size());

    noteCountFuture = notesHandler->noteCount(noteCountOptions);
    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);

    for (const auto & tagLocalId: qAsConst(allTagLocalIds)) {
        noteCountFuture =
            notesHandler->noteCountPerTagLocalId(tagLocalId, noteCountOptions);

        noteCountFuture.waitForFinished();
        EXPECT_EQ(noteCountFuture.result(), 0U);
    }

    noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        gNotebook->localId(), noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);

    noteCountFuture = notesHandler->noteCountPerNotebookAndTagLocalIds(
        QStringList{} << gNotebook->localId(), allTagLocalIds,
        noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 0U);

    for (const auto & note: qAsConst(notes)) {
        auto foundByLocalIdNoteFuture =
            notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

        foundByLocalIdNoteFuture.waitForFinished();
        ASSERT_EQ(foundByLocalIdNoteFuture.resultCount(), 1);
        ASSERT_FALSE(foundByLocalIdNoteFuture.result());

        auto foundByGuidNoteFuture =
            notesHandler->findNoteByGuid(note.guid().value(), fetchNoteOptions);

        foundByGuidNoteFuture.waitForFinished();
        ASSERT_EQ(foundByGuidNoteFuture.resultCount(), 1);
        ASSERT_FALSE(foundByGuidNoteFuture.result());
    }
}

// The test checks that updates of existing note in the local storage work
// as expected when updated note doesn't have several fields which existed
// for the original note
TEST_F(NotesHandlerTest, RemoveNoteFieldsOnUpdate)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNotebookFuture = notebooksHandler->putNotebook(*gNotebook);
    putNotebookFuture.waitForFinished();

    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    qevercloud::Tag tag;
    tag.setGuid(UidGenerator::Generate());
    tag.setUpdateSequenceNum(1);
    tag.setName(QStringLiteral("Tag"));

    auto putTagFuture = tagsHandler->putTag(tag);
    putTagFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    // Put note with a tag and a resource to the local storage
    qevercloud::Note note;
    note.setGuid(UidGenerator::Generate());
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(gNotebook->guid());
    note.setNotebookLocalId(gNotebook->localId());

    qevercloud::Resource resource;
    resource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000044"));
    resource.setUpdateSequenceNum(1);
    resource.setNoteGuid(note.guid());
    resource.setNoteLocalId(note.localId());

    resource.setData(qevercloud::Data{});
    resource.mutableData()->setBody(QByteArray("Fake resource data body"));
    resource.mutableData()->setSize(resource.data()->body()->size());
    resource.mutableData()->setBodyHash(QCryptographicHash::hash(
        *resource.data()->body(), QCryptographicHash::Md5));

    note.setResources(QList<qevercloud::Resource>() << resource);
    note.setTagGuids(QList<qevercloud::Guid>() << *tag.guid());
    note.setNotebookLocalId(gNotebook->localId());

    auto putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    // Update this note and ensure it no longer has the resource and the tag
    // binding
    qevercloud::Note updatedNote;
    updatedNote.setLocalId(note.localId());
    updatedNote.setGuid(note.guid());
    updatedNote.setUpdateSequenceNum(1);
    updatedNote.setTitle(QStringLiteral("Note"));

    updatedNote.setContent(
        QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    updatedNote.setCreated(1);
    updatedNote.setUpdated(1);
    updatedNote.setActive(true);
    updatedNote.setNotebookGuid(gNotebook->guid());
    updatedNote.setNotebookLocalId(gNotebook->localId());

    putNoteFuture = notesHandler->putNote(updatedNote);
    putNoteFuture.waitForFinished();

    using UpdateNoteOption = NotesHandler::UpdateNoteOption;
    using UpdateNoteOptions = NotesHandler::UpdateNoteOptions;

    const auto updateNoteOptions = UpdateNoteOptions{} |
        UpdateNoteOption::UpdateTags |
        UpdateNoteOption::UpdateResourceMetadata |
        UpdateNoteOption::UpdateResourceBinaryData;

    auto updateNoteFuture =
        notesHandler->updateNote(updatedNote, updateNoteOptions);

    updateNoteFuture.waitForFinished();

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    const auto fetchNoteOptions = FetchNoteOptions{} |
        FetchNoteOption::WithResourceMetadata |
        FetchNoteOption::WithResourceBinaryData;

    auto foundNoteFuture =
        notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

    foundNoteFuture.waitForFinished();
    ASSERT_EQ(foundNoteFuture.resultCount(), 1);
    ASSERT_TRUE(foundNoteFuture.result());
    EXPECT_EQ(foundNoteFuture.result(), updatedNote);

    // Add resource attributes to the resource and add the resource to note
    // again
    resource.setAttributes(qevercloud::ResourceAttributes{});

    auto & resourceAttributes = *resource.mutableAttributes();
    resourceAttributes.setApplicationData(qevercloud::LazyMap{});

    auto & resourceAppData = *resourceAttributes.mutableApplicationData();
    resourceAppData.setKeysOnly(
        QSet<QString>() << QStringLiteral("key_1") << QStringLiteral("key_2")
                        << QStringLiteral("key_3"));

    resourceAppData.setFullMap(QMap<QString, QString>{});

    (*resourceAppData.mutableFullMap())[QStringLiteral("key_1")] =
        QStringLiteral("value_1");

    (*resourceAppData.mutableFullMap())[QStringLiteral("key_2")] =
        QStringLiteral("value_2");

    (*resourceAppData.mutableFullMap())[QStringLiteral("key_3")] =
        QStringLiteral("value_3");

    updatedNote.setResources(QList<qevercloud::Resource>() << resource);

    updateNoteFuture = notesHandler->updateNote(updatedNote, updateNoteOptions);
    updateNoteFuture.waitForFinished();

    foundNoteFuture =
        notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

    foundNoteFuture.waitForFinished();
    ASSERT_EQ(foundNoteFuture.resultCount(), 1);
    ASSERT_TRUE(foundNoteFuture.result());
    EXPECT_EQ(foundNoteFuture.result(), updatedNote);
    ASSERT_TRUE(foundNoteFuture.result()->resources());
    ASSERT_FALSE(foundNoteFuture.result()->resources()->isEmpty());
    EXPECT_TRUE(foundNoteFuture.result()->resources()->begin()->attributes());

    // Remove resource attributes from note's resource and update it again
    updatedNote.mutableResources()->begin()->setAttributes(std::nullopt);

    updateNoteFuture = notesHandler->updateNote(updatedNote, updateNoteOptions);
    updateNoteFuture.waitForFinished();

    foundNoteFuture =
        notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

    foundNoteFuture.waitForFinished();
    ASSERT_EQ(foundNoteFuture.resultCount(), 1);
    ASSERT_TRUE(foundNoteFuture.result());
    EXPECT_EQ(foundNoteFuture.result(), updatedNote);
    ASSERT_TRUE(foundNoteFuture.result()->resources());
    ASSERT_FALSE(foundNoteFuture.result()->resources()->isEmpty());
    EXPECT_FALSE(foundNoteFuture.result()->resources()->begin()->attributes());
}

enum class ExcludedTagIds
{
    LocalIds,
    Guids
};

const std::array gExcludedTagIds{
    ExcludedTagIds::LocalIds, ExcludedTagIds::Guids};

class NotesHandlerUpdateNoteTagIdsTest :
    public NotesHandlerTest,
    public testing::WithParamInterface<ExcludedTagIds>
{};

INSTANTIATE_TEST_SUITE_P(
    NotesHandlerUpdateNoteTagIdsTestInstance, NotesHandlerUpdateNoteTagIdsTest,
    testing::ValuesIn(gExcludedTagIds));

TEST_P(NotesHandlerUpdateNoteTagIdsTest, UpdateNoteWithTagPartialTagIds)
{
    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNotebookFuture = notebooksHandler->putNotebook(*gNotebook);
    putNotebookFuture.waitForFinished();

    const auto tagsHandler = std::make_shared<TagsHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread);

    qevercloud::Tag tag1;
    tag1.setGuid(UidGenerator::Generate());
    tag1.setUpdateSequenceNum(1);
    tag1.setName(QStringLiteral("Tag #1"));

    auto putTagFuture = tagsHandler->putTag(tag1);
    putTagFuture.waitForFinished();

    qevercloud::Tag tag2;
    tag2.setGuid(UidGenerator::Generate());
    tag2.setUpdateSequenceNum(2);
    tag2.setName(QStringLiteral("Tag #2"));

    putTagFuture = tagsHandler->putTag(tag2);
    putTagFuture.waitForFinished();

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    qevercloud::Note note;
    note.setGuid(UidGenerator::Generate());
    note.setUpdateSequenceNum(1);
    note.setTitle(QStringLiteral("Note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
    note.setCreated(1);
    note.setUpdated(1);
    note.setActive(true);
    note.setNotebookGuid(gNotebook->guid());
    note.setNotebookLocalId(gNotebook->localId());
    note.setTagGuids(
        QList<qevercloud::Guid>{} << tag1.guid().value()
                                  << tag2.guid().value());
    note.setTagLocalIds(QStringList{} << tag1.localId() << tag2.localId());

    auto putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    qevercloud::Note updatedNote = note;

    const auto excludedTagIds = GetParam();
    if (excludedTagIds == ExcludedTagIds::LocalIds) {
        updatedNote.setTagLocalIds(QStringList{});
        updatedNote.setTagGuids(
            QList<qevercloud::Guid>{} << tag1.guid().value());
    }
    else {
        updatedNote.setTagGuids(std::nullopt);
        updatedNote.setTagLocalIds(QStringList{} << tag1.localId());
    }

    using UpdateNoteOption = NotesHandler::UpdateNoteOption;
    using UpdateNoteOptions = NotesHandler::UpdateNoteOptions;

    const auto updateNoteOptions = UpdateNoteOptions{} |
        UpdateNoteOption::UpdateTags |
        UpdateNoteOption::UpdateResourceMetadata |
        UpdateNoteOption::UpdateResourceBinaryData;

    auto updateNoteFuture =
        notesHandler->updateNote(updatedNote, updateNoteOptions);

    updateNoteFuture.waitForFinished();

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    const auto fetchNoteOptions = FetchNoteOptions{} |
        FetchNoteOption::WithResourceMetadata |
        FetchNoteOption::WithResourceBinaryData;

    auto foundNoteFuture =
        notesHandler->findNoteByLocalId(note.localId(), fetchNoteOptions);

    foundNoteFuture.waitForFinished();
    ASSERT_EQ(foundNoteFuture.resultCount(), 1);
    ASSERT_TRUE(foundNoteFuture.result());

    if (excludedTagIds == ExcludedTagIds::LocalIds) {
        updatedNote.setTagLocalIds(QStringList{} << tag1.localId());
    }
    else {
        updatedNote.setTagGuids(
            QList<qevercloud::Guid>{} << tag1.guid().value());
    }
    EXPECT_EQ(foundNoteFuture.result(), updatedNote);
}

struct NoteSearchQueryTestData
{
    QString queryString;
    QSet<int> expectedContainedNotesIndices;
};

class NotesHandlerNoteSearchQueryTest :
    public NotesHandlerTest,
    public testing::WithParamInterface<NoteSearchQueryTestData>
{
    void SetUp() override
    {
        NotesHandlerTest::SetUp();

        createNotebooks();
        createTags();
        createResources();
        createNotes();

        putNotebooks();
        putTags();
        putNotes();
    }

    void createNotebooks()
    {
        constexpr int notebookCount = 3;
        m_notebooks.reserve(notebookCount);
        for (int i = 0; i < notebookCount; ++i) {
            qevercloud::Notebook notebook;
            notebook.setName(
                QString(QStringLiteral("Test notebook #")) +
                QString::number(i));

            notebook.setUpdateSequenceNum(i);
            notebook.setDefaultNotebook(i == 0);
            notebook.setServiceCreated(i);
            notebook.setServiceUpdated(i + 1);

            m_notebooks << notebook;
        }
    }

    void createTags()
    {
        constexpr int tagCount = 9;
        m_tags.reserve(tagCount);
        for (int i = 0; i < tagCount; ++i) {
            m_tags << qevercloud::Tag();
            auto & tag = m_tags.back();
            tag.setUpdateSequenceNum(i);
        }

        m_tags[0].setName(QStringLiteral("College"));
        m_tags[1].setName(QStringLiteral("Server"));
        m_tags[2].setName(QStringLiteral("Binary"));
        m_tags[3].setName(QStringLiteral("Download"));
        m_tags[4].setName(QStringLiteral("Browser"));
        m_tags[5].setName(QStringLiteral("Tracker"));
        m_tags[6].setName(QStringLiteral("Application"));
        m_tags[7].setName(QString::fromUtf8("Footlocker αυΤΟκίΝΗτο"));
        m_tags[8].setName(QStringLiteral("Money"));

        m_tags[0].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813a"));
        m_tags[1].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813b"));
        m_tags[2].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813c"));
        m_tags[3].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813d"));
        m_tags[4].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813e"));
        m_tags[5].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813f"));
        m_tags[6].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813g"));
        m_tags[7].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813h"));
        m_tags[8].setGuid(
            QStringLiteral("8743428c-ef91-4d05-9e7c-4a2e856e813i"));
    }

    void createResources()
    {
        constexpr int resourceCount = 3;
        m_resources.reserve(resourceCount);

        for (int i = 0; i < resourceCount; ++i) {
            m_resources << qevercloud::Resource();
            auto & resource = m_resources.back();

            resource.setUpdateSequenceNum(i);
        }

        auto & res0 = m_resources[0];
        res0.setMime(QStringLiteral("image/gif"));
        res0.setData(qevercloud::Data{});
        res0.mutableData()->setBody(QByteArray("fake image/gif byte array"));
        res0.mutableData()->setSize(res0.data()->body()->size());

        res0.mutableData()->setBodyHash(QCryptographicHash::hash(
            *res0.data()->body(), QCryptographicHash::Md5));

        QString recognitionBodyStr = QStringLiteral(
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
            "</recoIndex>");

        res0.setRecognition(qevercloud::Data{});
        res0.mutableRecognition()->setBody(recognitionBodyStr.toUtf8());
        res0.mutableRecognition()->setSize(res0.recognition()->body()->size());

        res0.mutableRecognition()->setBodyHash(QCryptographicHash::hash(
            *res0.recognition()->body(), QCryptographicHash::Md5));

        auto & res1 = m_resources[1];
        res1.setMime(QStringLiteral("audio/*"));
        res1.setData(qevercloud::Data{});
        res1.mutableData()->setBody(QByteArray("fake audio/* byte array"));
        res1.mutableData()->setSize(res1.data()->body()->size());

        res1.mutableData()->setBodyHash(QCryptographicHash::hash(
            *res1.data()->body(), QCryptographicHash::Md5));

        res1.setRecognition(qevercloud::Data{});
        res1.mutableRecognition()->setBody(
            QByteArray("<recoIndex docType=\"picture\" objType=\"image\" "
                       "objID=\"fc83e58282d8059be17debabb69be900\" "
                       "engineVersion=\"5.5.22.7\" recoType=\"service\" "
                       "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
                       "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
                       "<t w=\"87\">WIKI ?</t>"
                       "<t w=\"83\">WIKIPEDIA</t>"
                       "<t w=\"82\">WIKJPEDJA</t>"
                       "<t w=\"71\">WJKJPEDJA</t>"
                       "<t w=\"67\">MJKJPEDJA</t>"
                       "<t w=\"67\">MJKJREDJA</t>"
                       "<t w=\"66\">MJKJREDJA</t>"
                       "</item>"
                       "<item x=\"1840\" y=\"1475\" w=\"14\" h=\"12\">"
                       "<t w=\"11\">et</t>"
                       "<t w=\"10\">TQ</t>"
                       "</item>"
                       "</recoIndex>"));

        res1.mutableRecognition()->setSize(res1.recognition()->body()->size());

        res1.mutableRecognition()->setBodyHash(QCryptographicHash::hash(
            *res1.recognition()->body(), QCryptographicHash::Md5));

        auto & res2 = m_resources[2];
        res2.setMime(QStringLiteral("application/vnd.evernote.ink"));

        res2.setData(qevercloud::Data{});
        res2.mutableData()->setBody(
            QByteArray("fake application/vnd.evernote.ink byte array"));

        res2.mutableData()->setSize(res2.data()->body()->size());

        res2.mutableData()->setBodyHash(QCryptographicHash::hash(
            *res2.data()->body(), QCryptographicHash::Md5));
    }

    void createNotes()
    {
        constexpr int titleCount = 3;
        QStringList titles;
        titles.reserve(titleCount);
        titles << QString::fromUtf8("Potato (είΝΑΙ)") << QStringLiteral("Ham")
               << QStringLiteral("Eggs");

        constexpr int contentCount = 9;
        QStringList contents;
        contents.reserve(contentCount);
        contents
            << QStringLiteral(
                   "<en-note><h1>The unique identifier of this note. "
                   "Will be set by the server</h1></en-note>")
            << QStringLiteral(
                   "<en-note><h1>The XHTML block that makes up the note. "
                   "This is the canonical form of the note's contents"
                   "</h1><en-todo checked = \"true\"/></en-note>")
            << QStringLiteral(
                   "<en-note><h1>The binary MD5 checksum of the UTF-8 "
                   "encoded content body.</h1></en-note>")
            << QString::fromUtf8(
                   "<en-note><h1>The number of Unicode characters "
                   "\"αυτό είναι ένα αυτοκίνητο\" in the content "
                   "of the note.</h1><en-todo/></en-note>")
            << QStringLiteral(
                   "<en-note><en-todo checked = \"true\"/><h1>The date "
                   "and time when the note was created in one of "
                   "the clients.</h1><en-todo checked = \"false\"/></en-note>")
            << QStringLiteral(
                   "<en-note><h1>If present [code characters], the note "
                   "is considered \"deleted\", and this stores the date "
                   "and time when the note was deleted</h1></en-note>")
            << QString::fromUtf8(
                   "<en-note><h1>If the note is available {ΑΥΤΌ "
                   "ΕΊΝΑΙ ΈΝΑ ΑΥΤΟΚΊΝΗΤΟ} for normal actions and viewing, "
                   "this flag will be set to true.</h1><en-crypt "
                   "hint=\"My Cat\'s Name\">NKLHX5yK1MlpzemJQijA"
                   "N6C4545s2EODxQ8Bg1r==</en-crypt></en-note>")
            << QString::fromUtf8(
                   "<en-note><h1>A number identifying the last "
                   "transaction (Αυτό ΕΊΝΑΙ ένα αυΤΟκίΝΗτο) to "
                   "modify the state of this note</h1></en-note>")
            << QStringLiteral(
                   "<en-note><h1>The list of resources that are embedded "
                   "within this note.</h1><en-todo checked = \"true\"/>"
                   "<en-crypt hint=\"My Cat\'s Name\">NKLHX5yK1Mlpzem"
                   "JQijAN6C4545s2EODxQ8Bg1r==</en-crypt></en-note>");

        QHash<QString, qevercloud::Timestamp> timestampForDateTimeString;

        QDateTime datetime = QDateTime::currentDateTime();
        datetime.setTime(QTime(0, 0, 0, 0)); // today midnight

        timestampForDateTimeString[QStringLiteral("day")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(-1);

        timestampForDateTimeString[QStringLiteral("day-1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(-1);

        timestampForDateTimeString[QStringLiteral("day-2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(-1);

        timestampForDateTimeString[QStringLiteral("day-3")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(4);

        timestampForDateTimeString[QStringLiteral("day+1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(1);

        timestampForDateTimeString[QStringLiteral("day+2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(1);

        timestampForDateTimeString[QStringLiteral("day+3")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(-3); // return back to today midnight

        const int dayOfWeek = datetime.date().dayOfWeek();

        // go to the closest previous Sunday
        datetime = datetime.addDays(-1L * dayOfWeek);

        timestampForDateTimeString[QStringLiteral("week")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(-7);

        timestampForDateTimeString[QStringLiteral("week-1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(-7);

        timestampForDateTimeString[QStringLiteral("week-2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(-7);

        timestampForDateTimeString[QStringLiteral("week-3")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(28);

        timestampForDateTimeString[QStringLiteral("week+1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(7);

        timestampForDateTimeString[QStringLiteral("week+2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addDays(7);

        timestampForDateTimeString[QStringLiteral("week+3")] =
            datetime.toMSecsSinceEpoch();

        // return to today midnight
        datetime = datetime.addDays(-21 + dayOfWeek);

        const int dayOfMonth = datetime.date().day();
        datetime = datetime.addDays(-(dayOfMonth - 1));

        timestampForDateTimeString[QStringLiteral("month")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addMonths(-1);

        timestampForDateTimeString[QStringLiteral("month-1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addMonths(-1);

        timestampForDateTimeString[QStringLiteral("month-2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addMonths(-1);

        timestampForDateTimeString[QStringLiteral("month-3")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addMonths(4);

        timestampForDateTimeString[QStringLiteral("month+1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addMonths(1);

        timestampForDateTimeString[QStringLiteral("month+2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addMonths(1);

        timestampForDateTimeString[QStringLiteral("month+3")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addMonths(-3);
        datetime =
            datetime.addDays(dayOfMonth - 1); // return back to today midnight

        const int monthOfYear = datetime.date().month();
        datetime = datetime.addMonths(-(monthOfYear - 1));
        datetime = datetime.addDays(-(dayOfMonth - 1));

        timestampForDateTimeString[QStringLiteral("year")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addYears(-1);

        timestampForDateTimeString[QStringLiteral("year-1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addYears(-1);

        timestampForDateTimeString[QStringLiteral("year-2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addYears(-1);

        timestampForDateTimeString[QStringLiteral("year-3")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addYears(4);

        timestampForDateTimeString[QStringLiteral("year+1")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addYears(1);

        timestampForDateTimeString[QStringLiteral("year+2")] =
            datetime.toMSecsSinceEpoch();

        datetime = datetime.addYears(1);

        timestampForDateTimeString[QStringLiteral("year+3")] =
            datetime.toMSecsSinceEpoch();

        const int creationTimestampCount = 9;
        QList<qevercloud::Timestamp> creationTimestamps;
        creationTimestamps.reserve(creationTimestampCount);

        creationTimestamps
            << timestampForDateTimeString[QStringLiteral("day-3")]
            << timestampForDateTimeString[QStringLiteral("day-2")]
            << timestampForDateTimeString[QStringLiteral("day-1")]
            << timestampForDateTimeString[QStringLiteral("day")]
            << timestampForDateTimeString[QStringLiteral("day+1")]
            << timestampForDateTimeString[QStringLiteral("day+2")]
            << timestampForDateTimeString[QStringLiteral("day+3")]
            << timestampForDateTimeString[QStringLiteral("week-3")]
            << timestampForDateTimeString[QStringLiteral("week-2")];

        const int modificationTimestampCount = 9;
        QList<qevercloud::Timestamp> modificationTimestamps;
        modificationTimestamps.reserve(modificationTimestampCount);

        modificationTimestamps
            << timestampForDateTimeString[QStringLiteral("month-3")]
            << timestampForDateTimeString[QStringLiteral("month-2")]
            << timestampForDateTimeString[QStringLiteral("month-1")]
            << timestampForDateTimeString[QStringLiteral("month")]
            << timestampForDateTimeString[QStringLiteral("month+1")]
            << timestampForDateTimeString[QStringLiteral("month+2")]
            << timestampForDateTimeString[QStringLiteral("month+3")]
            << timestampForDateTimeString[QStringLiteral("week-1")]
            << timestampForDateTimeString[QStringLiteral("week")];

        const int subjectDateTimestampCount = 3;
        QList<qevercloud::Timestamp> subjectDateTimestamps;
        subjectDateTimestamps.reserve(subjectDateTimestampCount);

        subjectDateTimestamps
            << timestampForDateTimeString[QStringLiteral("week+1")]
            << timestampForDateTimeString[QStringLiteral("week+2")]
            << timestampForDateTimeString[QStringLiteral("week+3")];

        const int latitudeCount = 9;
        QList<double> latitudes;
        latitudes.reserve(latitudeCount);

        latitudes << -72.5 << -51.3 << -32.1 << -11.03 << 10.24 << 32.33
                  << 54.78 << 72.34 << 91.18;

        const int longitudeCount = 9;
        QList<double> longitudes;
        longitudes.reserve(longitudeCount);

        longitudes << -71.15 << -52.42 << -31.91 << -12.25 << 9.78 << 34.62
                   << 56.17 << 73.27 << 92.46;

        const int altitudeCount = 9;
        QList<double> altitudes;
        altitudes.reserve(altitudeCount);

        altitudes << -70.23 << -51.81 << -32.62 << -11.14 << 10.45 << 31.73
                  << 52.73 << 71.82 << 91.92;

        const int authorCount = 3;
        QStringList authors;
        authors.reserve(authorCount);

        authors << QStringLiteral("Shakespeare") << QStringLiteral("Homer")
                << QStringLiteral("Socrates");

        const int sourceCount = 3;
        QStringList sources;
        sources.reserve(sourceCount);

        sources << QStringLiteral("web.clip") << QStringLiteral("mail.clip")
                << QStringLiteral("mobile.android");

        const int sourceApplicationCount = 3;
        QStringList sourceApplications;
        sourceApplications.reserve(sourceApplicationCount);

        sourceApplications << QStringLiteral("food.*")
                           << QStringLiteral("hello.*")
                           << QStringLiteral("skitch.*");

        const int contentClassCount = 3;
        QStringList contentClasses;
        contentClasses.reserve(contentClassCount);

        contentClasses << QStringLiteral("evernote.food.meal")
                       << QStringLiteral("evernote.hello.*")
                       << QStringLiteral("whatever");

        const int placeNameCount = 3;
        QStringList placeNames;
        placeNames.reserve(placeNameCount);

        placeNames << QStringLiteral("home") << QStringLiteral("school")
                   << QStringLiteral("bus");

        const int applicationDataCount = 3;
        QStringList applicationData;
        applicationData.reserve(applicationDataCount);

        applicationData << QStringLiteral("myapp") << QStringLiteral("Evernote")
                        << QStringLiteral("Quentier");

        const int reminderOrderCount = 3;
        QList<qint64> reminderOrders;
        reminderOrders.reserve(reminderOrderCount);
        reminderOrders << 1 << 2 << 3;

        const int reminderTimeCount = 3;
        QList<qevercloud::Timestamp> reminderTimes;
        reminderTimes.reserve(reminderTimeCount);

        reminderTimes << timestampForDateTimeString[QStringLiteral("year-3")]
                      << timestampForDateTimeString[QStringLiteral("year-2")]
                      << timestampForDateTimeString[QStringLiteral("year-1")];

        const int reminderDoneTimeCount = 3;
        QList<qevercloud::Timestamp> reminderDoneTimes;
        reminderDoneTimes.reserve(reminderDoneTimeCount);

        reminderDoneTimes
            << timestampForDateTimeString[QStringLiteral("year")]
            << timestampForDateTimeString[QStringLiteral("year+1")]
            << timestampForDateTimeString[QStringLiteral("year+2")];

        const int noteCount = 9;
        m_notes.reserve(noteCount);

        Q_ASSERT(!m_notebooks.isEmpty());
        const int notebookCount = m_notebooks.size();

        for (int i = 0; i < noteCount; ++i) {
            m_notes << qevercloud::Note();
            auto & note = m_notes.back();

            note.setTitle(
                titles[i / titleCount] + QStringLiteral(" #") +
                QString::number(i));

            note.setContent(contents[i]);

            if (i != 7) {
                note.setCreated(creationTimestamps[i]);
            }

            if (!note.attributes()) {
                note.setAttributes(qevercloud::NoteAttributes{});
            }

            auto & attributes = *note.mutableAttributes();

            attributes.setSubjectDate(
                subjectDateTimestamps[i / subjectDateTimestampCount]);

            if ((i != 6) && (i != 7) && (i != 8)) {
                attributes.setLatitude(latitudes[i]);
            }

            attributes.setLongitude(longitudes[i]);
            attributes.setAltitude(altitudes[i]);
            attributes.setAuthor(authors[i / authorCount]);
            attributes.setSource(sources[i / sourceCount]);

            attributes.setSourceApplication(
                sourceApplications[i / sourceApplicationCount]);

            attributes.setContentClass(contentClasses[i / contentClassCount]);

            if (i / placeNameCount != 2) {
                attributes.setPlaceName(placeNames[i / placeNameCount]);
            }

            if ((i != 3) && (i != 4) && (i != 5)) {
                attributes.setApplicationData(qevercloud::LazyMap{});
                attributes.mutableApplicationData()->setKeysOnly(
                    QSet<QString>{});

                auto & keysOnly =
                    *attributes.mutableApplicationData()->mutableKeysOnly();

                keysOnly.insert(applicationData[i / applicationDataCount]);

                attributes.mutableApplicationData()->setFullMap(
                    QMap<QString, QString>{});

                auto & fullMap =
                    *attributes.mutableApplicationData()->mutableFullMap();

                fullMap.insert(
                    applicationData[i / applicationDataCount],
                    QStringLiteral("Application data value at key ") +
                        applicationData[i / applicationDataCount]);
            }

            if (i == 6) {
                if (!attributes.applicationData()->keysOnly()) {
                    attributes.mutableApplicationData()->setKeysOnly(
                        QSet<QString>{});
                }

                auto & keysOnly =
                    *attributes.mutableApplicationData()->mutableKeysOnly();

                keysOnly.insert(applicationData[1]);

                if (!attributes.applicationData()->fullMap()) {
                    attributes.mutableApplicationData()->setFullMap(
                        QMap<QString, QString>{});
                }

                auto & fullMap =
                    *attributes.mutableApplicationData()->mutableFullMap();

                fullMap.insert(
                    applicationData[1],
                    QStringLiteral("Application data value at key ") +
                        applicationData[1]);
            }

            if ((i != 0) && (i != 1) && (i != 2)) {
                attributes.setReminderOrder(
                    reminderOrders[i / reminderOrderCount]);
            }

            attributes.setReminderTime(reminderTimes[i / reminderTimeCount]);
            attributes.setReminderDoneTime(
                reminderDoneTimes[i / reminderDoneTimeCount]);

            if (i != (noteCount - 1)) {
                int k = 0;
                const int tagCount = m_tags.size();
                while (((i + k) < tagCount) && (k < 3)) {
                    if (!note.tagGuids()) {
                        note.setTagGuids(
                            QList<qevercloud::Guid>()
                            << m_tags[i + k].guid().value());
                    }
                    else {
                        note.mutableTagGuids()->append(
                            m_tags[i + k].guid().value());
                    }

                    note.mutableTagLocalIds().append(m_tags[i + k].localId());
                    ++k;
                }
            }

            if (i != 8) {
                Q_ASSERT(!m_resources.isEmpty());
                auto resource = m_resources[i / m_resources.size()];
                resource.setLocalId(QUuid::createUuid().toString());
                resource.setNoteLocalId(note.localId());
                resource.setNoteGuid(note.guid());
                if (!note.resources()) {
                    note.setResources(
                        QList<qevercloud::Resource>() << resource);
                }
                else {
                    note.mutableResources()->append(resource);
                }
            }

            if (i == 3) {
                auto additionalResource = m_resources[0];
                additionalResource.setLocalId(QUuid::createUuid().toString());
                additionalResource.setNoteLocalId(note.localId());
                additionalResource.setNoteGuid(note.guid());
                if (!note.resources()) {
                    note.setResources(
                        QList<qevercloud::Resource>() << additionalResource);
                }
                else {
                    note.mutableResources()->append(additionalResource);
                }
            }

            m_notes[i].setNotebookLocalId(
                m_notebooks[i / notebookCount].localId());
        }
    }

    void putNotebooks()
    {
        Q_ASSERT(!m_notebooks.isEmpty());

        const auto notebooksHandler = std::make_shared<NotebooksHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

        for (const auto & notebook: qAsConst(m_notebooks)) {
            auto putNotebookFuture = notebooksHandler->putNotebook(notebook);
            putNotebookFuture.waitForFinished();
        }
    }

    void putTags()
    {
        Q_ASSERT(!m_tags.isEmpty());

        const auto tagsHandler = std::make_shared<TagsHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread);

        for (const auto & tag: qAsConst(m_tags)) {
            auto putTagFuture = tagsHandler->putTag(tag);
            putTagFuture.waitForFinished();
        }
    }

    void putNotes()
    {
        Q_ASSERT(!m_notes.isEmpty());

        const auto notesHandler = std::make_shared<NotesHandler>(
            m_connectionPool, QThreadPool::globalInstance(), m_notifier,
            m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

        for (const auto & note: qAsConst(m_notes)) {
            auto putNoteFuture = notesHandler->putNote(note);
            putNoteFuture.waitForFinished();
        }
    }

protected:
    QList<qevercloud::Notebook> m_notebooks;
    QList<qevercloud::Tag> m_tags;
    QList<qevercloud::Resource> m_resources;
    QList<qevercloud::Note> m_notes;
};

const std::array gNoteSearchQueryTestData{
    NoteSearchQueryTestData{
        QStringLiteral("todo:true"), QSet<int>{} << 1 << 4 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("todo:false"), QSet<int>{} << 3 << 4},
    NoteSearchQueryTestData{
        QStringLiteral("todo:*"), QSet<int>{} << 1 << 3 << 4 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("todo:true -todo:false"), QSet<int>{} << 1 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("-todo:false todo:true"), QSet<int>{} << 1 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("todo:false -todo:true"), QSet<int>{} << 3},
    NoteSearchQueryTestData{
        QStringLiteral("-todo:true todo:false"), QSet<int>{} << 3},
    NoteSearchQueryTestData{
        QStringLiteral("todo:true -todo:false todo:*"),
        QSet<int>{} << 1 << 3 << 4 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: todo:true todo:false"),
        QSet<int>{} << 1 << 3 << 4 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("todo:true todo:false"), QSet<int>{} << 4},
    NoteSearchQueryTestData{
        QStringLiteral("-todo:*"), QSet<int>{} << 0 << 2 << 5 << 6 << 7},
    NoteSearchQueryTestData{
        QStringLiteral("encryption:"), QSet<int>{} << 6 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("-encryption:"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 7},
    NoteSearchQueryTestData{
        QStringLiteral("reminderOrder:*"),
        QSet<int>{} << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("-reminderOrder:*"), QSet<int>{} << 0 << 1 << 2},
    NoteSearchQueryTestData{
        QStringLiteral("notebook:\"Test notebook #1\""),
        QSet<int>{} << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("tag:\"Server\""), QSet<int>{} << 0 << 1},
    NoteSearchQueryTestData{
        QStringLiteral("-tag:\"Binary\""),
        QSet<int>{} << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("tag:\"Server\" tag:\"Download\""), QSet<int>{} << 1},
    NoteSearchQueryTestData{
        QStringLiteral("any: tag:\"Server\" tag:\"Download\""),
        QSet<int>{} << 0 << 1 << 2 << 3},
    NoteSearchQueryTestData{
        QStringLiteral("tag:\"Browser\" -tag:\"Binary\""),
        QSet<int>{} << 3 << 4},
    NoteSearchQueryTestData{
        QStringLiteral("any: tag:\"Browser\" -tag:\"Binary\""),
        QSet<int>{} << 2 << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("tag:*"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7},
    NoteSearchQueryTestData{QStringLiteral("-tag:*"), QSet<int>{} << 8},
    NoteSearchQueryTestData{
        QStringLiteral("resource:\"audio/*\""), QSet<int>{} << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("-resource:\"application/vnd.evernote.ink\""),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("resource:\"image/gif\" resource:\"audio/*\""),
        QSet<int>{} << 3},
    NoteSearchQueryTestData{
        QStringLiteral("any: resource:\"image/gif\" resource:\"audio/*\""),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("resource:\"image/gif\" -resource:\"audio/*\""),
        QSet<int>{} << 0 << 1 << 2},
    NoteSearchQueryTestData{
        QStringLiteral("any: resource:\"image/gif\" -resource:\"audio/*\""),
        QSet<int>{} << 0 << 1 << 2 << 3 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("resource:*"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7},
    NoteSearchQueryTestData{QStringLiteral("-resource:*"), QSet<int>{} << 8},
    NoteSearchQueryTestData{
        QStringLiteral("created:day"), QSet<int>{} << 3 << 4 << 5 << 6},
    NoteSearchQueryTestData{
        QStringLiteral("-created:day+1"), QSet<int>{} << 0 << 1 << 2 << 3 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("created:day created:day+2"), QSet<int>{} << 5 << 6},
    NoteSearchQueryTestData{
        QStringLiteral("-created:day+2 -created:day-1"),
        QSet<int>{} << 0 << 1 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: created:day-1 created:day+2"),
        QSet<int>{} << 2 << 3 << 4 << 5 << 6},
    NoteSearchQueryTestData{
        QStringLiteral("any: -created:day+2 -created:day-1"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("created:day-1 -created:day+2"),
        QSet<int>{} << 2 << 3 << 4},
    NoteSearchQueryTestData{
        QStringLiteral("any: created:day+2 -created:day-1"),
        QSet<int>{} << 0 << 1 << 5 << 6 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("created:*"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 8},
    NoteSearchQueryTestData{QStringLiteral("-created:*"), QSet<int>{} << 7},
    NoteSearchQueryTestData{
        QStringLiteral("latitude:10"), QSet<int>{} << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("-latitude:-30"), QSet<int>{} << 0 << 1 << 2},
    NoteSearchQueryTestData{
        QStringLiteral("latitude:-10 latitude:10"), QSet<int>{} << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("any: latitude:-10 latitude:10"), QSet<int>{} << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("-latitude:-30 -latitude:-10"),
        QSet<int>{} << 0 << 1 << 2},
    NoteSearchQueryTestData{
        QStringLiteral("any: -latitude:-30 -latitude:-10"),
        QSet<int>{} << 0 << 1 << 2 << 3},
    NoteSearchQueryTestData{
        QStringLiteral("latitude:-20 -latitude:20"), QSet<int>{} << 3 << 4},
    NoteSearchQueryTestData{
        QStringLiteral("any: -latitude:-30 latitude:30"),
        QSet<int>{} << 0 << 1 << 2 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("latitude:*"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("-latitude:*"), QSet<int>{} << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("placeName:school"), QSet<int>{} << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("-placeName:home"),
        QSet<int>{} << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("placeName:home placeName:school"), QSet<int>{}},
    NoteSearchQueryTestData{
        QStringLiteral("any: placeName:home placeName:school"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("placeName:home -placeName:school"),
        QSet<int>{} << 0 << 1 << 2},
    NoteSearchQueryTestData{
        QStringLiteral("any: placeName:home -placeName:school"),
        QSet<int>{} << 0 << 1 << 2 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("-placeName:home -placeName:school"),
        QSet<int>{} << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: -placeName:home -placeName:school"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("placeName:*"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("-placeName:*"), QSet<int>{} << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("applicationData:myapp"), QSet<int>{} << 0 << 1 << 2},
    NoteSearchQueryTestData{
        QStringLiteral("-applicationData:Evernote"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("applicationData:Evernote applicationData:Quentier"),
        QSet<int>{} << 6},
    NoteSearchQueryTestData{
        QStringLiteral("any: applicationData:myapp applicationData:Evernote"),
        QSet<int>{} << 0 << 1 << 2 << 6},
    NoteSearchQueryTestData{
        QStringLiteral("-applicationData:myapp -applicationData:Evernote"),
        QSet<int>{} << 3 << 4 << 5 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: -applicationData:myapp -applicationData:Evernote"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("applicationData:Quentier -applicationData:Evernote"),
        QSet<int>{} << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: applicationData:Quentier -applicationData:myapp"),
        QSet<int>{} << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("applicationData:*"),
        QSet<int>{} << 0 << 1 << 2 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("-applicationData:*"), QSet<int>{} << 3 << 4 << 5},
    // Single simple search term
    NoteSearchQueryTestData{QStringLiteral("cAnOniCal"), QSet<int>{} << 1},
    NoteSearchQueryTestData{
        QStringLiteral("-canOnIcal"),
        QSet<int>{} << 0 << 2 << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: cAnOnical cHeckSuM ConsiDerEd"),
        QSet<int>{} << 1 << 2 << 5},
    // Multiple simple search terms
    NoteSearchQueryTestData{
        QStringLiteral("cAnOnical cHeckSuM ConsiDerEd"), QSet<int>{}},
    NoteSearchQueryTestData{
        QStringLiteral("-cAnOnical -cHeckSuM -ConsiDerEd"),
        QSet<int>{} << 0 << 3 << 4 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: -cAnOnical -cHeckSuM -ConsiDerEd"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5 << 6 << 7 << 8},
    // With search terms "overlapping" with some notes
    NoteSearchQueryTestData{
        QStringLiteral("-iDEnTIfIEr xhTmL -cHeckSuM -ConsiDerEd"),
        QSet<int>{} << 1},
    // Search by tag names as well as note contents
    NoteSearchQueryTestData{
        QStringLiteral("any: CaNonIcAl colLeGE UniCODe foOtLOCkeR"),
        QSet<int>{} << 0 << 1 << 3 << 5 << 6 << 7},
    NoteSearchQueryTestData{
        QStringLiteral("CaNonIcAl sERveR"), QSet<int>{} << 1},
    NoteSearchQueryTestData{QStringLiteral("wiLl -colLeGe"), QSet<int>{} << 6},
    // Search by note titles as well as note contents
    NoteSearchQueryTestData{
        QStringLiteral("any: CaNonIcAl eGGs"), QSet<int>{} << 1 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("CaNonIcAl PotAto"), QSet<int>{} << 1},
    NoteSearchQueryTestData{QStringLiteral("unIQue -EGgs"), QSet<int>{} << 0},
    NoteSearchQueryTestData{
        QStringLiteral("any: cONSiDERed -hAm"),
        QSet<int>{} << 0 << 1 << 2 << 5 << 6 << 7 << 8},
    // Search by note titles and tag names as well as note contents
    NoteSearchQueryTestData{
        QStringLiteral("any: cHECksUM SeRVEr hAM"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5},
    // Search by resource recognition data
    NoteSearchQueryTestData{
        QStringLiteral("inFoRmATiON"), QSet<int>{} << 0 << 1 << 2 << 3},
    NoteSearchQueryTestData{
        QStringLiteral("-infoRMatiON"), QSet<int>{} << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("infoRMAtion wikiPEDiA any:"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 4 << 5},
    NoteSearchQueryTestData{
        QStringLiteral("-inFORMation -wikiPEDiA"), QSet<int>{} << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("inFOrMATioN tHe poTaTO serVEr"), QSet<int>{} << 0 << 1},
    NoteSearchQueryTestData{
        QStringLiteral("wiKiPeDiA servER haM iDEntiFYiNg any:"),
        QSet<int>{} << 0 << 1 << 3 << 4 << 5 << 7},
    NoteSearchQueryTestData{
        QStringLiteral("infORMatioN -colLEgE pOtaTo -xHtMl"), QSet<int>{} << 2},
    NoteSearchQueryTestData{
        QStringLiteral("wikiPEDiA traNSActioN any: -PotaTo -biNARy"),
        QSet<int>{} << 3 << 4 << 5 << 6 << 7 << 8},
    // Search by phrases
    NoteSearchQueryTestData{QStringLiteral("\"The list\""), QSet<int>{} << 8},
    NoteSearchQueryTestData{
        QStringLiteral("\"tHe lIsT\" \"tHE lASt\" any: \"tHE xhTMl\""),
        QSet<int>{} << 1 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("any: \"The xhTMl\" eggS wikiPEDiA"),
        QSet<int>{} << 1 << 3 << 4 << 5 << 6 << 7 << 8},
    NoteSearchQueryTestData{
        QStringLiteral("\"tHE noTE\" -\"of tHE\""), QSet<int>{} << 5 << 6},
    // Search by phrases with wildcards
    NoteSearchQueryTestData{
        QStringLiteral("\"the canonic*\""), QSet<int>{} << 1},
    NoteSearchQueryTestData{
        QStringLiteral("\"the can*cal\""), QSet<int>{} << 1},
    NoteSearchQueryTestData{QStringLiteral("\"*onical\""), QSet<int>{} << 1},
    NoteSearchQueryTestData{
        QStringLiteral("\"*code characters\""), QSet<int>{} << 3 << 5},
    // Search by terms in Greek with diacritics
    NoteSearchQueryTestData{
        QString::fromUtf8("είναι"), QSet<int>{} << 0 << 1 << 2 << 3 << 6 << 7},
    NoteSearchQueryTestData{
        QString::fromUtf8("ΕΊΝΑΙ"), QSet<int>{} << 0 << 1 << 2 << 3 << 6 << 7},
    NoteSearchQueryTestData{
        QString::fromUtf8("ΕΊναι"), QSet<int>{} << 0 << 1 << 2 << 3 << 6 << 7},
    // Search by terms in Greek without diacritics
    NoteSearchQueryTestData{
        QString::fromUtf8("ειναι"), QSet<int>{} << 0 << 1 << 2 << 3 << 6 << 7},
    NoteSearchQueryTestData{
        QString::fromUtf8("ΕΙΝΑΙ"), QSet<int>{} << 0 << 1 << 2 << 3 << 6 << 7},
    NoteSearchQueryTestData{
        QString::fromUtf8("ΕΙναι"), QSet<int>{} << 0 << 1 << 2 << 3 << 6 << 7},
    // Search by terms in Greek with and without diacritics
    NoteSearchQueryTestData{
        QString::fromUtf8("ΕΊναι any: αυΤΟκιΝΗτο"),
        QSet<int>{} << 0 << 1 << 2 << 3 << 5 << 6 << 7},
};

INSTANTIATE_TEST_SUITE_P(
    NotesHandlerNoteSearchQueryTestInstance, NotesHandlerNoteSearchQueryTest,
    testing::ValuesIn(gNoteSearchQueryTestData));

TEST_P(NotesHandlerNoteSearchQueryTest, QueryNotes)
{
    const auto & testData = GetParam();

    NoteSearchQuery noteSearchQuery;
    ErrorString errorDescription;
    ASSERT_TRUE(
        noteSearchQuery.setQueryString(testData.queryString, errorDescription))
        << errorDescription.nonLocalizedString().toStdString();

    using FetchNoteOption = NotesHandler::FetchNoteOption;
    using FetchNoteOptions = NotesHandler::FetchNoteOptions;

    const auto fetchNoteOptions = FetchNoteOptions{} |
        FetchNoteOption::WithResourceMetadata |
        FetchNoteOption::WithResourceBinaryData;

    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto queryNotesFuture =
        notesHandler->queryNotes(noteSearchQuery, fetchNoteOptions);

    queryNotesFuture.waitForFinished();
    ASSERT_EQ(queryNotesFuture.resultCount(), 1);
    const auto notes = queryNotesFuture.result();

    if (notes.isEmpty()) {
        EXPECT_TRUE(testData.expectedContainedNotesIndices.isEmpty()) << ([&] {
            QString res;
            QTextStream strm{&res};
            strm << "Internal error: no notes corresponding to note search "
                 << "query were found; query string: " << testData.queryString
                 << "\nNote search query: " << noteSearchQuery;
            return res.toStdString();
        }());
    }
    else {
        QSet<int> containedNoteIndices;
        const int originalNoteCount = m_notes.size();
        for (int i = 0; i < originalNoteCount; ++i) {
            if (notes.contains(m_notes.at(i))) {
                containedNoteIndices.insert(i);
            }
        }

        EXPECT_EQ(containedNoteIndices, testData.expectedContainedNotesIndices)
            << ([&] {
                   QString res;
                   QTextStream strm{&res};
                   strm
                       << "unexpected result of note search query processing: ";
                   strm << "Expected note indices: ";
                   for (const int i:
                        qAsConst(testData.expectedContainedNotesIndices)) {
                       strm << i << "; ";
                   }
                   strm << "received note indices: ";
                   for (const int i: qAsConst(containedNoteIndices)) {
                       strm << i << "; ";
                   }

                   strm << "\nExpected notes: ";
                   for (int i = 0; i < originalNoteCount; ++i) {
                       if (!testData.expectedContainedNotesIndices.contains(i))
                       {
                           continue;
                       }

                       strm << m_notes.at(i) << "\n";
                   }

                   strm << "\nActual found notes: ";
                   for (const auto & note: qAsConst(notes)) {
                       strm << note << "\n";
                   }

                   strm << "\nQuery string: " << testData.queryString
                        << "\nNote search query: " << noteSearchQuery;

                   return res.toStdString();
               }());
    }
}

} // namespace quentier::local_storage::sql::tests

#include "NotesHandlerTest.moc"
