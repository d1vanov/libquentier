/*
 * Copyright 2018-2020 Dmitry Ivanov
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

#include <quentier/synchronization/ForwardDeclarations.h>
#include <quentier/types/ErrorString.h>
#include <quentier/types/Note.h>
#include <quentier/types/Notebook.h>
#include <quentier/types/SavedSearch.h>
#include <quentier/types/Tag.h>
#include <quentier/utility/Linkage.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QObject>

#include <memory>

namespace quentier {

/**
 * @brief INoteStore is the interface which provides methods
 * required for the implementation of NoteStore part of Evernote EDAM sync
 * protocol
 */
class QUENTIER_EXPORT INoteStore : public QObject
{
    Q_OBJECT
protected:
    explicit INoteStore(QObject * parent = nullptr);

public:
    virtual ~INoteStore() = default;

    /*
     * Factory method, create a new INoteStore subclass object
     */
    virtual INoteStore * create() const = 0;

    /**
     * Provide note store URL used by this INoteStore instance
     */
    virtual QString noteStoreUrl() const = 0;

    /**
     * Set note store URL to be used by this INoteStore instance
     */
    virtual void setNoteStoreUrl(QString noteStoreUrl) = 0;

    /**
     * Set authentication data to be used by this INoteStore instance
     */
    virtual void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) = 0;

    /**
     * Stop asynchronous queries for notes or resources which might be running
     * at the moment
     */
    virtual void stop() = 0;

    /**
     * Create notebook
     *
     * @param notebook          Notebook to be created, must have name set and
     *                          either "active" or "default notebook" fields
     *                          may be set
     * @param errorDescription  The textual description of the error in case
     *                          notebook could not be created
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting
     *                          to call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @param linkedNotebookAuthToken   If a notebook is created within another
     *                                  user's account, the corresponding auth
     *                                  token should be set, otherwise
     *                                  the notebook would be created in user's
     *                                  own account
     * @return                  Error code, 0 in case of successful notebook
     *                          creation, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 createNotebook(
        Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds, QString linkedNotebookAuthToken = {}) = 0;

    /**
     * Update notebook
     *
     * @param notebook          Notebook to be updated, must have guid set
     * @param errorDescription  The textual description of the error in case
     *                          notebook could not be updated
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @param linkedNotebookAuthToken   If a notebook is updated within another
     *                                  user's account, the corresponding auth
     *                                  token should be set, otherwise
     *                                  the notebook would be updated within
     *                                  user's own account
     * @return                  Error code, 0 in case of successful notebook
     *                          update, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 updateNotebook(
        Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds, QString linkedNotebookAuthToken = {}) = 0;

    /**
     * Create note
     *
     * @param note              Note to be created
     * @param errorDescription  The textual description of the error in case
     *                          note could not be created
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @param linkedNotebookAuthToken   If a note is created within another
     *                                  user's account, the corresponding auth
     *                                  token should be set, otherwise the note
     *                                  would be created in user's own account
     * @return                  Error code, 0 in case of successful note
     *                          creation, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 createNote(
        Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) = 0;

    /**
     * Update note
     *
     * @param note              Note to be updated, must have guid set
     * @param errorDescription  The textual description of the error in case
     *                          note could not be updated
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @param linkedNotebookAuthToken   If a note is updated within another
     *                                  user's account, the corresponding auth
     *                                  token should be set, otherwise the note
     *                                  would be updated within user's own
     *                                  account
     * @return                  Error code, 0 in case of successful note update,
     *                          other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 updateNote(
        Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) = 0;

    /**
     * Create tag
     *
     * @param note              Tag to be created, must have name set, can also
     *                          have parent guid set
     * @param errorDescription  The textual description of the error in case
     *                          tag could not be created
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @param linkedNotebookAuthToken   If a tag is created within another
     *                                  user's account, the corresponding auth
     *                                  token should be set, otherwise the tag
     *                                  would be created in user's own account
     * @return                  Error code, 0 in case of successful tag
     *                          creation, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 createTag(
        Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) = 0;

    /**
     * Update tag
     *
     * @param tag               Tag to be updated, must have guid set
     * @param errorDescription  The textual description of the error in case
     *                          tag could not be updated
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @param linkedNotebookAuthToken   If a tag is updated within another
     *                                  user's account, the corresponding auth
     *                                  token should be set, otherwise the tag
     *                                  would be updated within user's own
     *                                  account
     * @return                  Error code, 0 in case of successful tag update,
     *                          other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 updateTag(
        Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) = 0;

    /**
     * Create saved search
     *
     * @param savedSearch       Saved search to be created, must have name and
     *                          query set, can also have search scope set
     * @param errorDescription  The textual description of the error in case
     *                          saved search could not be created
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful saved search
     *                          creation, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 createSavedSearch(
        SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) = 0;

    /**
     * Update saved search
     *
     * @param savedSearch       Saved search to be updated, must have guid set
     * @param errorDescription  The textual description of the error in case
     *                          saved search could not be updated
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful saved search
     *                          update, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 updateSavedSearch(
        SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) = 0;

    /**
     * Get sync state
     *
     * @param syncState         Output parameter, the sync state
     * @param errorDescription  The textual description of the error in case
     *                          sync state could not be retrieved
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful sync state
     *                          retrieval, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 getSyncState(
        qevercloud::SyncState & syncState, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) = 0;

    /**
     * Get sync chunk
     *
     * @param afterUSN          The USN after which the sync chunks are being
     *                          requested
     * @param maxEntries        Max number of items within the sync chunk to be
     *                          returned
     * @param filter            Filter for items to be returned within the sync
     *                          chunks
     * @param syncChunk         Output parameter, the sync chunk
     * @param errorDescription  The textual description of the error in case
     *                          sync chunk could not be retrieved
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful sync chunk
     *                          retrieval, other values corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 getSyncChunk(
        const qint32 afterUSN, const qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) = 0;

    /**
     * Get linked notebook sync state
     *
     * @param linkedNotebook    The linked notebook for which the sync state
     *                          is being retrieved, must contain identifying
     *                          information and permissions to access
     *                          the notebook in question
     * @param authToken         The authentication token to use for the data
     *                          from the linked notebook
     * @param syncState         Output parameter, the sync state
     * @param errorDescription  The textual description of the error in case
     *                          linked notebook sync state could not be
     *                          retrieved
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful linked
     *                          notebook sync state retrieval, other values
     *                          corresponding to qevercloud::EDAMErrorCode
     *                          enumeration instead
     */
    virtual qint32 getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & authToken, qevercloud::SyncState & syncState,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    /**
     * Get linked notebook sync chunk
     *
     * @param linkedNotebook            The linked notebook for which the sync
     *                                  chunk is being retrieved, must contain
     *                                  identifying information and permissions
     *                                  to access the notebook in question
     * @param afterUSN                  The USN after which the sync chunks are
     *                                  being requested
     * @param maxEntries                Max number of items within the sync
     *                                  chunk to be returned
     * @param linkedNotebookAuthToken   The authentication token to use for the
     *                                  data from the linked notebook
     * @param fullSyncOnly              If true then client only wants initial
     *                                  data for a full sync. In this case
     *                                  Evernote service will not return any
     *                                  expunged objects and will not return
     *                                  any resources since these are also
     *                                  provided in their corresponding notes
     * @param syncChunk                 Output parameter, the sync chunk
     * @param errorDescription          The textual description of the error in
     *                                  case linked notebook sync chunk could
     *                                  not be retrieved
     * @param rateLimitSeconds          Output parameter, the number of seconds
     *                                  the client needs to wait before
     *                                  attempting to call this method or any
     *                                  other method calling Evernote API again;
     *                                  only meaningful if returned value
     *                                  matches
     *                                  qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                          Error code, 0 in case of successful
     *                                  linked notebook sync chunk retrieval,
     *                                  other values corresponding to
     *                                  qevercloud::EDAMErrorCode enumeration
     *                                  instead
     */
    virtual qint32 getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUSN, const qint32 maxEntries,
        const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) = 0;

    /**
     * Get note synchronously
     *
     * @param withContent               If true, the returned note would
     *                                  include the content
     * @param withResourcesData         If true, any resources the note might
     *                                  have would include their full data
     * @param withResourcesRecognition  If true, any resources the note might
     *                                  have and which have Evernote supplied
     *                                  recognition would include their full
     *                                  recognition data
     * @param withResourceAlternateData If true, any resources the note might
     *                                  have would include their full
     *                                  alternate data
     * @param note                      Input and output parameter,
     *                                  the retrieved note, must have guid set
     * @param errorDescription          The textual description of the error in
     *                                  case the note could not be retrieved
     * @param rateLimitSeconds          Output parameter, the number of seconds
     *                                  the client needs to wait before
     *                                  attempting to call this method or any
     *                                  other method calling Evernote API again;
     *                                  only meaningful if returned value
     *                                  matches
     *                                  qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                          Error code, 0 in case of successful note
     *                                  retrieval, other values corresponding to
     *                                  qevercloud::EDAMErrorCode enumeration
     *                                  instead
     */
    virtual qint32 getNote(
        const bool withContent, const bool withResourcesData,
        const bool withResourcesRecognition,
        const bool withResourceAlternateData, Note & note,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    /**
     * Get note asynchronously
     *
     * If the method returned true, the actual result of the method invokation
     * would be returned via the emission of getNoteAsyncFinished signal.
     *
     * @param withContent               If true, the returned note would
     *                                  include the content
     * @param withResourcesData         If true, any resources the note might
     *                                  have would include their full data
     * @param withResourcesRecognition  If true, any resources the note might
     *                                  have and which have Evernote supplied
     *                                  recognition would include their full
     *                                  recognition data
     * @param withResourceAlternateData If true, any resources the note might
     *                                  have would include their full
     *                                  alternate data
     * @param withSharedNotes           If true, any shared notes contained
     *                                  within the note would be provided along
     *                                  with the asynchronously fetched result
     * @param withNoteAppDataValues     If true, the asynchronously fetched note
     *                                  would contain the app data values
     * @param withResourceAppDataValues If true, the resources of asynchronously
     *                                  fetched note would contain the app data
     *                                  values
     * @param withNoteLimits            If true, the asynchronously fetched note
     *                                  would contain note limits
     * @param noteGuid                  The guid of the note to be retrieved
     * @param authToken                 Authentication token to use for note
     *                                  retrieval
     * @param errorDescription          The textual description of the error if
     *                                  the launch of async note retrieval has
     *                                  failed
     * @return                          True if the launch of async note
     *                                  retrieval was successful, false
     *                                  otherwise
     */
    virtual bool getNoteAsync(
        const bool withContent, const bool withResourceData,
        const bool withResourcesRecognition,
        const bool withResourceAlternateData, const bool withSharedNotes,
        const bool withNoteAppDataValues, const bool withResourceAppDataValues,
        const bool withNoteLimits, const QString & noteGuid,
        const QString & authToken, ErrorString & errorDescription) = 0;

    /**
     * Get resource synchronously
     *
     * @param withDataBody              If true, the returned resource would
     *                                  include its data body
     * @param withRecognitionDataBody   If true, the returned resource would
     *                                  include its recognition data body
     * @param withAlternateDataBody     If true, the returned resource would
     *                                  include its alternate data body
     * @param withAttributes            If true, the returned resource would
     *                                  include its attributes
     * @param authToken                 Authentication token to use for
     *                                  resource retrieval
     * @param resource                  Input and output parameter,
     *                                  the retrieved resource, must have guid
     *                                  set
     * @param errorDescription          The textual description of the error in
     *                                  case the resource could not be retrieved
     * @param rateLimitSeconds          Output parameter, the number of seconds
     *                                  the client needs to wait before
     *                                  attempting to call this method or any
     *                                  other method calling Evernote API again;
     *                                  only meaningful if returned value
     *                                  matches
     *                                  qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                          Error code, 0 in case of successful
     *                                  resource retrieval, other values
     *                                  corresponding to
     *                                  qevercloud::EDAMErrorCode enumeration
     *                                  instead
     */
    virtual qint32 getResource(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & authToken, Resource & resource,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

    /**
     * Get resource asynchronously
     *
     * If the method returned true, the actual result of the method invokation
     * would be returned via the emission of getResourceAsyncFinished signal.
     *
     * @param withDataBody              If true, the returned resource would
     *                                  include its data body
     * @param withRecognitionDataBody   If true, the returned resource would
     *                                  include its recognition data body
     * @param withAlternateDataBody     If true, the returned resource would
     *                                  include its alternate data body
     * @param withAttributes            If true, the returned resource would
     *                                  include its attributes
     * @param resourceGuid              The guid of the resource to be retrieved
     * @param authToken                 Authentication token to use for resource
     *                                  retrieval
     * @param errorDescription          The textual description of the error if
     *                                  the launch of async resource retrieval
     *                                  has failed
     * @return                          True if the launch of async resource
     *                                  retrieval was successful, false
     *                                  otherwise
     */
    virtual bool getResourceAsync(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & resourceGuid, const QString & authToken,
        ErrorString & errorDescription) = 0;

    /**
     * Authenticate to shared notebook
     *
     * @param shareKey          The shared notebook global identifier
     * @param authResult        Output parameter, the result of authentication
     * @param errorDescription  The textual description of the error if
     *                          authentication to shared notebook could not
     *                          be performed
     * @param rateLimitSeconds  Output parameter, the number of seconds
     *                          the client needs to wait before attempting to
     *                          call this method or any other method calling
     *                          Evernote API again; only meaningful if returned
     *                          value matches
     *                          qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED
     * @return                  Error code, 0 in case of successful
     *                          authentication to shared notebook, other values
     *                          corresponding to
     *                          qevercloud::EDAMErrorCode enumeration instead
     */
    virtual qint32 authenticateToSharedNotebook(
        const QString & shareKey, qevercloud::AuthenticationResult & authResult,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) = 0;

Q_SIGNALS:
    void getNoteAsyncFinished(
        qint32 errorCode, qevercloud::Note note, qint32 rateLimitSeconds,
        ErrorString errorDescription);

    void getResourceAsyncFinished(
        qint32 errorCode, qevercloud::Resource resource,
        qint32 rateLimitSeconds, ErrorString errorDescription);

private:
    Q_DISABLE_COPY(INoteStore)
};

QUENTIER_EXPORT INoteStorePtr newNoteStore(QObject * parent = nullptr);

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_I_NOTE_STORE_H
