/*
 * Copyright 2017-2021 Dmitry Ivanov
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

#include <qevercloud/QEverCloud.h>

#include <QFuture>
#include <QFutureWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QUuid>
#include <QVariant>

namespace quentier {

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

    ~NoteStore() override;

    [[nodiscard]] INoteStore * create() const override;

    [[nodiscard]] QString noteStoreUrl() const override;
    void setNoteStoreUrl(QString noteStoreUrl) override;

    void setAuthData(
        QString authenticationToken, QList<QNetworkCookie> cookies) override;

    void stop() override;

    [[nodiscard]] qint32 createNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 updateNotebook(
        qevercloud::Notebook & notebook, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 createNote(
        qevercloud::Note & note, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 updateNote(
        qevercloud::Note & note, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 createTag(
        qevercloud::Tag & tag, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 updateTag(
        qevercloud::Tag & tag, ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        QString linkedNotebookAuthToken = {}) override;

    [[nodiscard]] qint32 createSavedSearch(
        qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 updateSavedSearch(
        qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getSyncState(
        qevercloud::SyncState & syncState, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getSyncChunk(
        qint32 afterUSN, qint32 maxEntries,
        const qevercloud::SyncChunkFilter & filter,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & authToken, qevercloud::SyncState & syncState,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        qint32 afterUSN, qint32 maxEntries,
        const QString & linkedNotebookAuthToken, bool fullSyncOnly,
        qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
        qint32 & rateLimitSeconds) override;

    [[nodiscard]] qint32 getNote(
        bool withContent, bool withResourcesData, bool withResourcesRecognition,
        bool withResourceAlternateData, qevercloud::Note & note,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    [[nodiscard]] bool getNoteAsync(
        bool withContent, bool withResourceData, bool withResourcesRecognition,
        bool withResourceAlternateData, bool withSharedNotes,
        bool withNoteAppDataValues, bool withResourceAppDataValues,
        bool withNoteLimits, const QString & noteGuid,
        const QString & authToken, ErrorString & errorDescription) override;

    [[nodiscard]] qint32 getResource(
        bool withDataBody, bool withRecognitionDataBody,
        bool withAlternateDataBody, bool withAttributes,
        const QString & authToken, qevercloud::Resource & resource,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

    [[nodiscard]] bool getResourceAsync(
        bool withDataBody, bool withRecognitionDataBody,
        bool withAlternateDataBody, bool withAttributes,
        const QString & resourceGuid, const QString & authToken,
        ErrorString & errorDescription) override;

    [[nodiscard]] qint32 authenticateToSharedNotebook(
        const QString & shareKey, qevercloud::AuthenticationResult & authResult,
        ErrorString & errorDescription, qint32 & rateLimitSeconds) override;

private:
    using IRequestContextPtr = qevercloud::IRequestContextPtr;

    void onGetNoteAsyncFinished(
        const qevercloud::Note & result,
        const std::exception_ptr & e,
        const IRequestContextPtr & ctx);

    void onGetResourceAsyncFinished(
        const qevercloud::Resource & result,
        const std::exception_ptr & e,
        const IRequestContextPtr & ctx);

private:
    enum class UserExceptionSource
    {
        Creation = 0,
        Update
    };

    [[nodiscard]] qint32 processEdamUserExceptionForNotebook(
        const qevercloud::Notebook & notebook,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processEdamUserExceptionForNote(
        const qevercloud::Note & note,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processEdamUserExceptionForTag(
        const qevercloud::Tag & tag,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processEdamUserExceptionForSavedSearch(
        const qevercloud::SavedSearch & search,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processEdamUserExceptionForGetSyncChunk(
        const qevercloud::EDAMUserException & userException,
        qint32 afterUSN, qint32 maxEntries,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processEdamUserExceptionForGetNote(
        const qevercloud::Note & note,
        const qevercloud::EDAMUserException & userException,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processEdamUserExceptionForGetResource(
        const qevercloud::Resource & resource,
        const qevercloud::EDAMUserException & userException,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processUnexpectedEdamUserException(
        const QString & typeName,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource & source,
        ErrorString & errorDescription) const;

    [[nodiscard]] qint32 processEdamSystemException(
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

    struct NoteRequestData
    {
        QString m_guid;
        QFuture<qevercloud::Note> m_future;
        QFutureWatcher<qevercloud::Note> * m_pFutureWatcher;
    };

    struct ResourceRequestData
    {
        QString m_guid;
        QFuture<qevercloud::Resource> m_future;
        QFutureWatcher<qevercloud::Resource> * m_pFutureWatcher;
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

    QHash<QUuid, NoteRequestData> m_noteRequestDataById;
    QHash<QUuid, ResourceRequestData> m_resourceRequestDataById;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_STORE_H
