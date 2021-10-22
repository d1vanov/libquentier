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

#include "../NotesHandler.h"
#include "../NotebooksHandler.h"
#include "../ConnectionPool.h"
#include "../LinkedNotebooksHandler.h"
#include "../NotebooksHandler.h"
#include "../Notifier.h"
#include "../TablesInitializer.h"

#include <quentier/exception/IQuentierException.h>
#include <quentier/utility/UidGenerator.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFlags>
#include <QFutureSynchronizer>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>

#include <gtest/gtest.h>

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
    const std::optional<qevercloud::Guid> & noteGuid)
{
    const int sharedNoteCount = 5;
    QList<qevercloud::SharedNote> sharedNotes;
    sharedNotes.reserve(sharedNoteCount);
    for (int i = 0; i < sharedNoteCount; ++i) {
        qevercloud::SharedNote sharedNote;
        sharedNote.setLocallyModified(i % 2 == 0);
        sharedNote.setLocalOnly(i % 3 == 0);
        sharedNote.setLocallyFavorited(i % 4 == 0);

        QHash<QString, QVariant> localData;
        localData[QStringLiteral("heySharedNote")] =
            QStringLiteral("hiSharedNote");

        sharedNote.setLocalData(std::move(localData));

        sharedNote.setSharerUserID(qevercloud::UserID{10});

        if (i % 2 == 0)
        {
            qevercloud::Identity recipientIdentity;
            recipientIdentity.setId(qevercloud::IdentityID{i * 20});

            if (i % 4 == 0) {
                qevercloud::Contact contact;
                contact.setName(QStringLiteral("contactName"));
                contact.setId(QStringLiteral("contactId"));
                contact.setType(qevercloud::ContactType::EVERNOTE);
                contact.setPhotoUrl(QStringLiteral("https://www.example.com"));

                contact.setPhotoLastUpdated(
                    QDateTime::currentMSecsSinceEpoch());

                contact.setMessagingPermit(
                    QByteArray::fromStdString("aaaa"s));

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

    for (int i = 0; i < resourceCount; ++i)
    {
        qevercloud::Resource resource;
        resource.setLocallyModified(true);
        resource.setLocalOnly(false);
        resource.setLocallyFavorited(true);

        QHash<QString, QVariant> localData;
        localData[QStringLiteral("heyResource")] = QStringLiteral("hiResource");
        resource.setLocalData(std::move(localData));

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
        recognition.setBody(
            QByteArray::fromStdString("test resource recognition data"s));

        recognition.setSize(recognition.body()->size());

        recognition.setBodyHash(QCryptographicHash::hash(
            *recognition.body(), QCryptographicHash::Md5));

        resource.setMime("application/text-plain");

        resource.setWidth(10);
        resource.setHeight(20);

        resource.setActive(true);

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

    QHash<QString, QVariant> localData;
    localData[QStringLiteral("hey")] = QStringLiteral("hi");
    note.setLocalData(std::move(localData));

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
        note.setSharedNotes(createSharedNotes(note.guid()));
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
    NotesHandlerTest,
    ShouldHaveZeroNoteCountPerTagLocalIdWhenThereAreNoNotes)
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

TEST_F(NotesHandlerTest, ShouldHaveZeroNoteCountsPerTagsWhenThereAreNeitherNotesNorTags)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    using NoteCountOption = NotesHandler::NoteCountOption;
    using NoteCountOptions = NotesHandler::NoteCountOptions;

    auto listTagsOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListTagsOrder>{};

    listTagsOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto noteCountsFuture = notesHandler->noteCountsPerTags(
        listTagsOptions,
        NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes} |
            NoteCountOption::IncludeDeletedNotes);

    noteCountsFuture.waitForFinished();
    EXPECT_EQ(noteCountsFuture.result().size(), 0);
}

TEST_F(NotesHandlerTest, ShouldHaveZeroNoteCountPerNotebookAndTagLocalidsWhenThereAreNoNotes)
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
    EXPECT_EQ(noteFuture.resultCount(), 0);
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
    EXPECT_EQ(noteFuture.resultCount(), 0);
}

TEST_F(NotesHandlerTest, IgnoreAttemptToExpungeNonexistentNoteByLocalId)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeNoteFuture = notesHandler->expungeNoteByLocalId(
        UidGenerator::Generate());

    EXPECT_NO_THROW(expungeNoteFuture.waitForFinished());
}

TEST_F(NotesHandlerTest, IgnoreAttemptToExpungeNonexistentNoteByGuid)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto expungeNoteFuture = notesHandler->expungeNoteByGuid(
        UidGenerator::Generate());

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

    auto listNotesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotesOrder>{};

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

    auto listNotesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotesOrder>{};

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

    auto listNotesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotesOrder>{};

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

    auto listNotesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotesOrder>{};

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

    auto listNotesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotesOrder>{};

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

static const qevercloud::Notebook gNotebook = createNotebook();

const std::array gNoteTestValues{
    createNote(gNotebook),
};

