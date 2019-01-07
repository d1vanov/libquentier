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
    m_receivedNoteTagsListChangedSignal(false)
{}

NoteNotebookAndTagListTrackingAsyncTester::~NoteNotebookAndTagListTrackingAsyncTester()
{
    clear();
}

void NoteNotebookAndTagListTrackingAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester");
    qint32 userId = 7;
    bool startFromScratch = true;
    bool overrideLock = false;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);
    m_pLocalStorageManagerAsync = new LocalStorageManagerAsync(account, startFromScratch, overrideLock);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester-local-storage-thread"));
    m_pLocalStorageManagerThread->start();
}

void NoteNotebookAndTagListTrackingAsyncTester::onLocalStorageManagerInitialized()
{
    m_firstNotebook.setGuid(QUuid::createUuid().toString());
    m_firstNotebook.setUpdateSequenceNumber(1);
    m_firstNotebook.setName(QStringLiteral("Previous"));
    m_firstNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    m_firstNotebook.setModificationTimestamp(m_firstNotebook.creationTimestamp());
    m_firstNotebook.setDefaultNotebook(true);
    m_firstNotebook.setLastUsed(true);

    m_secondNotebook.setGuid(QUuid::createUuid().toString());
    m_secondNotebook.setUpdateSequenceNumber(2);
    m_secondNotebook.setName(QStringLiteral("New"));
    m_secondNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    m_secondNotebook.setModificationTimestamp(m_secondNotebook.creationTimestamp());

    int numTags = 3;
    m_firstNoteTagsSet.reserve(numTags);
    for(int i = 0; i < numTags; ++i) {
        Tag tag;
        tag.setGuid(QUuid::createUuid().toString());
        tag.setName(QStringLiteral("Previous ") + QString::number(i+1));
        m_firstNoteTagsSet << tag;
    }

    m_secondNoteTagsSet.reserve(numTags);
    for(int i = 0; i < numTags; ++i) {
        Tag tag;
        tag.setGuid(QUuid::createUuid().toString());
        tag.setName(QStringLiteral("New ") + QString::number(i+1));
    }

    m_state = STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION;

    Q_EMIT addNotebook(m_firstNotebook, QUuid::createUuid());
    Q_EMIT addNotebook(m_secondNotebook, QUuid::createUuid());

    for(int i = 0; i < numTags; ++i) {
        Q_EMIT addTag(m_firstNoteTagsSet[i], QUuid::createUuid());
        Q_EMIT addTag(m_secondNoteTagsSet[i], QUuid::createUuid());
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookComplete(Notebook notebook, QUuid requestId)
{
    Q_UNUSED(notebook)
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION)) {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected add notebook complete event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    ++m_addedNotebooksCount;
    if (m_addedNotebooksCount != 2) {
        return;
    }

    if (m_addedTagsCount == (m_firstNoteTagsSet.size() + m_secondNoteTagsSet.size())) {
        createNoteInLocalStorage();
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookFailed(Notebook notebook, ErrorString errorDescription,
                                                                    QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onAddNotebookFailed: ") << errorDescription
              << QStringLiteral(", notebook: ") << notebook);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddTagComplete(Tag tag, QUuid requestId)
{
    Q_UNUSED(tag)
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTEBOOKS_AND_TAGS_CREATION)) {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected add tag complete event"));
        QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    ++m_addedTagsCount;

    if (m_addedTagsCount != (m_firstNoteTagsSet.size() + m_secondNoteTagsSet.size())) {
        return;
    }

    if (m_addedNotebooksCount == 2) {
        createNoteInLocalStorage();
    }
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddTagFailed(Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onAddTagFailed: ") << errorDescription
              << QStringLiteral(", tag: ") << tag);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onAddNoteComplete(Note note, QUuid requestId)
{
    Q_UNUSED(requestId)

    if (Q_UNLIKELY(m_state != STATE_PENDING_NOTE_CREATION)) {
        ErrorString errorDescription(QStringLiteral("Internal error: unexpected add note complete event"));
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

void NoteNotebookAndTagListTrackingAsyncTester::onAddNoteFailed(Note note, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onAddNoteFailed: ") << errorDescription
              << QStringLiteral(", note: ") << note);

    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteComplete(Note note, LocalStorageManager::UpdateNoteOptions options,
                                                                     QUuid requestId)
{
    Q_UNUSED(options)
    Q_UNUSED(note)
    Q_UNUSED(requestId)

    if (m_state == STATE_PENDING_NOTE_UPDATE_WITHOUT_NOTEBOOK_OR_TAG_LIST_CHANGE)
    {
        Note modifiedNote = m_note;
        modifiedNote.setTitle(m_note.title() + QStringLiteral("3"));
        modifiedNote.setNotebookLocalUid(m_secondNotebook.localUid());
        modifiedNote.setNotebookGuid(m_secondNotebook.guid());

        m_state = STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY;
        m_receivedUpdateNoteCompleteSignal = false;
        m_receivedNoteTagsListChangedSignal = false;
        m_receivedNoteMovedToAnotherNotebookSignal = false;

        LocalStorageManager::UpdateNoteOptions options(0);
        Q_EMIT updateNote(modifiedNote, options, QUuid::createUuid());
    }
    // TODO: consider other relevant states here
}

void NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteFailed(Note note, LocalStorageManager::UpdateNoteOptions options,
                                                                   ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(QStringLiteral("NoteNotebookAndTagListTrackingAsyncTester::onUpdateNoteFailed: ") << errorDescription
              << QStringLiteral(", note: ") << note);

    Q_UNUSED(options)
    Q_UNUSED(requestId)
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void NoteNotebookAndTagListTrackingAsyncTester::onNoteMovedToAnotherNotebook(QString noteLocalUid,
                                                                             QString previousNotebookLocalUid,
                                                                             QString newNotebookLocalUid)
{
    if ( (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_CHANGE_ONLY) ||
         (m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES) )
    {
        m_receivedNoteMovedToAnotherNotebookSignal = true;

        if (m_receivedUpdateNoteCompleteSignal)
        {
            if ((m_state == STATE_PENDING_NOTE_UPDATE_WITH_NOTEBOOK_AND_TAG_LIST_CHANGES) &&
                m_receivedNoteTagsListChangedSignal)
            {
                Q_EMIT success();
                return;
            }

            // TODO: next thing to do
            return;
        }

        return;
    }

    ErrorString errorDescription(QStringLiteral("Internal error: unexpected note moved to another notebook event"));
    QNWARNING(errorDescription << QStringLiteral(", state = ") << m_state);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

} // namespace test
} // namespace quentier
