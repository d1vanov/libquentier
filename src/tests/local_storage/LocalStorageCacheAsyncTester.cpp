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

#include "LocalStorageCacheAsyncTester.h"

#include <quentier/local_storage/DefaultLocalStorageCacheExpiryChecker.h>
#include <quentier/local_storage/LocalStorageCacheManager.h>
#include <quentier/logging/QuentierLogger.h>

#include "../../local_storage/DefaultLocalStorageCacheExpiryCheckerConfig.h"

#include <QDebug>
#include <QThread>

namespace quentier {
namespace test {

LocalStorageCacheAsyncTester::LocalStorageCacheAsyncTester(QObject * parent) :
    QObject(parent)
{}

LocalStorageCacheAsyncTester::~LocalStorageCacheAsyncTester()
{
    clear();
}

void LocalStorageCacheAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("LocalStorageCacheAsyncTester");
    qint32 userId = 12;

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

    m_pLocalStorageManagerThread->setObjectName(
        QStringLiteral("LocalStorageCacheAsyncTester-local-storage-thread"));

    m_pLocalStorageManagerThread->start();
}

void LocalStorageCacheAsyncTester::initialize()
{
    m_pLocalStorageCacheManager =
        m_pLocalStorageManagerAsync->localStorageCacheManager();

    if (!m_pLocalStorageCacheManager) {
        QString error = QStringLiteral(
            "Local storage cache is not enabled by default for unknown reason");

        QNWARNING("tests:local_storage", error);
        Q_EMIT failure(error);
        return;
    }

    addNotebook();
}

void LocalStorageCacheAsyncTester::onAddNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else {                                                                     \
        errorDescription.setBase(                                              \
            "Internal error in LocalStorageCacheAsyncTester: wrong state");    \
        QNWARNING("tests:local_storage", errorDescription << ": " << m_state); \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
    }