INSTANTIATE_TEST_SUITE_P(
    NotesHandlerSingleNoteTestInstance,
    NotesHandlerSingleNoteTest,
    testing::ValuesIn(gNoteTestValues));

TEST_P(NotesHandlerSingleNoteTest, HandleSingleNote)
{
    const auto notesHandler = std::make_shared<NotesHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    NotesHandlerTestNotifierListener notifierListener;

    QObject::connect(
        m_notifier,
        &Notifier::notePut,
        &notifierListener,
        &NotesHandlerTestNotifierListener::onNotePut);

    QObject::connect(
        m_notifier,
        &Notifier::noteUpdated,
        &notifierListener,
        &NotesHandlerTestNotifierListener::onNoteUpdated);

    QObject::connect(
        m_notifier,
        &Notifier::noteExpunged,
        &notifierListener,
        &NotesHandlerTestNotifierListener::onNoteExpunged);

    const auto notebooksHandler = std::make_shared<NotebooksHandler>(
        m_connectionPool, QThreadPool::globalInstance(), m_notifier,
        m_writerThread, m_temporaryDir.path(), m_resourceDataFilesLock);

    auto putNotebookFuture = notebooksHandler->putNotebook(gNotebook);
    putNotebookFuture.waitForFinished();

    const auto note = GetParam();

    auto putNoteFuture = notesHandler->putNote(note);
    putNoteFuture.waitForFinished();

    QCoreApplication::processEvents();
    ASSERT_EQ(notifierListener.putNotes().size(), 1);
    EXPECT_EQ(notifierListener.putNotes()[0], note);

    using NoteCountOption = ILocalStorage::NoteCountOption;
    using NoteCountOptions = ILocalStorage::NoteCountOptions;

    const auto noteCountOptions =
        (note.deleted()
             ? NoteCountOptions{NoteCountOption::IncludeDeletedNotes}
             : NoteCountOptions{NoteCountOption::IncludeNonDeletedNotes});

    auto noteCountFuture = notesHandler->noteCount(noteCountOptions);
    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 1U);

    noteCountFuture = notesHandler->noteCountPerNotebookLocalId(
        gNotebook.localId(), noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 1U);

    for (const auto & tagLocalId: qAsConst(note.tagLocalIds())) {
        noteCountFuture = notesHandler->noteCountPerTagLocalId(
            tagLocalId, noteCountOptions);

        noteCountFuture.waitForFinished();
        EXPECT_EQ(noteCountFuture.result(), 1U);
    }

    noteCountFuture = notesHandler->noteCountPerNotebookAndTagLocalIds(
        QStringList{} << gNotebook.localId(), note.tagLocalIds(),
        noteCountOptions);

    noteCountFuture.waitForFinished();
    EXPECT_EQ(noteCountFuture.result(), 1U);

    using FetchNoteOption = ILocalStorage::FetchNoteOption;
    using FetchNoteOptions = ILocalStorage::FetchNoteOptions;

    const auto fetchNoteOptions = FetchNoteOptions{} |
        FetchNoteOption::WithResourceMetadata |
        FetchNoteOption::WithResourceBinaryData;

    auto foundByLocalIdNoteFuture = notesHandler->findNoteByLocalId(
        note.localId(), fetchNoteOptions);

    foundByLocalIdNoteFuture.waitForFinished();
    ASSERT_EQ(foundByLocalIdNoteFuture.resultCount(), 1);
    EXPECT_EQ(foundByLocalIdNoteFuture.result(), note);

    auto foundByGuidNoteFuture = notesHandler->findNoteByGuid(
        note.guid().value(), fetchNoteOptions);

    foundByGuidNoteFuture.waitForFinished();
    ASSERT_EQ(foundByGuidNoteFuture.resultCount(), 1);
    EXPECT_EQ(foundByGuidNoteFuture.result(), note);

    auto listNotesOptions =
        ILocalStorage::ListOptions<ILocalStorage::ListNotesOrder>{};

    listNotesOptions.m_flags = ILocalStorage::ListObjectsOptions{
        ILocalStorage::ListObjectsOption::ListAll};

    auto listNotesFuture = notesHandler->listNotes(
        fetchNoteOptions, listNotesOptions);

    listNotesFuture.waitForFinished();
    ASSERT_EQ(listNotesFuture.result().size(), 1);
    EXPECT_EQ(listNotesFuture.result().at(0), note);

    auto sharedNotesFuture = notesHandler->listSharedNotes(note.guid().value());
    sharedNotesFuture.waitForFinished();

    EXPECT_EQ(
        sharedNotesFuture.result(),
        note.sharedNotes().value_or(QList<qevercloud::SharedNote>{}));

    listNotesFuture = notesHandler->listNotesPerNotebookLocalId(
        gNotebook.localId(), fetchNoteOptions, listNotesOptions);

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
}

} // namespace quentier::local_storage::sql::tests

#include "NotesHandlerTest.moc"
