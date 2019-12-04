/*
 * Copyright 2017-2019 Dmitry Ivanov
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

#include <quentier_private/synchronization/INoteStore.h>
#include <quentier/utility/Macros.h>
#include <quentier/types/ErrorString.h>
#include <QSharedPointer>
#include <QObject>
#include <QHash>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <qt5qevercloud/QEverCloud.h>
#else
#include <qt4qevercloud/QEverCloud.h>
#endif

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
class Q_DECL_HIDDEN NoteStore: public INoteStore
{
    Q_OBJECT
public:
    explicit NoteStore(const qevercloud::INoteStorePtr & pQecNoteStore,
                       QObject * parent = Q_NULLPTR);
    virtual ~NoteStore();

    virtual INoteStore * create() const Q_DECL_OVERRIDE;

    virtual void stop() Q_DECL_OVERRIDE;

    virtual qint32 createNotebook(
        Notebook & notebook,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;

    virtual qint32 updateNotebook(
        Notebook & notebook,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;

    virtual qint32 createNote(
        Note & note,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;

    virtual qint32 updateNote(
        Note & note,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;

    virtual qint32 createTag(
        Tag & tag,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;

    virtual qint32 updateTag(
        Tag & tag,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds,
        const QString & linkedNotebookAuthToken = QString()) Q_DECL_OVERRIDE;

    virtual qint32 createSavedSearch(
        SavedSearch & savedSearch,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 updateSavedSearch(
        SavedSearch & savedSearch,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getSyncState(qevercloud::SyncState & syncState,
                                ErrorString & errorDescription,
                                qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getSyncChunk(const qint32 afterUSN,
                                const qint32 maxEntries,
                                const qevercloud::SyncChunkFilter & filter,
                                qevercloud::SyncChunk & syncChunk,
                                ErrorString & errorDescription,
                                qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getLinkedNotebookSyncState(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const QString & authToken,
        qevercloud::SyncState & syncState,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getLinkedNotebookSyncChunk(
        const qevercloud::LinkedNotebook & linkedNotebook,
        const qint32 afterUSN,
        const qint32 maxEntries,
        const QString & linkedNotebookAuthToken,
        const bool fullSyncOnly,
        qevercloud::SyncChunk & syncChunk,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual qint32 getNote(const bool withContent,
                           const bool withResourcesData,
                           const bool withResourcesRecognition,
                           const bool withResourceAlternateData,
                           Note & note,
                           ErrorString & errorDescription,
                           qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual bool getNoteAsync(const bool withContent,
                              const bool withResourceData,
                              const bool withResourcesRecognition,
                              const bool withResourceAlternateData,
                              const bool withSharedNotes,
                              const bool withNoteAppDataValues,
                              const bool withResourceAppDataValues,
                              const bool withNoteLimits,
                              const QString & noteGuid,
                              const QString & authToken,
                              ErrorString & errorDescription) Q_DECL_OVERRIDE;

    virtual qint32 getResource(const bool withDataBody,
                               const bool withRecognitionDataBody,
                               const bool withAlternateDataBody,
                               const bool withAttributes,
                               const QString & authToken,
                               Resource & resource,
                               ErrorString & errorDescription,
                               qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

    virtual bool getResourceAsync(const bool withDataBody,
                                  const bool withRecognitionDataBody,
                                  const bool withAlternateDataBody,
                                  const bool withAttributes,
                                  const QString & resourceGuid,
                                  const QString & authToken,
                                  ErrorString & errorDescription) Q_DECL_OVERRIDE;

    virtual qint32 authenticateToSharedNotebook(
        const QString & shareKey,
        qevercloud::AuthenticationResult & authResult,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds) Q_DECL_OVERRIDE;

private:
    typedef qevercloud::EverCloudExceptionData EverCloudExceptionData;

private Q_SLOTS:
    void onGetNoteAsyncFinished(
        QVariant result, QSharedPointer<EverCloudExceptionData> exceptionData);

    void onGetResourceAsyncFinished(
        QVariant result, QSharedPointer<EverCloudExceptionData> exceptionData);

private:
    struct UserExceptionSource
    {
        enum type {
            Creation = 0,
            Update
        };
    };

    qint32 processEdamUserExceptionForNotebook(
        const Notebook & notebook,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource::type & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForNote(
        const Note & note,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource::type & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForTag(
        const Tag & tag,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource::type & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForSavedSearch(
        const SavedSearch & search,
        const qevercloud::EDAMUserException & userException,
        const UserExceptionSource::type & source,
        ErrorString & errorDescription) const;

    qint32 processEdamUserExceptionForGetSyncChunk(
        const qevercloud::EDAMUserException & userException,
        const qint32 afterUSN,
        const qint32 maxEntries,
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
        const UserExceptionSource::type & source,
        ErrorString & errorDescription) const;

    qint32 processEdamSystemException(
        const qevercloud::EDAMSystemException & systemException,
        ErrorString & errorDescription,
        qint32 & rateLimitSeconds) const;

    void processEdamNotFoundException(
        const qevercloud::EDAMNotFoundException & notFoundException,
        ErrorString & errorDescription) const;

private:
    Q_DISABLE_COPY(NoteStore)

private:
    QHash<qevercloud::AsyncResult*, QString>    m_noteGuidByAsyncResultPtr;
    QHash<qevercloud::AsyncResult*, QString>    m_resourceGuidByAsyncResultPtr;
};

} // namespace quentier

#endif // LIB_QUENTIER_SYNCHRONIZATION_NOTE_STORE_H
