/*
 * Copyright 2016-2020 Dmitry Ivanov
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

#include "NoteLocalStorageManagerAsyncTester.h"

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/Compat.h>

#include <QPainter>
#include <QThread>

namespace quentier {
namespace test {

NoteLocalStorageManagerAsyncTester::NoteLocalStorageManagerAsyncTester(
    QObject * parent) :
    QObject(parent)
{}

NoteLocalStorageManagerAsyncTester::~NoteLocalStorageManagerAsyncTester()
{
    clear();
}

void NoteLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("NoteLocalStorageManagerAsyncTester");
    qint32 userId = 5;

    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(QStringLiteral(
        "NoteLocalStorageManagerAsyncTester-local-storage-thread"));

    m_pLocalStorageManagerThread->start();
}

void NoteLocalStorageManagerAsyncTester::initialize()
{
    m_notebook.clear();
    m_notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    m_notebook.setUpdateSequenceNumber(1);
    m_notebook.setName(QStringLiteral("Fake notebook name"));
    m_notebook.setCreationTimestamp(1);
    m_notebook.setModificationTimestamp(1);
    m_notebook.setDefaultNotebook(true);
    m_notebook.setLastUsed(false);
    m_notebook.setPublishingUri(QStringLiteral("Fake publishing uri"));
    m_notebook.setPublishingOrder(1);
    m_notebook.setPublishingAscending(true);

    m_notebook.setPublishingPublicDescription(
        QStringLiteral("Fake public description"));

    m_notebook.setPublished(true);
    m_notebook.setStack(QStringLiteral("Fake notebook stack"));

    m_notebook.setBusinessNotebookDescription(
        QStringLiteral("Fake business notebook description"));

    m_notebook.setBusinessNotebookPrivilegeLevel(1);
    m_notebook.setBusinessNotebookRecommended(true);

    ErrorString errorDescription;
    if (!m_notebook.checkParameters(errorDescription)) {
        QNWARNING(
            "tests:local_storage",
            "Found invalid notebook: " << m_notebook
                                       << ", error: " << errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_ADD_NOTEBOOK_REQUEST;
    Q_EMIT addNotebookRequest(m_notebook, QUuid::createUuid());
}

void NoteLocalStorageManagerAsyncTester::onAddNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else {                                                                     \
        errorDescription.setBase(                                              \
            "Internal error in NoteLocalStorageManagerAsyncTester: "           \
            "found wrong state");                                              \
        QNWARNING("tests:local_storage", errorDescription << ": " << m_state); \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
    }

    if (m_state == STATE_SENT_ADD_NOTEBOOK_REQUEST) {
        if (m_notebook != notebook) {
            errorDescription.setBase(
                "Internal error in NoteLocalStorageManagerAsyncTester: "
                "notebook in onAddNotebookCompleted slot "
                "doesn't match the original Notebook");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_initialNote.clear();

        m_initialNote.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000048"));

        m_initialNote.setUpdateSequenceNumber(1);
        m_initialNote.setTitle(QStringLiteral("Fake note"));

        m_initialNote.setContent(
            QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

        m_initialNote.setCreationTimestamp(1);
        m_initialNote.setModificationTimestamp(1);
        m_initialNote.setNotebookGuid(m_notebook.guid());
        m_initialNote.setNotebookLocalUid(m_notebook.localUid());
        m_initialNote.setActive(true);

        m_state = STATE_SENT_ADD_REQUEST;
        Q_EMIT addNoteRequest(m_initialNote, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTEBOOK_REQUEST) {
        Note extraNote;

        extraNote.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000006"));

        extraNote.setUpdateSequenceNumber(6);
        extraNote.setActive(true);

        extraNote.setContent(
            QStringLiteral("<en-note><h1>Hello, world 3</h1></en-note>"));

        extraNote.setCreationTimestamp(3);
        extraNote.setModificationTimestamp(3);
        extraNote.setNotebookGuid(m_extraNotebook.guid());
        extraNote.setNotebookLocalUid(m_extraNotebook.localUid());
        extraNote.setTitle(QStringLiteral("Fake note title three"));

        m_state = STATE_SENT_ADD_EXTRA_NOTE_THREE_REQUEST;
        Q_EMIT addNoteRequest(extraNote, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NoteLocalStorageManagerAsyncTester::onAddNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteLocalStorageManagerAsyncTester::onGetNoteCountCompleted(
    int count, LocalStorageManager::NoteCountOptions options, QUuid requestId)
{
    Q_UNUSED(options)
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST) {
        if (count != 1) {
            errorDescription.setBase(
                "GetNoteCount returned result different "
                "from the expected one (1)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedNote.setLocal(false);
        m_modifiedNote.setActive(false);
        m_modifiedNote.setDeletionTimestamp(3);
        m_state = STATE_SENT_DELETE_REQUEST;

        Q_EMIT updateNoteRequest(
            m_modifiedNote,
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
            LocalStorageManager::UpdateNoteOptions(),
#else
            LocalStorageManager::UpdateNoteOptions(0),
#endif
            QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST) {
        if (count != 0) {
            errorDescription.setBase(
                "GetNoteCount returned result different "
                "from the expected one (0)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        Note extraNote;

        extraNote.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000001"));

        extraNote.setUpdateSequenceNumber(1);
        extraNote.setActive(true);

        extraNote.setContent(
            QStringLiteral("<en-note><h1>Hello, world 1</h1></en-note>"));

        extraNote.setCreationTimestamp(1);
        extraNote.setModificationTimestamp(1);
        extraNote.setNotebookGuid(m_notebook.guid());
        extraNote.setNotebookLocalUid(m_notebook.localUid());
        extraNote.setTitle(QStringLiteral("Fake note title one"));

        Resource resource;

        resource.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000002"));

        resource.setUpdateSequenceNumber(2);
        resource.setNoteGuid(extraNote.guid());
        resource.setDataBody(QByteArray("Fake resource data body"));
        resource.setDataSize(resource.dataBody().size());
        resource.setDataHash(QByteArray("Fake hash      1"));
        resource.setMime(QStringLiteral("text/plain"));
        resource.setHeight(20);
        resource.setWidth(20);

        extraNote.addResource(resource);

        Resource resource2;

        resource2.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000009"));

        resource2.setUpdateSequenceNumber(3);
        resource2.setNoteGuid(extraNote.guid());
        resource2.setDataBody(QByteArray("Fake resource data body"));
        resource2.setDataSize(resource.dataBody().size());
        resource2.setDataHash(QByteArray("Fake hash      9"));
        resource2.setMime(QStringLiteral("text/plain"));
        resource2.setHeight(30);
        resource2.setWidth(30);

        extraNote.addResource(resource2);

        auto & noteAttributes = extraNote.noteAttributes();
        noteAttributes.altitude = 20.0;
        noteAttributes.latitude = 10.0;
        noteAttributes.longitude = 30.0;

        noteAttributes.author =
            QStringLiteral("NoteLocalStorageManagerAsyncTester");

        noteAttributes.lastEditedBy = QStringLiteral("Same as author");
        noteAttributes.placeName = QStringLiteral("Testing hall");
        noteAttributes.sourceApplication = QStringLiteral("tester");

        m_state = STATE_SENT_ADD_EXTRA_NOTE_ONE_REQUEST;
        Q_EMIT addNoteRequest(extraNote, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NoteLocalStorageManagerAsyncTester::onGetNoteCountFailed(
    ErrorString errorDescription, LocalStorageManager::NoteCountOptions options,
    QUuid requestId)
{
    Q_UNUSED(options)

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteLocalStorageManagerAsyncTester::onAddNoteCompleted(
    Note note, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_REQUEST) {
        if (m_initialNote != note) {
            errorDescription.setBase(
                "Internal error in NoteLocalStorageManagerAsyncTester: "
                "note in onAddNoteCompleted slot doesn't match the original "
                "Note");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundNote = Note();
        m_foundNote.setLocalUid(note.localUid());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata |
            LocalStorageManager::GetNoteOption::WithResourceBinaryData);

        Q_EMIT findNoteRequest(m_foundNote, options, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTE_ONE_REQUEST) {
        m_initialNotes << note;

        Note extraNote;

        extraNote.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000004"));

        extraNote.setUpdateSequenceNumber(4);
        extraNote.setActive(true);

        extraNote.setContent(
            QStringLiteral("<en-note><h1>Hello, world 2</h1></en-note>"));

        extraNote.setCreationTimestamp(2);
        extraNote.setModificationTimestamp(2);
        extraNote.setNotebookGuid(m_notebook.guid());
        extraNote.setNotebookLocalUid(m_notebook.localUid());
        extraNote.setTitle(QStringLiteral("Fake note title two"));

        m_state = STATE_SENT_ADD_EXTRA_NOTE_TWO_REQUEST;
        Q_EMIT addNoteRequest(extraNote, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTE_TWO_REQUEST) {
        m_initialNotes << note;

        m_extraNotebook.clear();

        m_extraNotebook.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000005"));

        m_extraNotebook.setUpdateSequenceNumber(1);
        m_extraNotebook.setName(QStringLiteral("Fake notebook name two"));
        m_extraNotebook.setCreationTimestamp(1);
        m_extraNotebook.setModificationTimestamp(1);
        m_extraNotebook.setDefaultNotebook(false);
        m_extraNotebook.setLastUsed(true);

        m_state = STATE_SENT_ADD_EXTRA_NOTEBOOK_REQUEST;
        Q_EMIT addNotebookRequest(m_extraNotebook, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_ADD_EXTRA_NOTE_THREE_REQUEST) {
        m_initialNotes << note;

        m_state = STATE_SENT_LIST_NOTES_PER_NOTEBOOK_ONE_REQUEST;

        LocalStorageManager::ListObjectsOptions flag =
            LocalStorageManager::ListObjectsOption::ListAll;

        size_t limit = 0, offset = 0;
        auto order = LocalStorageManager::ListNotesOrder::NoOrder;
        auto orderDirection = LocalStorageManager::OrderDirection::Ascending;

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata |
            LocalStorageManager::GetNoteOption::WithResourceBinaryData);

        Q_EMIT listNotesPerNotebookRequest(
            m_notebook, options, flag, limit, offset, order, orderDirection,
            QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NoteLocalStorageManagerAsyncTester::onAddNoteFailed(
    Note note, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", note: " << note);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteLocalStorageManagerAsyncTester::onUpdateNoteCompleted(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    Q_UNUSED(options)
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST) {
        if (m_modifiedNote != note) {
            errorDescription.setBase(
                "Internal error in NoteLocalStorageManagerAsyncTester: "
                "note in onUpdateNoteCompleted slot doesn't match the original "
                "updated Note");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;

        LocalStorageManager::GetNoteOptions options(
            LocalStorageManager::GetNoteOption::WithResourceMetadata |
            LocalStorageManager::GetNoteOption::WithResourceBinaryData);

        Q_EMIT findNoteRequest(m_foundNote, options, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_DELETE_REQUEST) {
        ErrorString errorDescription;

        if (m_modifiedNote != note) {
            errorDescription.setBase(
                "Internal error in NoteLocalStorageManagerAsyncTester: "
                "note in onUpdateNoteCompleted slot after the deletion update "
                "doesn't match the original deleted Note");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedNote.setLocal(true);
        m_state = STATE_SENT_EXPUNGE_REQUEST;
        Q_EMIT expungeNoteRequest(m_modifiedNote, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void NoteLocalStorageManagerAsyncTester::onUpdateNoteFailed(
    Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(options)

    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", note: " << note);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteLocalStorageManagerAsyncTester::onFindNoteCompleted(
    Note note, LocalStorageManager::GetNoteOptions options, QUuid requestId)
{
    Q_UNUSED(requestId)
    Q_UNUSED(options)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST) {
        if (m_initialNote != note) {
            errorDescription.setBase(
                "Internal error in NoteLocalStorageManagerAsyncTester: "
                "note in onFindNoteCompleted slot doesn't match the original "
                "Note");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original note: " << m_initialNote
                                 << "\nFound note: " << note);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found note is good, updating it now
        m_modifiedNote = m_initialNote;

        m_modifiedNote.setUpdateSequenceNumber(
            m_initialNote.updateSequenceNumber() + 1);

        m_modifiedNote.setTitle(
            m_initialNote.title() + QStringLiteral("_modified"));

        m_state = STATE_SENT_UPDATE_REQUEST;

        LocalStorageManager::UpdateNoteOptions options(
            LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
            LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData |
            LocalStorageManager::UpdateNoteOption::UpdateTags);

        Q_EMIT updateNoteRequest(m_modifiedNote, options, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST) {
        if (m_modifiedNote != note) {
            errorDescription.setBase(
                "Internal error in NoteLocalStorageManagerAsyncTester: "
                "not in onFindNoteCompleted slot doesn't match the original "
                "modified Note");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_modifiedNote = note;
        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;

        LocalStorageManager::NoteCountOptions options(
            LocalStorageManager::NoteCountOption::IncludeNonDeletedNotes);

        Q_EMIT getNoteCountRequest(options, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        errorDescription.setBase(
            "Found note which should have been expunged from the local "
            "storage");

        QNWARNING(
            "tests:local_storage",
            errorDescription
                << ": Note expunged from LocalStorageManager: "
                << m_modifiedNote
                << "\nNote found in LocalStorageManager: " << m_foundNote);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void NoteLocalStorageManagerAsyncTester::onFindNoteFailed(
    Note note, LocalStorageManager::GetNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;

        LocalStorageManager::NoteCountOptions options(
            LocalStorageManager::NoteCountOption::IncludeNonDeletedNotes);

        Q_EMIT getNoteCountRequest(options, QUuid::createUuid());
        return;
    }

    QNWARNING(
        "tests:local_storage",
        errorDescription
            << ", requestId = " << requestId << ", note: " << note
            << "\nWith resource metadata = "
            << ((options &
                 LocalStorageManager::GetNoteOption::WithResourceMetadata)
                    ? "true"
                    : "false")
            << ", with resource binary data = "
            << ((options &
                 LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                    ? "true"
                    : "false"));

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteLocalStorageManagerAsyncTester::onListNotesPerNotebookCompleted(
    Notebook notebook, LocalStorageManager::GetNoteOptions options,
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection, QList<Note> notes,
    QUuid requestId)
{
    Q_UNUSED(notebook)
    Q_UNUSED(options)
    Q_UNUSED(flag)
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_LIST_NOTES_PER_NOTEBOOK_ONE_REQUEST) {
        for (const auto & note: qAsConst(notes)) {
            if (!m_initialNotes.contains(note)) {
                errorDescription.setBase(
                    "One of found notes was not found "
                    "within initial notes");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ", unfound note: " << note);

                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }

            if (note.notebookGuid() != m_notebook.guid()) {
                errorDescription.setBase(
                    "One of found notes has invalid notebook guid");

                errorDescription.details() = QStringLiteral("expected ");
                errorDescription.details() += m_notebook.guid();
                errorDescription.details() += QStringLiteral(", found: ");
                errorDescription.details() += note.notebookGuid();
                QNWARNING("tests:local_storage", errorDescription);
                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }
        }
    }
    else if (m_state == STATE_SENT_LIST_NOTES_PER_NOTEBOOK_TWO_REQUEST) {
        for (const auto & note: qAsConst(notes)) {
            if (!m_initialNotes.contains(note)) {
                errorDescription.setBase(
                    "One of found notes was not found within initial notes");

                QNWARNING("tests:local_storage", errorDescription);
                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }

            if (note.notebookGuid() != m_extraNotebook.guid()) {
                errorDescription.setBase(
                    "One of found notes has invalid notebook guid");

                errorDescription.details() = QStringLiteral("expected");
                errorDescription.details() += m_extraNotebook.guid();
                errorDescription.details() += QStringLiteral(", found: ");
                errorDescription.details() += note.notebookGuid();
                QNWARNING("tests:local_storage", errorDescription);
                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }
        }
    }
    HANDLE_WRONG_STATE();

    Q_EMIT success();
}

void NoteLocalStorageManagerAsyncTester::onListNotesPerNotebookFailed(
    Notebook notebook, LocalStorageManager::GetNoteOptions options,
    LocalStorageManager::ListObjectsOptions flag, size_t limit, size_t offset,
    LocalStorageManager::ListNotesOrder order,
    LocalStorageManager::OrderDirection orderDirection,
    ErrorString errorDescription, QUuid requestId)
{
    Q_UNUSED(flag)
    Q_UNUSED(limit)
    Q_UNUSED(offset)
    Q_UNUSED(order)
    Q_UNUSED(orderDirection)

    QNWARNING(
        "tests:local_storage",
        errorDescription
            << ", requestId = " << requestId << ", notebook: " << notebook
            << ", with resource metadata = "
            << ((options &
                 LocalStorageManager::GetNoteOption::WithResourceMetadata)
                    ? "true"
                    : "false")
            << ", with resource binary data = "
            << ((options &
                 LocalStorageManager::GetNoteOption::WithResourceBinaryData)
                    ? "true"
                    : "false"));

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteLocalStorageManagerAsyncTester::onExpungeNoteCompleted(
    Note note, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedNote != note) {
        errorDescription.setBase(
            "Internal error in NoteLocalStorageManagerAsyncTester: "
            "note in onExpungeNoteCompleted slot doesn't match the original "
            "expunged Note");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;

    LocalStorageManager::GetNoteOptions options(
        LocalStorageManager::GetNoteOption::WithResourceMetadata |
        LocalStorageManager::GetNoteOption::WithResourceBinaryData);

    Q_EMIT findNoteRequest(m_foundNote, options, QUuid::createUuid());
}

void NoteLocalStorageManagerAsyncTester::onExpungeNoteFailed(
    Note note, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", note: " << note);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &QThread::deleteLater);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &NoteLocalStorageManagerAsyncTester::initialize);

    // Request --> slot connections
    QObject::connect(
        this, &NoteLocalStorageManagerAsyncTester::addNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest);

    QObject::connect(
        this, &NoteLocalStorageManagerAsyncTester::getNoteCountRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onGetNoteCountRequest);

    QObject::connect(
        this, &NoteLocalStorageManagerAsyncTester::addNoteRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNoteRequest);

    QObject::connect(
        this, &NoteLocalStorageManagerAsyncTester::updateNoteRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    QObject::connect(
        this, &NoteLocalStorageManagerAsyncTester::findNoteRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onFindNoteRequest);

    QObject::connect(
        this, &NoteLocalStorageManagerAsyncTester::listNotesPerNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onListNotesPerNotebookRequest);

    QObject::connect(
        this, &NoteLocalStorageManagerAsyncTester::expungeNoteRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeNoteRequest);

    // Slot <-- result connections
    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &NoteLocalStorageManagerAsyncTester::onAddNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookFailed, this,
        &NoteLocalStorageManagerAsyncTester::onAddNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getNoteCountComplete, this,
        &NoteLocalStorageManagerAsyncTester::onGetNoteCountCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getNoteCountFailed, this,
        &NoteLocalStorageManagerAsyncTester::onGetNoteCountFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &NoteLocalStorageManagerAsyncTester::onAddNoteCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteFailed,
        this, &NoteLocalStorageManagerAsyncTester::onAddNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &NoteLocalStorageManagerAsyncTester::onUpdateNoteCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteFailed, this,
        &NoteLocalStorageManagerAsyncTester::onUpdateNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findNoteComplete, this,
        &NoteLocalStorageManagerAsyncTester::onFindNoteCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::findNoteFailed,
        this, &NoteLocalStorageManagerAsyncTester::onFindNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listNotesPerNotebookComplete, this,
        &NoteLocalStorageManagerAsyncTester::onListNotesPerNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::listNotesPerNotebookFailed, this,
        &NoteLocalStorageManagerAsyncTester::onListNotesPerNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteComplete, this,
        &NoteLocalStorageManagerAsyncTester::onExpungeNoteCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeNoteFailed, this,
        &NoteLocalStorageManagerAsyncTester::onExpungeNoteFailed);
}

void NoteLocalStorageManagerAsyncTester::clear()
{
    if (m_pLocalStorageManagerThread) {
        m_pLocalStorageManagerThread->quit();
        m_pLocalStorageManagerThread->wait();
        m_pLocalStorageManagerThread->deleteLater();
        m_pLocalStorageManagerThread = nullptr;
    }

    if (m_pLocalStorageManagerAsync) {
        m_pLocalStorageManagerAsync->deleteLater();
        m_pLocalStorageManagerAsync = nullptr;
    }

    m_state = STATE_UNINITIALIZED;
}

} // namespace test
} // namespace quentier
