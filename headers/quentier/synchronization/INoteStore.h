/*
 * Copyright 2018 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_I_NOTE_STORE_H
#define LIB_QUENTIER_SYNCHRONIZATION_I_NOTE_STORE_H

#include <quentier/types/Notebook.h>
#include <quentier/types/Note.h>
#include <quentier/types/Tag.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/ErrorString.h>
#include <quentier/utility/Linkage.h>
#include <quentier/utility/Macros.h>
#include <QObject>
#include <QSharedPointer>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

namespace quentier {

/**
 * @brief INoteStore is the interface for NoteStore used by
 * SynchronizationManager: it provides signatures of methods
 * required for the implementation of Evernote EDAM sync protocol
 *
 * By default SynchronizationManager within libquentier uses its own
 * private implementation of INoteStore interface but another implementation
 * can be injected at SynchronizationManager construction time. For one thing,
 * such injection is used for testing of libquentier's synchronization
 * logic, for other things, it can be used to implement custom synchronization
 * with some alternative backends.
 */
class QUENTIER_EXPORT INoteStore: public QObject
{
    Q_OBJECT
protected:
    explicit INoteStore(const QSharedPointer<qevercloud::NoteStore> & pQecNoteStore, QObject * parent = Q_NULLPTR);

public:
    virtual ~INoteStore();

    QSharedPointer<qevercloud::NoteStore> getQecNoteStore();
    void setQecNoteStore(const QSharedPointer<qevercloud::NoteStore> & pQecNoteStore);

    QString noteStoreUrl() const;
    void setNoteStoreUrl(const QString & noteStoreUrl);

    QString authenticationToken() const;
    void setAuthenticationToken(const QString & authToken);

public:
    /**
     * Factory method, create a new INoteStore subclass object
     */
    virtual INoteStore * create() const = 0;

    virtual void stop() = 0;

    virtual qint32 createNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds, const QString & linkedNotebookAuthToken = QString()) = 0;
    virtual qint32 updateNotebook(Notebook & notebook, ErrorString & errorDescription, qint32 & rateLimitSeconds, const QString & linkedNotebookAuthToken = QString()) = 0;

    virtual qint32 createNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds, const QString & linkedNotebookAuthToken = QString()) = 0;
    virtual qint32 updateNote(Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds, const QString & linkedNotebookAuthToken = QString()) = 0;

    virtual qint32 createTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds, const QString & linkedNotebookAuthToken = QString()) = 0;
    virtual qint32 updateTag(Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds, const QString & linkedNotebookAuthToken = QString()) = 0;

    virtual qint32 createSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;
    virtual qint32 updateSavedSearch(SavedSearch & savedSearch, ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    virtual qint32 getSyncState(qevercloud::SyncState & syncState, ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    virtual qint32 getSyncChunk(const qint32 afterUSN, const qint32 maxEntries,
                                const qevercloud::SyncChunkFilter & filter,
                                qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                qint32 & rateLimitSeconds) = 0;

    virtual qint32 getLinkedNotebookSyncState(const qevercloud::LinkedNotebook & linkedNotebook,
                                              const QString & authToken, qevercloud::SyncState & syncState,
                                              ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    virtual qint32 getLinkedNotebookSyncChunk(const qevercloud::LinkedNotebook & linkedNotebook,
                                              const qint32 afterUSN, const qint32 maxEntries,
                                              const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
                                              qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
                                              qint32 & rateLimitSeconds) = 0;

    virtual qint32 getNote(const bool withContent, const bool withResourcesData,
                           const bool withResourcesRecognition, const bool withResourceAlternateData,
                           Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    virtual bool getNoteAsync(const bool withContent, const bool withResourceData, const bool withResourcesRecognition,
                              const bool withResourceAlternateData, const bool withSharedNotes,
                              const bool withNoteAppDataValues, const bool withResourceAppDataValues,
                              const bool withNoteLimits, const QString & noteGuid,
                              const QString & authToken, ErrorString & errorDescription) = 0;

    virtual qint32 getResource(const bool withDataBody, const bool withRecognitionDataBody,
                               const bool withAlternateDataBody, const bool withAttributes,
                               const QString & authToken, Resource & resource, ErrorString & errorDescription,
                               qint32 & rateLimitSeconds) = 0;

    virtual bool getResourceAsync(const bool withDataBody, const bool withRecognitionDataBody,
                                  const bool withAlternateDataBody, const bool withAttributes, const QString & resourceGuid,
                                  const QString & authToken, ErrorString & errorDescription) = 0;

    virtual qint32 authenticateToSharedNotebook(const QString & shareKey, qevercloud::AuthenticationResult & authResult,
                                                ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

Q_SIGNALS:
    void getNoteAsyncFinished(qint32 errorCode, qevercloud::Note note, qint32 rateLimitSeconds, ErrorString errorDescription);
    void getResourceAsyncFinished(qint32 errorCode, qevercloud::Resource resource, qint32 rateLimitSeconds, ErrorString errorDescription);

private:
    Q_DISABLE_COPY(INoteStore)

protected:
    QSharedPointer<qevercloud::NoteStore>       m_pQecNoteStore;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_I_NOTE_STORE_H