    if (m_state == State::STATE_SENT_NOTEBOOK_ADD_REQUEST) {
        if (m_currentNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: notebook in "
                "onAddNotebookCompleted doesn't match the original notebook");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original notebook: " << m_currentNotebook
                                 << "\nFound notebook: " << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        ++m_addedNotebooksCount;

        if (m_addedNotebooksCount == 1) {
            m_firstNotebook = m_currentNotebook;
        }
        else if (m_addedNotebooksCount == 2) {
            m_secondNotebook = m_currentNotebook;
        }

        if (m_addedNotebooksCount > MAX_NOTEBOOKS_TO_STORE) {
            const Notebook * pNotebook =
                m_pLocalStorageCacheManager->findNotebook(
                    m_firstNotebook.localUid(),
                    LocalStorageCacheManager::LocalUid);

            if (pNotebook) {
                errorDescription.setBase(
                    "Found notebook which should not have "
                    "been present in the local storage cache");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ": " << pNotebook->toString());

                Q_EMIT failure(errorDescription.nonLocalizedString());
            }
            else {
                updateNotebook();
            }

            return;
        }
        else if (m_addedNotebooksCount > 1) {
            const Notebook * pNotebook =
                m_pLocalStorageCacheManager->findNotebook(
                    m_firstNotebook.localUid(),
                    LocalStorageCacheManager::LocalUid);

            if (!pNotebook) {
                errorDescription.setBase(
                    "Notebook which should have been present in the local "
                    "storage cache was not found there");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ", first notebook: "
                                     << m_firstNotebook);

                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }
        }

        addNotebook();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onAddNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onUpdateNotebookCompleted(
    Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_NOTEBOOK_UPDATE_REQUEST) {
        if (m_secondNotebook != notebook) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: notebook in "
                "onUpdateNotebookCompleted doesn't match the original "
                "notebook");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original notebook: " << m_secondNotebook
                                 << "\nFound notebook: " << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        const Notebook * pNotebook = m_pLocalStorageCacheManager->findNotebook(
            notebook.localUid(), LocalStorageCacheManager::LocalUid);

        if (!pNotebook) {
            errorDescription.setBase(
                "Updated notebook which should have been present in the local "
                "storage cache was not found there");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", notebook: " << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
        else if (*pNotebook != notebook) {
            errorDescription.setBase(
                "Updated notebook does not match the notebook stored in "
                "the local storage cache");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", notebook: " << notebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, updated notebook was cached correctly, moving to testing notes
        addNote();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onUpdateNotebookFailed(
    Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onAddNoteCompleted(
    Note note, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_NOTE_ADD_REQUEST) {
        if (m_currentNote != note) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: "
                "note in onAddNoteCompleted doesn't match the original note");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original note: " << m_currentNote
                                 << "\nFound note: " << note);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (m_secondNotebook.localUid() != note.notebookLocalUid()) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: notebook in "
                "onAddNoteCompleted doesn't match the original notebook");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original notebook: " << m_secondNotebook
                                 << "\nFound note's local notebook uid: "
                                 << note.notebookLocalUid());

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        ++m_addedNotesCount;

        if (m_addedNotesCount == 1) {
            m_firstNote = m_currentNote;
        }
        else if (m_addedNotesCount == 2) {
            m_secondNote = m_currentNote;
        }

        if (m_addedNotesCount > MAX_NOTES_TO_STORE) {
            const Note * pNote = m_pLocalStorageCacheManager->findNote(
                m_firstNote.localUid(), LocalStorageCacheManager::LocalUid);

            if (pNote) {
                errorDescription.setBase(
                    "Found note which should not have been "
                    "present in the local storage cache: ");

                errorDescription.details() = pNote->toString();
                QNWARNING("tests:local_storage", errorDescription);
                Q_EMIT failure(errorDescription.nonLocalizedString());
            }
            else {
                updateNote();
            }

            return;
        }
        else if (m_addedNotesCount > 1) {
            const Note * pNote = m_pLocalStorageCacheManager->findNote(
                m_firstNote.localUid(), LocalStorageCacheManager::LocalUid);

            if (!pNote) {
                errorDescription.setBase(
                    "Note which should have been present in the local storage "
                    "cache was not found there");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ", first note: " << m_firstNote);

                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }
        }

        addNote();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onAddNoteFailed(
    Note note, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", note: " << note);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onUpdateNoteCompleted(
    Note note, LocalStorageManager::UpdateNoteOptions options, QUuid requestId)
{
    Q_UNUSED(options)
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_NOTE_UPDATE_REQUEST) {
        if (m_secondNote != note) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: note in "
                "onUpdateNoteCompleted doesn't match the original note");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original note: " << m_secondNote
                                 << "\nFound note: " << note);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        if (m_secondNotebook.localUid() != note.notebookLocalUid()) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: "
                "note's notebook local uid in onUpdateNoteCompleted doesn't "
                "match the original notebook local uid");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original notebook: " << m_secondNotebook
                                 << "\nUpdated note's notebook local uid: "
                                 << note.notebookLocalUid());

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        const Note * pNote = m_pLocalStorageCacheManager->findNote(
            note.localUid(), LocalStorageCacheManager::LocalUid);

        if (!pNote) {
            errorDescription.setBase(
                "Updated note which should have been "
                "present in the local storage cache was "
                "not found there");

            QNWARNING(
                "tests:local_storage", errorDescription << ", note: " << note);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
        else if (*pNote != note) {
            errorDescription.setBase(
                "Updated note does not match the note "
                "stored in the local storage cache");

            QNWARNING(
                "tests:local_storage", errorDescription << ", note: " << note);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, updated note was cached correctly, moving to testing tags
        addTag();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onUpdateNoteFailed(
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

void LocalStorageCacheAsyncTester::onAddTagCompleted(Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_TAG_ADD_REQUEST) {
        if (m_currentTag != tag) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: "
                "tag in onAddTagCompleted doesn't match the original tag");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original tag: " << m_currentTag
                                 << "\nFound tag: " << tag);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        ++m_addedTagsCount;

        if (m_addedTagsCount == 1) {
            m_firstTag = m_currentTag;
        }
        else if (m_addedTagsCount == 2) {
            m_secondTag = m_currentTag;
        }

        if (m_addedTagsCount > MAX_TAGS_TO_STORE) {
            const Tag * pTag = m_pLocalStorageCacheManager->findTag(
                m_firstTag.localUid(), LocalStorageCacheManager::LocalUid);

            if (pTag) {
                errorDescription.setBase(
                    "Found tag which should not have been "
                    "present in the local storage cache");

                QNWARNING(
                    "tests:local_storage", errorDescription << ": " << *pTag);

                Q_EMIT failure(errorDescription.nonLocalizedString());
            }
            else {
                updateTag();
            }

            return;
        }
        else if (m_addedTagsCount > 1) {
            const Tag * pTag = m_pLocalStorageCacheManager->findTag(
                m_firstTag.localUid(), LocalStorageCacheManager::LocalUid);

            if (!pTag) {
                errorDescription.setBase(
                    "Tag which should have been present in the local storage "
                    "cache was not found there");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ", first tag: " << m_firstTag);

                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }

            // Check that we can also find the tag by name in the cache
            pTag =
                m_pLocalStorageCacheManager->findTagByName(m_firstTag.name());
            if (!pTag) {
                errorDescription.setBase(
                    "Tag present in the local storage cache could not be "
                    "found by tag name");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ", first tag: " << m_firstTag);

                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }
        }

        addTag();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onAddTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", request id = " << requestId << ", tag: " << tag);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onUpdateTagCompleted(
    Tag tag, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_TAG_UPDATE_REQUEST) {
        if (m_secondTag != tag) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: tag in "
                "onUpdateTagCompleted doesn't match the original tag");

            QNWARNING(
                "tests:local_storage",
                errorDescription << "; original tag: " << m_secondTag
                                 << "\nFound tag: " << tag);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        const Tag * pTag = m_pLocalStorageCacheManager->findTag(
            tag.localUid(), LocalStorageCacheManager::LocalUid);

        if (!pTag) {
            errorDescription.setBase(
                "Updated tag which should have been present in the local "
                "storage cache was not found there");

            QNWARNING(
                "tests:local_storage", errorDescription << ", tag: " << tag);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
        else if (*pTag != tag) {
            errorDescription.setBase(
                "Updated tag does not match the tag "
                "stored in the local storage cache");

            QNWARNING(
                "tests:local_storage", errorDescription << ", tag: " << tag);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, updated tag was cached correctly, moving to testing linked
        // notebooks
        addLinkedNotebook();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onUpdateTagFailed(
    Tag tag, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", request id = " << requestId << ", tag: " << tag);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onAddLinkedNotebookCompleted(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_LINKED_NOTEBOOK_ADD_REQUEST) {
        if (m_currentLinkedNotebook != linkedNotebook) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: linked "
                "notebook in onAddLinkedNotebookCompleted doesn't match "
                "the original linked notebook");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << "; original linked notebook: " << m_currentLinkedNotebook
                    << "\nFound linked notebook: " << linkedNotebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        ++m_addedLinkedNotebooksCount;

        if (m_addedLinkedNotebooksCount == 1) {
            m_firstLinkedNotebook = m_currentLinkedNotebook;
        }
        else if (m_addedLinkedNotebooksCount == 2) {
            m_secondLinkedNotebook = m_currentLinkedNotebook;
        }

        if (m_addedLinkedNotebooksCount > MAX_LINKED_NOTEBOOKS_TO_STORE) {
            const LinkedNotebook * pLinkedNotebook =
                m_pLocalStorageCacheManager->findLinkedNotebook(
                    m_firstLinkedNotebook.guid());

            if (pLinkedNotebook) {
                errorDescription.setBase(
                    "Found linked notebook which should not have been present "
                    "in the local storage cache");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ": " << *pLinkedNotebook);

                Q_EMIT failure(errorDescription.nonLocalizedString());
            }
            else {
                updateLinkedNotebook();
            }

            return;
        }
        else if (m_addedLinkedNotebooksCount > 1) {
            const auto * pLinkedNotebook =
                m_pLocalStorageCacheManager->findLinkedNotebook(
                    m_firstLinkedNotebook.guid());

            if (!pLinkedNotebook) {
                errorDescription.setBase(
                    "Linked notebook which should have been present in "
                    "the local storage cache was not found there");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ", first linked notebook: "
                                     << m_firstLinkedNotebook);

                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }
        }

        addLinkedNotebook();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onAddLinkedNotebookFailed(
    LinkedNotebook linkedNotebook, ErrorString errorDescription,
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", linked notebook: " << linkedNotebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onUpdateLinkedNotebookCompleted(
    LinkedNotebook linkedNotebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_LINKED_NOTEBOOK_UPDATE_REQUEST) {
        if (m_secondLinkedNotebook != linkedNotebook) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: linked "
                "notebook in onUpdateLinkedNotebookCompleted doesn't match "
                "the original linked notebook");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << "; original linked notebook: " << m_secondLinkedNotebook
                    << "\nFound linked notebook: " << linkedNotebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        const LinkedNotebook * pLinkedNotebook =
            m_pLocalStorageCacheManager->findLinkedNotebook(
                linkedNotebook.guid());

        if (!pLinkedNotebook) {
            errorDescription.setBase(
                "Updated linked notebook which should have been present in "
                "the local storage cache was not found there");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", linked notebook: " << linkedNotebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
        else if (*pLinkedNotebook != linkedNotebook) {
            errorDescription.setBase(
                "Updated linked notebook does not match the linked notebook "
                "stored in the local storage cache");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", linked notebook: " << linkedNotebook);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, updated linked notebook was cached correctly, moving to testing
        // saved searches
        addSavedSearch();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onUpdateLinkedNotebookFailed(
    LinkedNotebook linkedNotebook, ErrorString errorDescription,
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", linked notebook: " << linkedNotebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onAddSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_SAVED_SEARCH_ADD_REQUEST) {
        if (m_currentSavedSearch != search) {
            errorDescription.setBase(
                "Internal error in LocalStorageCacheAsyncTester: saved search "
                "in onAddSavedSearchCompleted doesn't match the original saved "
                "search");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << "; original saved search: " << m_currentSavedSearch
                    << "\nFound saved search: " << search);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        ++m_addedSavedSearchesCount;

        if (m_addedSavedSearchesCount == 1) {
            m_firstSavedSearch = m_currentSavedSearch;
        }
        else if (m_addedSavedSearchesCount == 2) {
            m_secondSavedSearch = m_currentSavedSearch;
        }

        if (m_addedSavedSearchesCount > MAX_SAVED_SEARCHES_TO_STORE) {
            const auto * pSavedSearch =
                m_pLocalStorageCacheManager->findSavedSearch(
                    m_firstSavedSearch.localUid(),
                    LocalStorageCacheManager::LocalUid);

            if (pSavedSearch) {
                errorDescription.setBase(
                    "Found saved search which should not have been present "
                    "in the local storage cache");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ": " << *pSavedSearch);

                Q_EMIT failure(errorDescription.nonLocalizedString());
            }
            else {
                updateSavedSearch();
            }

            return;
        }
        else if (m_addedSavedSearchesCount > 1) {
            const auto * pSavedSearch =
                m_pLocalStorageCacheManager->findSavedSearch(
                    m_firstSavedSearch.localUid(),
                    LocalStorageCacheManager::LocalUid);

            if (!pSavedSearch) {
                errorDescription.setBase(
                    "Saved search which should have been present in the local "
                    "storage cache was not found there");

                QNWARNING(
                    "tests:local_storage",
                    errorDescription << ", first saved search: "
                                     << m_firstSavedSearch);

                Q_EMIT failure(errorDescription.nonLocalizedString());
                return;
            }
        }

        addSavedSearch();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onAddSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::onUpdateSavedSearchCompleted(
    SavedSearch search, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == State::STATE_SENT_SAVED_SEARCH_UPDATE_REQUEST) {
        if (m_secondSavedSearch != search) {
            errorDescription.setBase(
                "Internal error in LocalStorageCachesyncTester: saved search "
                "in onUpdateSavedSearchCompleted doesn't match the original "
                "saved search");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << "; original saved search: " << m_secondSavedSearch
                    << "\nFound saved search: " << search);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        const SavedSearch * pSavedSearch =
            m_pLocalStorageCacheManager->findSavedSearch(
                search.localUid(), LocalStorageCacheManager::LocalUid);

        if (!pSavedSearch) {
            errorDescription.setBase(
                "Updated saved search which should have been present in "
                "the local storage cache was not found there");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", saved search: " << search);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }
        else if (*pSavedSearch != search) {
            errorDescription.setBase(
                "Updated saved search does not match the saved search in "
                "the local storage cache");

            QNWARNING(
                "tests:local_storage",
                errorDescription << ", saved search: " << search);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, updated saved search was cached correctly, can finally return
        // successfully
        Q_EMIT success();
    }
    HANDLE_WRONG_STATE()
}

void LocalStorageCacheAsyncTester::onUpdateSavedSearchFailed(
    SavedSearch search, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", saved search: " << search);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void LocalStorageCacheAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &LocalStorageManagerAsync::deleteLater);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &LocalStorageCacheAsyncTester::initialize);

    // Request --> slot connections
    QObject::connect(
        this, &LocalStorageCacheAsyncTester::addNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::updateNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNotebookRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::addNoteRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNoteRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::updateNoteRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateNoteRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::addTagRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddTagRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::updateTagRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateTagRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::addLinkedNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddLinkedNotebookRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::updateLinkedNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateLinkedNotebookRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::addSavedSearchRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddSavedSearchRequest);

    QObject::connect(
        this, &LocalStorageCacheAsyncTester::updateSavedSearchRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateSavedSearchRequest);

    // Slot <-- result connections
    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &LocalStorageCacheAsyncTester::onAddNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookFailed, this,
        &LocalStorageCacheAsyncTester::onAddNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookComplete, this,
        &LocalStorageCacheAsyncTester::onUpdateNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNotebookFailed, this,
        &LocalStorageCacheAsyncTester::onUpdateNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &LocalStorageCacheAsyncTester::onAddNoteCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteFailed,
        this, &LocalStorageCacheAsyncTester::onAddNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteComplete, this,
        &LocalStorageCacheAsyncTester::onUpdateNoteCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateNoteFailed, this,
        &LocalStorageCacheAsyncTester::onUpdateNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addTagComplete,
        this, &LocalStorageCacheAsyncTester::onAddTagCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addTagFailed,
        this, &LocalStorageCacheAsyncTester::onAddTagFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateTagComplete, this,
        &LocalStorageCacheAsyncTester::onUpdateTagCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::updateTagFailed,
        this, &LocalStorageCacheAsyncTester::onUpdateTagFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookComplete, this,
        &LocalStorageCacheAsyncTester::onAddLinkedNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addLinkedNotebookFailed, this,
        &LocalStorageCacheAsyncTester::onAddLinkedNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookComplete, this,
        &LocalStorageCacheAsyncTester::onUpdateLinkedNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateLinkedNotebookFailed, this,
        &LocalStorageCacheAsyncTester::onUpdateLinkedNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchComplete, this,
        &LocalStorageCacheAsyncTester::onAddSavedSearchCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addSavedSearchFailed, this,
        &LocalStorageCacheAsyncTester::onAddSavedSearchFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchComplete, this,
        &LocalStorageCacheAsyncTester::onUpdateSavedSearchCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateSavedSearchFailed, this,
        &LocalStorageCacheAsyncTester::onUpdateSavedSearchFailed);
}

