/*
 * Copyright 2017-2020 Dmitry Ivanov
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

#ifndef LIB_QUENTIER_SYNCHRONIZATION_NOTE_STORE_H
#define LIB_QUENTIER_SYNCHRONIZATION_NOTE_STORE_H

#include <quentier/synchronization/INoteStore.h>

#include <quentier/types/ErrorString.h>

#include <qt5qevercloud/QEverCloud.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QUuid>

namespace quentier {

QT_FORWARD_DECLARE_CLASS(Notebook)
QT_FORWARD_DECLARE_CLASS(Note)
QT_FORWARD_DECLARE_CLASS(Resource)
QT_FORWARD_DECLARE_CLASS(Tag)
QT_FORWARD_DECLARE_CLASS(SavedSearch)

/**
 * @brief The NoteStore class in quentier namespace is a wrapper under NoteStore
 * from QEverCloud.
 *
 * The main difference from the underlying class is stronger exception safety:
 * most QEverCloud's methods throw exceptions to indicate errors (much like
 * the native Evernote API for supported languages do). Using exceptions along
 * with Qt is not simple and desirable. Therefore, this class' methods simply
 * redirect the requests to methods of QEverCloud's NoteStore but catch
 * the "expected" exceptions, "parse" their internal error flags and return
 * the textual representation of the error.
 *
 * libquentier at the moment uses only several methods from those available
 * in QEverCloud's NoteStore so only the small subset of original NoteStore's
 * API is wrapped here at the moment.
 */
class Q_DECL_HIDDEN NoteStore final : public INoteStore
{
    Q_OBJECT
public:
    explicit NoteStore(QObject * parent = nullptr);

    virtual ~NoteStore() override;

    virtual INoteStore * create() const override;

    virtual QString noteStoreUrl() const override;

    virtual void setNoteStoreUrl(QString noteStoreUrl) override;

    virtual void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) override;

    virtual void stop() override;

    virtual qint32 createNotebook(
        Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 updateNotebook(
        Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 createNote(
        Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 updateNote(
        Note & note, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 createTag(
        Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 updateTag(
        Tag & tag, ErrorString & errorDescription, qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    virtual qint32 createSavedSearch(
        SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 updateSavedSearch(
        SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getSyncState(
        qevercloud::SyncState & syncState, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getSyncChunk(
        const qint32 afterUSN, const qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & authToken, qevercloud::SyncState & syncState,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    virtual qint32 getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUSN, const qint32 maxEntries,
        const QString & linkedNotebookAuthToken, const bool fullSyncOnly,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    virtual qint32 getNote(
        const bool withContent, const bool withResourcesData,
        const bool withResourcesRecognition,
        const bool withResourceAlternateData, Note & note,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    virtual bool getNoteAsync(
        const bool withContent, const bool withResourceData,
        const bool withResourcesRecognition,
        const bool withResourceAlternateData, const bool withSharedNotes,
        const bool withNoteAppDataValues, const bool withResourceAppDataValues,
        const bool withNoteLimits, const QString & noteGuid,
        const QString & authToken, ErrorString & errorDescription) override;

    virtual qint32 getResource(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & authToken, Resource & resource,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    virtual bool getResourceAsync(
        const bool withDataBody, const bool withRecognitionDataBody,
        const bool withAlternateDataBody, const bool withAttributes,
        const QString & resourceGuid, const QString & authToken,
        ErrorString & errorDescription) override;

    virtual qint32 authenticateToSharedNotebook(
        const QString & shareKey, qevercloud::AuthenticationResult & authResult,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

private:
    using EverCloudExceptionDataPtr = qevercloud::EverCloudExceptionDataPtr;
    using IRequestContextPtr = qevercloud::IRequestContextPtr;

private Q_SLOTS:
    void onGetNoteAsyncFinished(
        QVariant result, EverCloudExceptionDataPtr exceptionData,
        IRequestContextPtr ctx);

    void onGetResourceAsyncFinished(
        QVariant result, EverCloudExceptionDataPtr exceptionData,
        IRequestContextPtr ctx);

private:
    enum class UserExceptionSource
    {
        Creation = 0,
        Update
    };

    qint32 processEdamUserExceptionForNotebook(
        const Notebook & notebook,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForNote(
        const Note & note, const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForTag(
        const Tag & tag, const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForSavedSearch(
        const SavedSearch & search,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForGetSyncChunk(
        const qevercloud::EDAMUserException & userException,
        const qint32 afterUSN, const qint32 maxEntries,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForGetNote(
        const qevercloud::Note & note,
        const qevercloud::EDAMUserException & userException,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForGetResource(
        const qevercloud::Resource & resource,
        const qevercloud::EDAMUserException & userException,
        ErrorString & errorDescription) const;

    qint32 processUnexpectedEdamUserException(
        const QString & typeName,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    qint32 processEdamSystemException(
        const qevercloud::EDAMSystemException & systemException,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) const;

    void processEdamNotFoundException(
        const qevercloud::EDAMNotFoundException & notFoundException,
        ErrorString & errorDescription) const;

    void processNextPendingGetNoteAsyncRequest();

private:
    Q_DISABLE_COPY(NoteStore)

private:
    qevercloud::INoteStorePtr m_pNoteStore;
    QString m_authenticationToken;

    struct RequestData
    {
        QString m_guid;
        QPointer<qevercloud::AsyncResult> m_asyncResult;
    };

    struct GetNoteRequest
    {
        QString m_guid;
        QString m_authToken;

        bool m_withContent = false;
        bool m_withResourceData = false;
        bool m_withResourcesRecognition = false;
        bool m_withResourceAlternateData = false;
        bool m_withSharedNotes = false;
        bool m_withNoteAppDataValues = false;
        bool m_withResourceAppDataValues = false;
        bool m_withNoteLimits = false;
    };

    int m_getNoteAsyncRequestCount = 0;
    int m_getNoteAsyncRequestCountMax = 100;

    QQueue<GetNoteRequest> m_pendingGetNoteRequests;

    QHash<QUuid, RequestData> m_noteRequestDataById;
    QHash<QUuid, RequestData> m_resourceRequestDataById;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_STORE_H
