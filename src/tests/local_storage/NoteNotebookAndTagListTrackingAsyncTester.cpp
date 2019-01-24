/*
 * Copyright 2019 Dmitry Ivanov
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

NoteNotebookAndTagListTrackingAsyncTester::NoteNotebookAndTagListTrackingAsyncTester(QObject * parent) :
    QObject(parent),
    m_state(STATE_UNINITIALIZED),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_pLocalStorageManagerThread(Q_NULLPTR),
    m_firstNotebook(),
    m_secondNotebook(),
    m_addedNotebooksCount(0),
    m_firstNoteTagsSet(),
    m_secondNoteTagsSet(),
    m_addedTagsCount(0),
    m_note(),
    m_receivedUpdateNoteCompleteSignal(false),
    m_receivedNoteMovedToAnotherNotebookSignal(false),
    m_receivedNoteTagsListChangedSignal(false),
    m_noteMovedToAnotherNotebookSlotInvocationCount(0),
    m_noteTagsListChangedSlotInvocationCount(0)
{}

NoteNotebookAndTagListTrackingAsyncTester::~NoteNotebookAndTagListTrackingAsyncTester()
{
    clear();
}

void NoteNotebookAndTagListTrackingAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester");
    qint32 userId = 7;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);
    LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);
    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(
        QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester-local-storage-thread"));
    m_pLocalStorageManagerThread->start();
}

void NoteNotebookAndTagListTrackingAsyncTester::initialize()
{
    m_firstNotebook.setUpdateSequenceNumber(1);
    m_firstNotebook.setName(QStringLiteral("Previous"));
    m_firstNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    m_firstNotebook.setModificationTimestamp(m_firstNotebook.creationTimestamp());
    m_firstNotebook.setDefaultNotebook(true);
    m_firstNotebook.setLastUsed(true);

    m_secondNotebook.setUpdateSequenceNumber(2);
    m_secondNotebook.setName(QStringLiteral("New"));
    m_secondNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    m_secondNotebook.setModificationTimestamp(m_secondNotebook.creationTimestamp());

    int numTags = 3;
    m_firstNoteTagsSet.reserve(numTags);
    for(int i = 0; i < numTags; ++i) {
        Tag tag;
        tag.setName(QStringLiteral("Previous ") + QString::number(i+1));
        m_firstNoteTagsSet << tag;
    }

    m_secondNoteTagsSet.reserve(numTags);
    for(int i = 0; i < numTags; ++i) {
        Tag tag;
        tag.setName(QStringLiteral("New ") + QString::number(i+1));
        m_secondNoteTagsSet << tag;
    }

    m_state = STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION;

    Q_EMIT addNotebook(m_firstNotebook, QUuid::createUuid());
    Q_EMIT addNotebook(m_secondNotebook, QUuid::createUuid());

    for(int i = 0; i < numTags; ++i) {
        Q_EMIT addTag(m_firstNoteTagsSet[i], QUuid::createUuid());
        Q_EMIT addTag(m_secondNoteTagsSet[i], QUuid::createUuid());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookComplete(Notebook notebook,
                                                                      QUuid requestId)
{
    Q_UNUSED(notebook)
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION))
    {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                    "add notebook complete event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
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

void NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookFailed(Notebook notebook,
                                                                    ErrorString errorDescription,
                                                                    QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookFailed: ")
              << errorDescription << QStringLiteral(", notebook: ") << notebook);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddTagComplete(Tag tag,
                                                                 QUuid requestId)
{
    Q_UNUSED(tag)
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION))
    {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                    "add tag complete event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
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

void NoteNotebookAndTagListTrackingAsyncTester::onAddTagFailed(Tag tag,
                                                               ErrorString errorDescription,
                                                               QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onAddTagFailed: ")
              << errorDescription << QStringLiteral(", tag: ") << tag);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNoteComplete(Note note,
                                                                  QUuid requestId)
{
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTE_CREATION))
    {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                    "add note complete event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    Note modifiedNote = note;
    modifiedNote.setTitle(note.title() + QStringLiteral("2"));

    m_state = STATE_PENDING_NOTE_UPDATE_WITHOUT_NOTEBOOK_OR_TAG_LIST_CHANGE;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

    LocalStorageManager::UpdateNoteOptions options(0);
    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNoteFailed(Note note,
                                                                ErrorString errorDescription,
                                                                QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onAddNoteFailed: ")
              << errorDescription << QStringLiteral(", note: ") << note);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteComplete(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    Q_UNUSED(options)
    Q_UNUSED(requestId)

    if (m_state == STATE_PENDING_NOTE_UPDATE_WITHOUT_NOTEBOOK_OR_TAG_LIST_CHANGE)
    {
        if (Q_UNLIKELY(m_receivedNoteMovedToAnotherNotebookSignal))
        {
            ErrorString errorDescription(
                QStringLiteral("Detected note moved to another notebook signal "
                               "when note's notebook was not changed"));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_receivedNoteTagsListChangedSignal))
        {
            ErrorString errorDescription(
                QStringLiteral("Detected note tags list updated signal when "
                               "note's tags were not changed"));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note = note;
        moveNoteToAnotherNotebook();
    }
    else if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY)
    {
        if (Q_UNLIKELY(m_receivedNoteTagsListChangedSignal))
        {
            ErrorString errorDescription(
                QStringLiteral("Detected note tags list updated signal when "
                               "note's tags were not changed"));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note = note;

        m_receivedUpdateNoteCompleteSignal = true;
        if (m_receivedNoteMovedToAnotherNotebookSignal) {
            changeNoteTagsList();
        }
    }
    else if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_TAG_LIST_CHANGE_ONLY)
    {
        if (Q_UNLIKELY(m_receivedNoteMovedToAnotherNotebookSignal))
        {
            ErrorString errorDescription(
                QStringLiteral("Detected note moved to another notebook signal "
                               "when note's notebook was not changed"));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note = note;

        m_receivedUpdateNoteCompleteSignal = true;
        if (m_receivedNoteTagsListChangedSignal) {
            moveNoteToAnotherNotebookAlongWithTagListChange();
        }
    }
    else if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES)
    {
        m_note = note;

        m_receivedUpdateNoteCompleteSignal = true;
        if (m_receivedNoteMovedToAnotherNotebookSignal &&
            m_receivedNoteTagsListChangedSignal)
        {
            Q_EMIT success();
        }
    }
    else
    {
        ErrorString errorDescription(
            QStringLiteral("Internal error: unexpected update note complete event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
        Q_EMIT failure(errorDescription.nonLocalizedString());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteFailed(
    Note note, LocalStorageManager::UpdateNoteOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteFailed: ")
              << errorDescription << QStringLiteral(", note: ") << note);

    Q_UNUSED(options)
    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onNoteMovedToAnotherNotebook(
    QString noteLocalUid, QString previousNotebookLocalUid,
    QString newNotebookLocalUid)
{
    ++m_noteMovedToAnotherNotebookSlotInvocationCount;
    if (Q_UNLIKELY(m_noteMovedToAnotherNotebookSlotInvocationCount > 2))
    {
        ErrorString errorDescription(QStringLiteral("Too many note moved to "
                                                    "another notebook signals "
                                                    "received"));
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY)
    {
        if (Q_UNLIKELY(m_note.localUid() != noteLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "note local uid in note "
                                                        "moved to another notebook "
                                                        "signal"));
            errorDescription.details() = noteLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_firstNotebook.localUid() != previousNotebookLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "previous notebook local "
                                                        "uid in note moved to "
                                                        "another notebook signal"));
            errorDescription.details() = previousNotebookLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_firstNotebook.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_secondNotebook.localUid() != newNotebookLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "new notebook local uid "
                                                        "in note moved to another "
                                                        "notebook signal"));
            errorDescription.details() = newNotebookLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_secondNotebook.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_receivedNoteTagsListChangedSignal))
        {
            ErrorString errorDescription(QStringLiteral("Detected note tags list "
                                                        "updated signal when note's "
                                                        "tags were not changed"));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_receivedNoteMovedToAnotherNotebookSignal = true;
        if (m_receivedUpdateNoteCompleteSignal) {
            changeNoteTagsList();
        }
    }
    else if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES)
    {
        if (Q_UNLIKELY(m_note.localUid() != noteLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "note local uid in note "
                                                        "moved to another notebook "
                                                        "signal"));
            errorDescription.details() = noteLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_secondNotebook.localUid() != previousNotebookLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "previous notebook local "
                                                        "uid in note moved to "
                                                        "another notebook signal"));
            errorDescription.details() = previousNotebookLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_secondNotebook.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_firstNotebook.localUid() != newNotebookLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: "
                                                        "unexpected new notebook "
                                                        "local uid in note moved "
                                                        "to another notebook signal"));
            errorDescription.details() = newNotebookLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_firstNotebook.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_receivedNoteTagsListChangedSignal = true;

        if (m_receivedUpdateNoteCompleteSignal && m_receivedNoteTagsListChangedSignal) {
            Q_EMIT success();
        }
    }
    else
    {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                    "note moved to another notebook "
                                                    "event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
        Q_EMIT failure(errorDescription.nonLocalizedString());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onNoteTagListUpdated(
    QString noteLocalUid, QStringList previousTagLocalUids,
    QStringList newTagLocalUids)
{
    ++m_noteTagsListChangedSlotInvocationCount;
    if (Q_UNLIKELY(m_noteTagsListChangedSlotInvocationCount > 2))
    {
        ErrorString errorDescription(QStringLiteral("Too many note tags list "
                                                    "changed signals received"));
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_TAG_LIST_CHANGE_ONLY)
    {
        if (Q_UNLIKELY(m_note.localUid() != noteLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "note local uid in note "
                                                        "tags list updated signal"));
            errorDescription.details() = noteLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(!checkTagsListEqual(m_firstNoteTagsSet, previousTagLocalUids)))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "set of previous tag local "
                                                        "uids in note tags list "
                                                        "updated signal"));
            errorDescription.details() = previousTagLocalUids.join(QStringLiteral(","));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(!checkTagsListEqual(m_secondNoteTagsSet, newTagLocalUids)))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "set of new tag local uids "
                                                        "in note tags list updated "
                                                        "signal"));
            errorDescription.details() = newTagLocalUids.join(QStringLiteral(","));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(m_receivedNoteMovedToAnotherNotebookSignal))
        {
            ErrorString errorDescription(QStringLiteral("Detected note moved to "
                                                        "another notebook signal "
                                                        "when note's notebook was "
                                                        "not changed"));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_receivedNoteTagsListChangedSignal = true;
        if (m_receivedUpdateNoteCompleteSignal) {
            moveNoteToAnotherNotebookAlongWithTagListChange();
        }
    }
    else if (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES)
    {
        if (Q_UNLIKELY(m_note.localUid() != noteLocalUid))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "note local uid in note tags "
                                                        "list updated signal"));
            errorDescription.details() = noteLocalUid;
            errorDescription.details() +=
                QStringLiteral("; expected ") + m_note.localUid();
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(!checkTagsListEqual(m_secondNoteTagsSet, previousTagLocalUids)))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "set of previous tag local "
                                                        "uids in note tags list "
                                                        "updated signal"));
            errorDescription.details() = previousTagLocalUids.join(QStringLiteral(","));
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (Q_UNLIKELY(!checkTagsListEqual(m_firstNoteTagsSet, newTagLocalUids)))
        {
            ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                        "set of new tag local uids "
                                                        "in note tags list updated "
                                                        "signal"));
            errorDescription.details() = newTagLocalUids.join(QStringLiteral(","));
            QNWARNING(errorDescription);
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
    else
    {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected "
                                                    "note tags list update event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
        Q_EMIT failure(errorDescription.nonLocalizedString());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::createConnections()
{
    QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,finished),
                     m_pLocalStorageManagerThread, QNSLOT(QThread,deleteLater));

    QObject::connect(this,
                     QNSIGNAL(NoteNotebookAndTagListTrackingAsyncTester,
                              addNotebook,Notebook,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onAddNotebookRequest,
                            Notebook,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NoteNotebookAndTagListTrackingAsyncTester,
                              addTag,Tag,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onAddTagRequest,Tag,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NoteNotebookAndTagListTrackingAsyncTester,
                              addNote,Note,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::connect(this,
                     QNSIGNAL(NoteNotebookAndTagListTrackingAsyncTester,updateNote,
                              Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     m_pLocalStorageManagerAsync,
                     QNSLOT(LocalStorageManagerAsync,onUpdateNoteRequest,
                            Note,LocalStorageManager::UpdateNoteOptions,QUuid));

    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,initialized),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            initialize));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,
                              Notebook,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onAddNotebookComplete,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,
                              Notebook,ErrorString,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onAddNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addTagComplete,Tag,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onAddTagComplete,Tag,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addTagFailed,
                              Tag,ErrorString,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onAddTagFailed,Tag,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onAddNoteComplete,Note,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,
                              Note,ErrorString,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onAddNoteFailed,Note,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNoteComplete,
                              Note,LocalStorageManager::UpdateNoteOptions,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onUpdateNoteComplete,Note,
                            LocalStorageManager::UpdateNoteOptions,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,updateNoteFailed,
                              Note,LocalStorageManager::UpdateNoteOptions,
                              ErrorString,QUuid),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onUpdateNoteFailed,Note,
                            LocalStorageManager::UpdateNoteOptions,
                            ErrorString,QUuid));

    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,noteMovedToAnotherNotebook,
                              QString,QString,QString),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onNoteMovedToAnotherNotebook,QString,QString,QString));
    QObject::connect(m_pLocalStorageManagerAsync,
                     QNSIGNAL(LocalStorageManagerAsync,noteTagListChanged,
                              QString,QStringList,QStringList),
                     this,
                     QNSLOT(NoteNotebookAndTagListTrackingAsyncTester,
                            onNoteTagListUpdated,QString,QStringList,QStringList));
}

void NoteNotebookAndTagListTrackingAsyncTester::clear()
{
    if (m_pLocalStorageManagerThread) {
        m_pLocalStorageManagerThread->quit();
        m_pLocalStorageManagerThread->wait();
        m_pLocalStorageManagerThread->deleteLater();
        m_pLocalStorageManagerThread = Q_NULLPTR;
    }

    if (m_pLocalStorageManagerAsync) {
        m_pLocalStorageManagerAsync->deleteLater();
        m_pLocalStorageManagerAsync = Q_NULLPTR;
    }

    m_state = STATE_UNINITIALIZED;
}

void NoteNotebookAndTagListTrackingAsyncTester::createNoteInLocalStorage()
{
    Note note;
    note.setTitle(QStringLiteral("My grand note"));
    note.setContent(QStringLiteral("<en-note><h1>Hello world!</h1></en-note>"));
    note.setNotebookLocalUid(m_firstNotebook.localUid());

    for(auto it = m_firstNoteTagsSet.constBegin(),
        end = m_firstNoteTagsSet.constEnd(); it != end; ++it)
    {
        note.addTagLocalUid(it->localUid());
    }

    m_note = note;
    m_state = STATE_PENDING_NOTE_CREATION;
    Q_EMIT addNote(note, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::moveNoteToAnotherNotebook()
{
    Note modifiedNote = m_note;
    modifiedNote.setTitle(m_note.title() + QStringLiteral("3"));
    modifiedNote.setNotebookLocalUid(m_secondNotebook.localUid());

    m_state = STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

    LocalStorageManager::UpdateNoteOptions options(0);
    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::changeNoteTagsList()
{
    Note modifiedNote = m_note;
    modifiedNote.setTitle(m_note.title() + QStringLiteral("4"));

    QStringList tagLocalUids;

    tagLocalUids.reserve(m_secondNoteTagsSet.size());
    for(auto it = m_secondNoteTagsSet.constBegin(),
        end = m_secondNoteTagsSet.constEnd(); it != end; ++it)
    {
        tagLocalUids << it->localUid();
    }

    modifiedNote.setTagLocalUids(tagLocalUids);

    m_state = STATE_PENDING_NOTE_UPDATE_WITH_TAG_LIST_CHANGE_ONLY;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateTags);
    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

void NoteNotebookAndTagListTrackingAsyncTester::moveNoteToAnotherNotebookAlongWithTagListChange()
{
    Note modifiedNote = m_note;
    modifiedNote.setTitle(m_note.title() + QStringLiteral("5"));
    modifiedNote.setNotebookLocalUid(m_firstNotebook.localUid());

    QStringList tagLocalUids;

    tagLocalUids.reserve(m_firstNoteTagsSet.size());
    for(auto it = m_firstNoteTagsSet.constBegin(),
        end = m_firstNoteTagsSet.constEnd(); it != end; ++it)
    {
        tagLocalUids << it->localUid();
    }

    modifiedNote.setTagLocalUids(tagLocalUids);

    m_state = STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES;
    m_receivedUpdateNoteCompleteSignal = false;
    m_receivedNoteTagsListChangedSignal = false;
    m_receivedNoteMovedToAnotherNotebookSignal = false;

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateTags);
    Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
}

bool NoteNotebookAndTagListTrackingAsyncTester::checkTagsListEqual(
    const QVector<Tag> & lhs, const QStringList & rhs) const
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for(auto it = lhs.constBegin(), end = lhs.constEnd(); it != end; ++it)
    {
        int index = rhs.indexOf(it->localUid());
        if (index < 0) {
            return false;
        }
    }

    return true;
}

} // namespace test
} // namespace quentier