void LocalStorageCacheAsyncTester::clear()
{
    if (m_pLocalStorageManagerThread) {
        m_pLocalStorageManagerThread->quit();
        m_pLocalStorageManagerThread->wait();
        m_pLocalStorageManagerThread->deleteLater();
    }

    if (m_pLocalStorageManagerAsync) {
        m_pLocalStorageManagerAsync->deleteLater();
        m_pLocalStorageManagerAsync = nullptr;
    }

    m_state = State::STATE_UNINITIALIZED;
}

void LocalStorageCacheAsyncTester::addNotebook()
{
    m_currentNotebook = Notebook();

    m_currentNotebook.setUpdateSequenceNumber(
        static_cast<qint32>(m_addedNotebooksCount + 1));

    m_currentNotebook.setName(
        QStringLiteral("Fake notebook #") +
        QString::number(m_addedNotebooksCount + 1));

    m_currentNotebook.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());

    m_currentNotebook.setModificationTimestamp(
        QDateTime::currentMSecsSinceEpoch());

    m_currentNotebook.setDefaultNotebook(
        m_addedNotebooksCount == 0 ? true : false);

    m_currentNotebook.setLastUsed(false);

    m_state = State::STATE_SENT_NOTEBOOK_ADD_REQUEST;
    Q_EMIT addNotebookRequest(m_currentNotebook, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::updateNotebook()
{
    m_secondNotebook.setUpdateSequenceNumber(
        m_secondNotebook.updateSequenceNumber() + 1);

    m_secondNotebook.setName(
        m_secondNotebook.name() + QStringLiteral("_modified"));

    m_secondNotebook.setModificationTimestamp(
        QDateTime::currentMSecsSinceEpoch());

    m_state = State::STATE_SENT_NOTEBOOK_UPDATE_REQUEST;
    Q_EMIT updateNotebookRequest(m_secondNotebook, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::addNote()
{
    m_currentNote = Note();

    m_currentNote.setUpdateSequenceNumber(
        static_cast<qint32>(m_addedNotesCount + 1));

    m_currentNote.setTitle(
        QStringLiteral("Fake note #") + QString::number(m_addedNotesCount + 1));

    m_currentNote.setCreationTimestamp(QDateTime::currentMSecsSinceEpoch());
    m_currentNote.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());
    m_currentNote.setActive(true);

    m_currentNote.setContent(
        QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

    m_currentNote.setNotebookLocalUid(m_secondNotebook.localUid());

    m_state = State::STATE_SENT_NOTE_ADD_REQUEST;
    Q_EMIT addNoteRequest(m_currentNote, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::updateNote()
{
    m_secondNote.setUpdateSequenceNumber(
        m_secondNote.updateSequenceNumber() + 1);

    m_secondNote.setTitle(m_secondNote.title() + QStringLiteral("_modified"));
    m_secondNote.setModificationTimestamp(QDateTime::currentMSecsSinceEpoch());

    m_state = State::STATE_SENT_NOTE_UPDATE_REQUEST;

    LocalStorageManager::UpdateNoteOptions options(
        LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata |
        LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData |
        LocalStorageManager::UpdateNoteOption::UpdateTags);

    Q_EMIT updateNoteRequest(m_secondNote, options, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::addTag()
{
    m_currentTag = Tag();

    m_currentTag.setUpdateSequenceNumber(
        static_cast<qint32>(m_addedTagsCount + 1));

    m_currentTag.setName(
        QStringLiteral("Fake tag #") + QString::number(m_addedTagsCount + 1));

    m_state = State::STATE_SENT_TAG_ADD_REQUEST;
    Q_EMIT addTagRequest(m_currentTag, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::updateTag()
{
    m_secondTag.setUpdateSequenceNumber(m_secondTag.updateSequenceNumber() + 1);
    m_secondTag.setName(m_secondTag.name() + QStringLiteral("_modified"));

    m_state = State::STATE_SENT_TAG_UPDATE_REQUEST;
    Q_EMIT updateTagRequest(m_secondTag, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::addLinkedNotebook()
{
    m_currentLinkedNotebook = LinkedNotebook();

    QString guid = QStringLiteral("00000000-0000-0000-c000-0000000000");
    if (m_addedLinkedNotebooksCount < 9) {
        guid += QStringLiteral("0");
    }
    guid += QString::number(m_addedLinkedNotebooksCount + 1);

    m_currentLinkedNotebook.setGuid(guid);
    m_currentLinkedNotebook.setShareName(
        QStringLiteral("Fake linked notebook share name"));

    m_state = State::STATE_SENT_LINKED_NOTEBOOK_ADD_REQUEST;

    Q_EMIT addLinkedNotebookRequest(
        m_currentLinkedNotebook, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::updateLinkedNotebook()
{
    m_secondLinkedNotebook.setShareName(
        m_secondLinkedNotebook.shareName() + QStringLiteral("_modified"));

    m_state = State::STATE_SENT_LINKED_NOTEBOOK_UPDATE_REQUEST;

    Q_EMIT updateLinkedNotebookRequest(
        m_secondLinkedNotebook, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::addSavedSearch()
{
    m_currentSavedSearch = SavedSearch();

    m_currentSavedSearch.setName(
        QStringLiteral("Saved search #") +
        QString::number(m_addedSavedSearchesCount + 1));

    m_currentSavedSearch.setQuery(
        QStringLiteral("Fake saved search query #") +
        QString::number(m_addedSavedSearchesCount + 1));

    m_currentSavedSearch.setUpdateSequenceNumber(
        static_cast<qint32>(m_addedSavedSearchesCount + 1));

    m_currentSavedSearch.setQueryFormat(1);
    m_currentSavedSearch.setIncludeAccount(true);

    m_state = State::STATE_SENT_SAVED_SEARCH_ADD_REQUEST;
    Q_EMIT addSavedSearchRequest(m_currentSavedSearch, QUuid::createUuid());
}

void LocalStorageCacheAsyncTester::updateSavedSearch()
{
    m_secondSavedSearch.setName(
        m_secondSavedSearch.name() + QStringLiteral("_modified"));

    m_state = State::STATE_SENT_SAVED_SEARCH_UPDATE_REQUEST;
    Q_EMIT updateSavedSearchRequest(m_secondSavedSearch, QUuid::createUuid());
}

QDebug & operator<<(
    QDebug & dbg, const LocalStorageCacheAsyncTester::State state)
{
    using State = LocalStorageCacheAsyncTester::State;

    switch (state) {
    case State::STATE_UNINITIALIZED:
        dbg << "Uninitialized";
        break;
    case State::STATE_SENT_NOTEBOOK_ADD_REQUEST:
        dbg << "Sent add notebook request";
        break;
    case State::STATE_SENT_NOTEBOOK_UPDATE_REQUEST:
        dbg << "Sent update notebook request";
        break;
    case State::STATE_SENT_NOTE_ADD_REQUEST:
        dbg << "Sent add note request";
        break;
    case State::STATE_SENT_NOTE_UPDATE_REQUEST:
        dbg << "Sent update note request";
        break;
    case State::STATE_SENT_TAG_ADD_REQUEST:
        dbg << "Sent add tag request";
        break;
    case State::STATE_SENT_TAG_UPDATE_REQUEST:
        dbg << "Sent update tag request";
        break;
    case State::STATE_SENT_LINKED_NOTEBOOK_ADD_REQUEST:
        dbg << "Sent add linked notebook request";
        break;
    case State::STATE_SENT_LINKED_NOTEBOOK_UPDATE_REQUEST:
        dbg << "Sent update linked notebook request";
        break;
    case State::STATE_SENT_SAVED_SEARCH_ADD_REQUEST:
        dbg << "Sent add saved search request";
        break;
    case State::STATE_SENT_SAVED_SEARCH_UPDATE_REQUEST:
        dbg << "Sent update saved search request";
        break;
    default:
        dbg << "Unknown (" << static_cast<qint64>(state) << ")";
        break;
    }

    return dbg;
}

} // namespace test
} // namespace quentier
