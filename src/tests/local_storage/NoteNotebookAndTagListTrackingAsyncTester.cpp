/*
 * Copyright 2019-2021 Dmitry Ivanov
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

#include "NoteNotebookAndTagListTrackingAsyncTester.h"

#include <quentier/logging/QuentierLogger.h>

#include <QThread>

namespace quentier {
namespace test {

NoteNotebookAndTagListTrackingAsyncTester::
    NoteNotebookAndTagListTrackingAsyncTester(QObject * parent) :
    QObject(parent)
{}

NoteNotebookAndTagListTrackingAsyncTester::
    ~NoteNotebookAndTagListTrackingAsyncTester()
{
    clear();
}

void NoteNotebookAndTagListTrackingAsyncTester::onInitTestCase()
{
    QString username =
        QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester");

    qint32 userId = 7;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    const Account account{username, Account::Type::Evernote, userId};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(QStringLiteral(
        "NoteNotebookAndTagListTrackingAsyncTester-local-storage-thread"));

    m_pLocalStorageManagerThread->start();
}

void NoteNotebookAndTagListTrackingAsyncTester::initialize()
{
    m_firstNotebook.setUpdateSequenceNum(1);
    m_firstNotebook.setName(QStringLiteral("Previous"));
    m_firstNotebook.setServiceCreated(QDateTime::currentMSecsSinceEpoch());

    m_firstNotebook.setServiceUpdated(
        m_firstNotebook.serviceCreated());

    m_firstNotebook.setDefaultNotebook(true);

    m_secondNotebook.setUpdateSequenceNum(2);
    m_secondNotebook.setName(QStringLiteral("New"));
    m_secondNotebook.setServiceCreated(QDateTime::currentMSecsSinceEpoch());

    m_secondNotebook.setServiceUpdated(
        m_secondNotebook.serviceCreated());

    int numTags = 3;
    m_firstNoteTagsSet.reserve(numTags);
    for (int i = 0; i < numTags; ++i) {
        qevercloud::Tag tag;
        tag.setName(QStringLiteral("Previous ") + QString::number(i + 1));
        m_firstNoteTagsSet << tag;
    }

    m_secondNoteTagsSet.reserve(numTags);
    for (int i = 0; i < numTags; ++i) {
        qevercloud::Tag tag;
        tag.setName(QStringLiteral("New ") + QString::number(i + 1));
        m_secondNoteTagsSet << tag;
    }

    m_state = STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION;

    Q_EMIT addNotebook(m_firstNotebook, QUuid::createUuid());
    Q_EMIT addNotebook(m_secondNotebook, QUuid::createUuid());

    for (int i = 0; i < numTags; ++i) {
        Q_EMIT addTag(m_firstNoteTagsSet[i], QUuid::createUuid());
        Q_EMIT addTag(m_secondNoteTagsSet[i], QUuid::createUuid());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookComplete(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(notebook)
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION)) {
        ErrorString errorDescription(
            "Internal error: unexpected add notebook complete event");

        QNWARNING(
            "tests:local_storage", errorDescription << ", state = " << m_state);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    ++m_addedNotebooksCount;
    if (m_addedNotebooksCount != 2) {
        return;
    }

    if (m_addedTagsCount ==
        (m_firstNoteTagsSet.size() + m_secondNoteTagsSet.size()))
    {
        createNoteInLocalStorage();
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        "NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookFailed: "
            << errorDescription << ", notebook: " << notebook);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddTagComplete(
    qevercloud::Tag tag, QUuid requestId)
{
    Q_UNUSED(tag)
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION)) {
        ErrorString errorDescription(
            "Internal error: unexpected add tag complete event");

        QNWARNING(
            "tests:local_storage", errorDescription << ", state = " << m_state);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    ++m_addedTagsCount;

    if (m_addedTagsCount !=
        (m_firstNoteTagsSet.size() + m_secondNoteTagsSet.size()))
    {
        return;
    }

    if (m_addedNotebooksCount == 2) {
        createNoteInLocalStorage();
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddTagFailed(
    qevercloud::Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        "NoteNotebookAndTagListTrackingAsyncTester::onAddTagFailed: "
            << errorDescription << ", tag: " << tag);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNoteComplete(
    qevercloud::Note note, QUuid requestId)
{
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTE_CREATION)) {
        ErrorString errorDescription(
            "Internal error: unexpected add note complete event");

        QNWARNING(
            "tests:local_storage", errorDescription << ", state = " << m_state);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    qevercloud::Note modifiedNote = note;
    modifiedNote.setTitle(note.title().value() + QStringLiteral("2"));

    m_state = STATE_PENDING_NOTE_UPDATE_WITHOUT_NOTEBOOK_OR_TAG_LIST_CHANGE;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    LocalStorageManager::UpdateNoteOptions options;
#else
    LocalStorageManager::UpdateNoteOptions options(0);
#endif

    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNoteFailed(
    qevercloud::Note note, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        "NoteNotebookAndTagListTrackingAsyncTester::onAddNoteFailed: "
            << errorDescription << ", note: " << note);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteComplete(
    qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
    QUuid requestId)
{
    Q_UNUSED(options)
    Q_UNUSED(requestId)

    if (m_state ==
        STATE_PENDING_NOTE_UPDATE_WITHOUT_NOTEBOOK_OR_TAG_LIST_CHANGE) {
        if (Q_UNLIKELY(m_receivedNoteMovedToAnotherNotebookSignal)) {
            ErrorString errorDescription(
                "Detected note moved to another notebook signal "
                "when note's notebook was not changed");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_receivedNoteTagsListChangedSignal)) {
            ErrorString errorDescription(
                "Detected note tags list updated signal when "
                "note's tags were not changed");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note = note;
        moveNoteToAnotherNotebook();
    }
    else if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY) {
        if (Q_UNLIKELY(m_receivedNoteTagsListChangedSignal)) {
            ErrorString errorDescription(
                "Detected note tags list updated signal when "
                "note's tags were not changed");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note = note;

        m_receivedUpdateNoteCompleteSignal = true;
        if (m_receivedNoteMovedToAnotherNotebookSignal) {
            changeNoteTagsList();
        }
    }
    else if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_TAG_LIST_CHANGE_ONLY) {
        if (Q_UNLIKELY(m_receivedNoteMovedToAnotherNotebookSignal)) {
            ErrorString errorDescription(
                "Detected note moved to another notebook signal "
                "when note's notebook was not changed");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note = note;

        m_receivedUpdateNoteCompleteSignal = true;
        if (m_receivedNoteTagsListChangedSignal) {
            moveNoteToAnotherNotebookAlongWithTagListChange();
        }
    }
    else if (
        m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES)
    {
        m_note = note;

        m_receivedUpdateNoteCompleteSignal = true;
        if (m_receivedNoteMovedToAnotherNotebookSignal &&
            m_receivedNoteTagsListChangedSignal)
        {
            Q_EMIT success();
        }
    }
    else {
        ErrorString errorDescription(
            "Internal error: unexpected update note complete event");

        QNWARNING(
            "tests:local_storage", errorDescription << ", state = " << m_state);

        Q_EMIT failure(errorDescription.nonLocalizedString());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteFailed(
    qevercloud::Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        "NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteFailed: "
            << errorDescription << ", note: " << note);

    Q_UNUSED(options)
    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onNoteMovedToAnotherNotebook(
    QString noteLocalId, QString previousNotebookLocalId,
    QString newNotebookLocalId)
{
    ++m_noteMovedToAnotherNotebookSlotInvocationCount;
    if (Q_UNLIKELY(m_noteMovedToAnotherNotebookSlotInvocationCount > 2)) {
        ErrorString errorDescription(
            "Too many note moved to another notebook signals received");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY) {
        if (Q_UNLIKELY(m_note.localId() != noteLocalId)) {
            ErrorString errorDescription(
                "Internal error: unexpected note local uid in note "
                "moved to another notebook signal");

            errorDescription.details() = noteLocalId;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localId();
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_firstNotebook.localId() != previousNotebookLocalId))
        {
            ErrorString errorDescription(
                "Internal error: unexpected previous notebook local "
                "uid in note moved to another notebook signal");

            errorDescription.details() = previousNotebookLocalId;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_firstNotebook.localId();
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_secondNotebook.localId() != newNotebookLocalId)) {
            ErrorString errorDescription(
                "Internal error: unexpected new notebook local uid "
                "in note moved to another notebook signal");

            errorDescription.details() = newNotebookLocalId;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_secondNotebook.localId();
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_receivedNoteTagsListChangedSignal)) {
            ErrorString errorDescription(
                "Detected note tags list updated signal when note's "
                "tags were not changed");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_receivedNoteMovedToAnotherNotebookSignal = true;
        if (m_receivedUpdateNoteCompleteSignal) {
            changeNoteTagsList();
        }
    }
    else if (
        m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES)
    {
        if (Q_UNLIKELY(m_note.localId() != noteLocalId)) {
            ErrorString errorDescription(
                "Internal error: unexpected note local uid in note "
                "moved to another notebook signal");

            errorDescription.details() = noteLocalId;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localId();
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_secondNotebook.localId() != previousNotebookLocalId))
        {
            ErrorString errorDescription(
                "Internal error: unexpected previous notebook local "
                "uid in note moved to another notebook signal");

            errorDescription.details() = previousNotebookLocalId;

            errorDescription.details() +=
                QStringLiteral("; expected ") + m_secondNotebook.localId();

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_firstNotebook.localId() != newNotebookLocalId)) {
            ErrorString errorDescription(
                "Internal error: unexpected new notebook local uid in note "
                "moved to another notebook signal");

            errorDescription.details() = newNotebookLocalId;

            errorDescription.details() +=
                QStringLiteral("; expected ") + m_firstNotebook.localId();

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_receivedNoteTagsListChangedSignal = true;

        if (m_receivedUpdateNoteCompleteSignal &&
            m_receivedNoteTagsListChangedSignal) {
            Q_EMIT success();
        }
    }
    else {
        ErrorString errorDescription(
            "Internal error: unexpected note moved to another notebook event");

        QNWARNING(
            "tests:local_storage", errorDescription << ", state = " << m_state);

        Q_EMIT failure(errorDescription.nonLocalizedString());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onNoteTagListUpdated(
    QString noteLocalId, QStringList previousTagLocalIds,
    QStringList newTagLocalIds)
{
    ++m_noteTagsListChangedSlotInvocationCount;
    if (Q_UNLIKELY(m_noteTagsListChangedSlotInvocationCount > 2)) {
        ErrorString errorDescription(
            "Too many note tags list changed signals received");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_TAG_LIST_CHANGE_ONLY) {
        if (Q_UNLIKELY(m_note.localId() != noteLocalId)) {
            ErrorString errorDescription(
                "Internal error: unexpected note local uid in note "
                "tags list updated signal");

            errorDescription.details() = noteLocalId;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localId();
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(
                !checkTagsListEqual(m_firstNoteTagsSet, previousTagLocalIds)))
        {
            ErrorString errorDescription(
                "Internal error: unexpected set of previous tag local "
                "uids in note tags list updated signal");

            errorDescription.details() =
                previousTagLocalIds.join(QStringLiteral(","));

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(
                !checkTagsListEqual(m_secondNoteTagsSet, newTagLocalIds))) {
            ErrorString errorDescription(
                "Internal error: unexpected set of new tag local uids "
                "in note tags list updated signal");

            errorDescription.details() =
                newTagLocalIds.join(QStringLiteral(","));

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_receivedNoteMovedToAnotherNotebookSignal)) {
            ErrorString errorDescription(
                "Detected note moved to another notebook signal "
                "when note's notebook was not changed");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_receivedNoteTagsListChangedSignal = true;
        if (m_receivedUpdateNoteCompleteSignal) {
            moveNoteToAnotherNotebookAlongWithTagListChange();
        }
    }
    else if (
        m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES)
    {
        if (Q_UNLIKELY(m_note.localId() != noteLocalId)) {
            ErrorString errorDescription(
                "Internal error: unexpected note local uid in note tags "
                "list updated signal");

            errorDescription.details() = noteLocalId;

            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localId();

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(
                !checkTagsListEqual(m_secondNoteTagsSet, previousTagLocalIds)))
        {
            ErrorString errorDescription(
                "Internal error: unexpected set of previous tag local "
                "uids in note tags list updated signal");

            errorDescription.details() =
                previousTagLocalIds.join(QStringLiteral(","));

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(
                !checkTagsListEqual(m_firstNoteTagsSet, newTagLocalIds))) {
            ErrorString errorDescription(
                "Internal error: unexpected set of new tag local uids "
                "in note tags list updated signal");

            errorDescription.details() =
                newTagLocalIds.join(QStringLiteral(","));

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_receivedNoteTagsListChangedSignal = true;

        if (m_receivedUpdateNoteCompleteSignal &&
            m_receivedNoteMovedToAnotherNotebookSignal)
        {
            Q_EMIT success();
        }
    }
    else {
        ErrorString errorDescription(
            "Internal error: unexpected note tags list update event");

        QNWARNING(
            "tests:local_storage", errorDescription << ", state = " << m_state);

        Q_EMIT failure(errorDescription.nonLocalizedString());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &QThread::deleteLater);

    QObject::connect(
        this, &NoteNotebookAndTagListTrackingAsyncTester::addNotebook,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest);

    QObject::connect(
        this, &NoteNotebookAndTagListTrackingAsyncTester::addTag,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddTagRequest);

    QObject::connect(
        this, &NoteNotebookAndTagListTrackingAsyncTester::addNote,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNoteRequest);

    QObject::connect(
        this, &NoteNotebookAndTagListTrackingAsyncTester::updateNote,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &NoteNotebookAndTagListTrackingAsyncTester::initialize);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookComplete);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookFailed, this,
        &NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &NoteNotebookAndTagListTrackingAsyncTester::onAddTagComplete);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addTagFailed,
        this, &NoteNotebookAndTagListTrackingAsyncTester::onAddTagFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &NoteNotebookAndTagListTrackingAsyncTester::onAddNoteComplete);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteFailed,
        this, &NoteNotebookAndTagListTrackingAsyncTester::onAddNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteComplete);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteFailed, this,
        &NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::noteMovedToAnotherNotebook, this,
        &NoteNotebookAndTagListTrackingAsyncTester::
            onNoteMovedToAnotherNotebook);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::noteTagListChanged, this,
        &NoteNotebookAndTagListTrackingAsyncTester::onNoteTagListUpdated);
}

void NoteNotebookAndTagListTrackingAsyncTester::clear()
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

void NoteNotebookAndTagListTrackingAsyncTester::createNoteInLocalStorage()
{
    qevercloud::Note note;
    note.setTitle(QStringLiteral("My grand note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello world!</h1></en-note>"));
    note.setNotebookLocalId(m_firstNotebook.localId());

    for (const auto & firstNoteTag: qAsConst(m_firstNoteTagsSet)) {
        note.mutableTagLocalIds().append(firstNoteTag.localId());
    }

    m_note = note;
    m_state = STATE_PENDING_NOTE_CREATION;
    Q_EMIT addNote(note, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::moveNoteToAnotherNotebook()
{
    qevercloud::Note modifiedNote = m_note;
    modifiedNote.setTitle(m_note.title().value() + QStringLiteral("3"));
    modifiedNote.setNotebookLocalId(m_secondNotebook.localId());

    m_state = STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    LocalStorageManager::UpdateNoteOptions options;
#else
    LocalStorageManager::UpdateNoteOptions options(0);
#endif

    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::changeNoteTagsList()
{
    qevercloud::Note modifiedNote = m_note;
    modifiedNote.setTitle(m_note.title().value() + QStringLiteral("4"));

    QStringList tagLocalIds;

    tagLocalIds.reserve(m_secondNoteTagsSet.size());
    for (const auto & secondNoteTag: qAsConst(m_secondNoteTagsSet)) {
        tagLocalIds << secondNoteTag.localId();
    }

    modifiedNote.setTagLocalIds(tagLocalIds);

    m_state = STATE_PENDING_NOTE_UPDATE_WITH_TAG_LIST_CHANGE_ONLY;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::
    moveNoteToAnotherNotebookAlongWithTagListChange()
{
    qevercloud::Note modifiedNote = m_note;
    modifiedNote.setTitle(m_note.title().value() + QStringLiteral("5"));
    modifiedNote.setNotebookLocalId(m_firstNotebook.localId());

    QStringList tagLocalIds;

    tagLocalIds.reserve(m_firstNoteTagsSet.size());
    for (const auto & firstNoteTag: qAsConst(m_firstNoteTagsSet)) {
        tagLocalIds << firstNoteTag.localId();
    }

    modifiedNote.setTagLocalIds(tagLocalIds);

    m_state = STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

bool NoteNotebookAndTagListTrackingAsyncTester::checkTagsListEqual(
    const QVector<qevercloud::Tag> & lhs, const QStringList & rhs) const
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (const auto & lhsTag: qAsConst(lhs)) {
        int index = rhs.indexOf(lhsTag.localId());
        if (index < 0) {
            return false;
        }
    }

    return true;
}

} // namespace test
} // namespace quentier
