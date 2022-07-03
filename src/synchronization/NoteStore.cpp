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

#include "NoteStore.h"

#include "ExceptionHandlingHelpers.h"

#include <quentier/logging/QuentierLogger.h>

namespace quentier {

#define SET_EDAM_USER_EXCEPTION_ERROR(userException)                           \
    errorDescription.setBase(                                                  \
        QT_TRANSLATE_NOOP("NoteStore", "caught EDAM user exception"));         \
    errorDescription.details() = QStringLiteral("error code");                 \
    errorDescription.details() += ToString(userException.errorCode());         \
    errorDescription.details() += QStringLiteral(": ");                        \
    errorDescription.details() +=                                              \
        QString::fromUtf8(userException.what())

#define NOTE_STORE_REQUEST_TIMEOUT_MSEC (-1)

NoteStore::NoteStore(QObject * parent) :
    INoteStore(parent), m_pNoteStore(qevercloud::newNoteStore())
{}

NoteStore::~NoteStore()
{
    stop();
}

INoteStore * NoteStore::create() const
{
    return new NoteStore;
}

QString NoteStore::noteStoreUrl() const
{
    return m_pNoteStore->noteStoreUrl();
}

void NoteStore::setNoteStoreUrl(QString noteStoreUrl)
{
    m_pNoteStore->setNoteStoreUrl(std::move(noteStoreUrl));
}

void NoteStore::setAuthData(
    QString authenticationToken, QList<QNetworkCookie> cookies)
{
    Q_UNUSED(cookies)
    m_authenticationToken = std::move(authenticationToken);
}

void NoteStore::stop()
{
    QNDEBUG("synchronization:note_store", "NoteStore::stop");

    for (const auto it: qevercloud::toRange(m_noteRequestDataById)) {
        auto & requestData = it.value();
        if (requestData.m_pFutureWatcher) {
            requestData.m_pFutureWatcher->disconnect(this);
            requestData.m_pFutureWatcher->deleteLater();
            requestData.m_pFutureWatcher = nullptr;
        }
    }

    m_noteRequestDataById.clear();

    for (const auto it: qevercloud::toRange(m_resourceRequestDataById)) {
        auto & requestData = it.value();
        if (requestData.m_pFutureWatcher) {
            requestData.m_pFutureWatcher->disconnect(this);
            requestData.m_pFutureWatcher->deleteLater();
            requestData.m_pFutureWatcher = nullptr;
        }
    }

    m_resourceRequestDataById.clear();
}

qint32 NoteStore::createNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            linkedNotebookAuthToken.isEmpty() ? m_authenticationToken
                                              : linkedNotebookAuthToken,
            NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const auto localId = notebook.localId();
        const auto localData = notebook.localData();
        notebook = m_pNoteStore->createNotebook(notebook, std::move(ctx));
        notebook.setLocalId(localId);
        notebook.setLocalData(localData);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForNotebook(
            notebook, userException, UserExceptionSource::Creation,
            errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::updateNotebook(
    qevercloud::Notebook & notebook, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            linkedNotebookAuthToken.isEmpty() ? m_authenticationToken
                                              : linkedNotebookAuthToken,
            NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        qint32 usn = m_pNoteStore->updateNotebook(notebook, std::move(ctx));
        notebook.setUpdateSequenceNum(usn);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForNotebook(
            notebook, userException, UserExceptionSource::Update,
            errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        processEdamNotFoundException(notFoundException, errorDescription);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::createNote(
    qevercloud::Note & note, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            linkedNotebookAuthToken.isEmpty() ? m_authenticationToken
                                              : linkedNotebookAuthToken,
            NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const qevercloud::Note noteMetadata =
            m_pNoteStore->createNote(note, std::move(ctx));

        QNDEBUG(
            "synchronization:note_store",
            "Note metadata returned from createNote method: " << noteMetadata);

        if (noteMetadata.guid()) {
            note.setGuid(*noteMetadata.guid());
        }

        if (noteMetadata.updateSequenceNum()) {
            note.setUpdateSequenceNum(*noteMetadata.updateSequenceNum());
        }

        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForNote(
            note, userException, UserExceptionSource::Creation,
            errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::updateNote(
    qevercloud::Note & note, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            linkedNotebookAuthToken.isEmpty() ? m_authenticationToken
                                              : linkedNotebookAuthToken,
            NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const qevercloud::Note noteMetadata =
            m_pNoteStore->updateNote(note, std::move(ctx));

        QNDEBUG(
            "synchronization:note_store",
            "Note metadata returned from updateNote method: " << noteMetadata);

        if (noteMetadata.guid()) {
            note.setGuid(*noteMetadata.guid());
        }

        if (noteMetadata.updateSequenceNum()) {
            note.setUpdateSequenceNum(*noteMetadata.updateSequenceNum());
        }

        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForNote(
            note, userException, UserExceptionSource::Update, errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        processEdamNotFoundException(notFoundException, errorDescription);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::createTag(
    qevercloud::Tag & tag, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            linkedNotebookAuthToken.isEmpty() ? m_authenticationToken
                                              : linkedNotebookAuthToken,
            NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const auto localId = tag.localId();
        const auto localData = tag.localData();
        tag = m_pNoteStore->createTag(tag, std::move(ctx));
        tag.setLocalId(localId);
        tag.setLocalData(localData);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForTag(
            tag, userException, UserExceptionSource::Creation,
            errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::updateTag(
    qevercloud::Tag & tag, ErrorString & errorDescription,
    qint32 & rateLimitSeconds, QString linkedNotebookAuthToken)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            linkedNotebookAuthToken.isEmpty() ? m_authenticationToken
                                              : linkedNotebookAuthToken,
            NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const qint32 usn = m_pNoteStore->updateTag(tag, std::move(ctx));
        tag.setUpdateSequenceNum(usn);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForTag(
            tag, userException, UserExceptionSource::Update, errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        processEdamNotFoundException(notFoundException, errorDescription);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::createSavedSearch(
    qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const auto localId = savedSearch.localId();
        const auto localData = savedSearch.localData();
        savedSearch = m_pNoteStore->createSearch(savedSearch, std::move(ctx));
        savedSearch.setLocalId(localId);
        savedSearch.setLocalData(localData);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForSavedSearch(
            savedSearch, userException, UserExceptionSource::Creation,
            errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::updateSavedSearch(
    qevercloud::SavedSearch & savedSearch, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const qint32 usn =
            m_pNoteStore->updateSearch(savedSearch, std::move(ctx));

        savedSearch.setUpdateSequenceNum(usn);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForSavedSearch(
            savedSearch, userException, UserExceptionSource::Update,
            errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        processEdamNotFoundException(notFoundException, errorDescription);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::getSyncState(
    qevercloud::SyncState & syncState, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        syncState = m_pNoteStore->getSyncState(std::move(ctx));
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        SET_EDAM_USER_EXCEPTION_ERROR(userException);
        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::getSyncChunk(
    const qint32 afterUSN, const qint32 maxEntries,
    const qevercloud::SyncChunkFilter & filter,
    qevercloud::SyncChunk & syncChunk, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore::getSyncChunk: "
            << "after USN = " << afterUSN << ", max entries = " << maxEntries
            << ", sync chunk filter = " << filter);

    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        syncChunk = m_pNoteStore->getFilteredSyncChunk(
            afterUSN, maxEntries, filter, std::move(ctx));
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForGetSyncChunk(
            userException, afterUSN, maxEntries, errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::getLinkedNotebookSyncState(
    const qevercloud::LinkedNotebook & linkedNotebook,
    const QString & authToken, qevercloud::SyncState & syncState,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            authToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        syncState = m_pNoteStore->getLinkedNotebookSyncState(
            linkedNotebook, std::move(ctx));
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        SET_EDAM_USER_EXCEPTION_ERROR(userException);
        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        errorDescription.setBase(
            QT_TR_NOOP("caught EDAM not found exception, "
                       "could not find linked notebook to "
                       "get the sync state for"));
        errorDescription.details() +=
            QString::fromUtf8(notFoundException.what());

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::getLinkedNotebookSyncChunk(
    const qevercloud::LinkedNotebook & linkedNotebook, const qint32 afterUSN,
    const qint32 maxEntries, const QString & linkedNotebookAuthToken,
    const bool fullSyncOnly, qevercloud::SyncChunk & syncChunk,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore::getLinkedNotebookSyncChunk: linked notebook: "
            << linkedNotebook << "\nAfter USN = " << afterUSN
            << ", max entries = " << maxEntries
            << ", full sync only = " << (fullSyncOnly ? "true" : "false"));

    try {
        auto ctx = qevercloud::newRequestContext(
            linkedNotebookAuthToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        syncChunk = m_pNoteStore->getLinkedNotebookSyncChunk(
            linkedNotebook, afterUSN, maxEntries, fullSyncOnly, std::move(ctx));
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForGetSyncChunk(
            userException, afterUSN, maxEntries, errorDescription);
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        errorDescription.setBase(
            QT_TR_NOOP("caught EDAM not found exception while "
                       "attempting to download the sync chunk "
                       "for linked notebook"));
        auto errorMessage = QString::fromUtf8(notFoundException.what());
        if (errorMessage == QStringLiteral("LinkedNotebook")) {
            errorDescription.appendBase(
                QT_TR_NOOP("the provided information "
                            "doesn't match any valid notebook"));
        }
        else if (errorMessage == QStringLiteral("LinkedNotebook.uri")) {
            errorDescription.appendBase(
                QT_TR_NOOP("the provided public URI "
                            "doesn't match any valid notebook"));
        }
        else if (errorMessage == QStringLiteral("SharedNotebook.id")) {
            errorDescription.appendBase(
                QT_TR_NOOP("the provided information indicates the shared "
                            "notebook no longer exists"));
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unknown error"));
            errorDescription.details() = errorMessage;
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

qint32 NoteStore::getNote(
    const bool withContent, const bool withResourcesData,
    const bool withResourcesRecognition, const bool withResourceAlternateData,
    qevercloud::Note & note, ErrorString & errorDescription,
    qint32 & rateLimitSeconds)
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore::getNote: with content = "
            << (withContent ? "true" : "false") << ", with resources data = "
            << (withResourcesData ? "true" : "false")
            << ", with resources recognition = "
            << (withResourcesRecognition ? "true" : "false")
            << ", with resources alternate data = "
            << (withResourceAlternateData ? "true" : "false"));

    if (!note.guid()) {
        errorDescription.setBase(
            QT_TR_NOOP("can't get note: note's guid is empty"));
        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const auto localId = note.localId();
        const auto localData = note.localData();

        note = m_pNoteStore->getNote(
            *note.guid(), withContent, withResourcesData,
            withResourcesRecognition, withResourceAlternateData,
            std::move(ctx));

        note.setLocalId(localId);
        note.setLocalData(localData);
        note.setLocalOnly(false);
        note.setLocallyModified(false);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForGetNote(
            note, userException, errorDescription);
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        processEdamNotFoundException(notFoundException, errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

bool NoteStore::getNoteAsync(
    const bool withContent, const bool withResourceData,
    const bool withResourcesRecognition, const bool withResourceAlternateData,
    const bool withSharedNotes, const bool withNoteAppDataValues,
    const bool withResourceAppDataValues, const bool withNoteLimits,
    const QString & noteGuid, const QString & authToken,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore::getNoteAsync: "
            << "with content = " << (withContent ? "true" : "false")
            << ", with resource data = "
            << (withResourceData ? "true" : "false")
            << ", with resource recognition = "
            << (withResourcesRecognition ? "true" : "false")
            << ", with resource alternate data = "
            << (withResourceAlternateData ? "true" : "false")
            << ", with shared notes = " << (withSharedNotes ? "true" : "false")
            << ", with note app data values = "
            << (withNoteAppDataValues ? "true" : "false")
            << ", with resource app data values = "
            << (withResourceAppDataValues ? "true" : "false")
            << ", with note limits = " << (withNoteLimits ? "true" : "false")
            << ", note guid = " << noteGuid);

    if (Q_UNLIKELY(noteGuid.isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected the attempt to get full "
                       "note's data for empty note guid"));
        return false;
    }

    if (m_getNoteAsyncRequestCount >= m_getNoteAsyncRequestCountMax) {
        QNDEBUG(
            "synchronization:note_store",
            "Too many get note async requests are already in flight: "
                << m_getNoteAsyncRequestCount
                << ", queueing the request to be executed later for note with "
                << "guid " << noteGuid);

        GetNoteRequest request;
        request.m_guid = noteGuid;
        request.m_authToken = authToken;
        request.m_withContent = withContent;
        request.m_withResourceData = withResourceData;
        request.m_withResourcesRecognition = withResourcesRecognition;
        request.m_withResourceAlternateData = withResourceAlternateData;
        request.m_withSharedNotes = withSharedNotes;
        request.m_withNoteAppDataValues = withNoteAppDataValues;
        request.m_withResourceAppDataValues = withResourceAppDataValues;
        request.m_withNoteLimits = withNoteLimits;

        m_pendingGetNoteRequests.enqueue(request);

        QNDEBUG(
            "synchronization:note_store",
            "Queue of pending get note async requests now has "
                << m_pendingGetNoteRequests.size() << " items");

        return true;
    }

    qevercloud::NoteResultSpec noteResultSpec;
    noteResultSpec.setIncludeContent(withContent);
    noteResultSpec.setIncludeResourcesData(withResourceData);
    noteResultSpec.setIncludeResourcesRecognition(withResourcesRecognition);
    noteResultSpec.setIncludeResourcesAlternateData(withResourceAlternateData);
    noteResultSpec.setIncludeSharedNotes(withSharedNotes);
    noteResultSpec.setIncludeNoteAppDataValues(withNoteAppDataValues);
    noteResultSpec.setIncludeResourceAppDataValues(withResourceAppDataValues);
    noteResultSpec.setIncludeAccountLimits(withNoteLimits);

    QNTRACE(
        "synchronization:note_store", "Note result spec: " << noteResultSpec);

    auto ctx = qevercloud::newRequestContext(
        authToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

    auto & requestData = m_noteRequestDataById[ctx->requestId()];
    requestData.m_guid = noteGuid;
    requestData.m_future = m_pNoteStore->getNoteWithResultSpecAsync(
        noteGuid, noteResultSpec, ctx);

    if (requestData.m_pFutureWatcher) {
        requestData.m_pFutureWatcher->disconnect(this);
        requestData.m_pFutureWatcher->deleteLater();
    }

    requestData.m_pFutureWatcher = new QFutureWatcher<qevercloud::Note>(this);

    QObject::connect(
        requestData.m_pFutureWatcher,
        &QFutureWatcher<QVariant>::finished,
        this,
        [this, ctx]
        {
            auto it = m_noteRequestDataById.find(ctx->requestId());
            if (it == m_noteRequestDataById.end()) {
                return;
            }

            auto * pWatcher = it->m_pFutureWatcher;

            std::exception_ptr e;
            qevercloud::Note value;

            try
            {
                value = pWatcher->result();
            }
            catch (...)
            {
                e = std::current_exception();
            }

            onGetNoteAsyncFinished(value, e, ctx);
        });

    requestData.m_pFutureWatcher->setFuture(requestData.m_future);

    ++m_getNoteAsyncRequestCount;
    return true;
}

qint32 NoteStore::getResource(
    const bool withDataBody, const bool withRecognitionDataBody,
    const bool withAlternateDataBody, const bool withAttributes,
    const QString & authToken, qevercloud::Resource & resource,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore::getResource: "
            << "with data body = " << (withDataBody ? "true" : "false")
            << ", with recognition data body = "
            << (withRecognitionDataBody ? "true" : "false")
            << ", with alternate data body = "
            << (withAlternateDataBody ? "true" : "false")
            << ", with attributes = " << (withAttributes ? "true" : "false")
            << ", resource guid = "
            << (resource.guid() ? *resource.guid()
                                : QStringLiteral("<not set>")));

    if (!resource.guid()) {
        errorDescription.setBase(
            QT_TR_NOOP("can't get resource: resource's guid is empty"));
        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN);
    }

    try {
        auto ctx = qevercloud::newRequestContext(
            authToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        const auto localId = resource.localId();
        const auto localData = resource.localData();

        resource = m_pNoteStore->getResource(
            *resource.guid(), withDataBody, withRecognitionDataBody,
            withAttributes, withAlternateDataBody, std::move(ctx));

        resource.setLocalId(localId);
        resource.setLocalData(localData);
        resource.setLocalOnly(false);
        resource.setLocallyModified(false);
        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        return processEdamUserExceptionForGetResource(
            resource, userException, errorDescription);
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        processEdamNotFoundException(notFoundException, errorDescription);
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        return processEdamSystemException(
            systemException, errorDescription, rateLimitSeconds);
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

bool NoteStore::getResourceAsync(
    const bool withDataBody, const bool withRecognitionDataBody,
    const bool withAlternateDataBody, const bool withAttributes,
    const QString & resourceGuid, const QString & authToken,
    ErrorString & errorDescription)
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore::getResourceAsync: "
            << "with data body = " << (withDataBody ? "true" : "false")
            << ", with recognition data body = "
            << (withRecognitionDataBody ? "true" : "false")
            << ", with alternate data body = "
            << (withAlternateDataBody ? "true" : "false")
            << ", with attributes = " << (withAttributes ? "true" : "false")
            << ", resource guid = " << resourceGuid);

    if (Q_UNLIKELY(resourceGuid.isEmpty())) {
        errorDescription.setBase(
            QT_TR_NOOP("Detected the attempt to get full "
                       "resource's data for empty resource guid"));
        return false;
    }

    auto ctx = qevercloud::newRequestContext(
        authToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

    auto & requestData = m_resourceRequestDataById[ctx->requestId()];
    requestData.m_guid = resourceGuid;
    requestData.m_future = m_pNoteStore->getResourceAsync(
        resourceGuid, withDataBody, withRecognitionDataBody, withAttributes,
        withAlternateDataBody, ctx);

    if (requestData.m_pFutureWatcher) {
        requestData.m_pFutureWatcher->disconnect(this);
        requestData.m_pFutureWatcher->deleteLater();
    }

    requestData.m_pFutureWatcher =
        new QFutureWatcher<qevercloud::Resource>(this);

    QObject::connect(
        requestData.m_pFutureWatcher,
        &QFutureWatcher<QVariant>::finished,
        this,
        [this, ctx]
        {
            auto it = m_resourceRequestDataById.find(ctx->requestId());
            if (it == m_resourceRequestDataById.end()) {
                return;
            }

            auto * pWatcher = it->m_pFutureWatcher;

            std::exception_ptr e;
            qevercloud::Resource value;

            try
            {
                value = pWatcher->result();
            }
            catch (...)
            {
                e = std::current_exception();
            }

            onGetResourceAsyncFinished(value, e, ctx);
        });

    requestData.m_pFutureWatcher->setFuture(requestData.m_future);
    return true;
}

qint32 NoteStore::authenticateToSharedNotebook(
    const QString & shareKey, qevercloud::AuthenticationResult & authResult,
    ErrorString & errorDescription, qint32 & rateLimitSeconds)
{
    try {
        auto ctx = qevercloud::newRequestContext(
            m_authenticationToken, NOTE_STORE_REQUEST_TIMEOUT_MSEC);

        authResult = m_pNoteStore->authenticateToSharedNotebook(
            shareKey, std::move(ctx));

        return 0;
    }
    catch (const qevercloud::EDAMUserException & userException) {
        if (userException.errorCode() ==
            qevercloud::EDAMErrorCode::DATA_REQUIRED)
        {
            errorDescription.setBase(
                QT_TR_NOOP("no valid authentication token for current user"));
        }
        else if (
            userException.errorCode() ==
            qevercloud::EDAMErrorCode::PERMISSION_DENIED)
        {
            errorDescription.setBase(
                QT_TR_NOOP("share requires login, and another "
                           "username has already been bound to this notebook"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("unexpected EDAM user exception"));
            errorDescription.details() = QStringLiteral("error code = ");
            errorDescription.details() += ToString(userException.errorCode());
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }
    catch (const qevercloud::EDAMNotFoundException & notFoundException) {
        // NOTE: this means that shared notebook no longer exists. It can happen
        // with shared/linked notebooks from time to time so it shouldn't be
        // really considered an error. Instead, the method would return empty
        // auth result which should indicate the fact of missing shared notebook
        // to the caller
        Q_UNUSED(notFoundException)
        authResult = qevercloud::AuthenticationResult();
        return 0;
    }
    catch (const qevercloud::EDAMSystemException & systemException) {
        if (systemException.errorCode() ==
            qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
            if (!systemException.rateLimitDuration()) {
                errorDescription.setBase(
                    QT_TR_NOOP("QEverCloud error: "
                               "RATE_LIMIT_REACHED exception "
                               "was caught but rateLimitDuration "
                               "is not set"));
                // FIXME: should actually return properly typed
                // qevercloud::EDAMErrorCode
                return static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN);
            }

            rateLimitSeconds = *systemException.rateLimitDuration();
        }
        else if (
            systemException.errorCode() ==
            qevercloud::EDAMErrorCode::BAD_DATA_FORMAT)
        {
            errorDescription.setBase(QT_TR_NOOP("invalid share key"));
        }
        else if (
            systemException.errorCode() ==
            qevercloud::EDAMErrorCode::INVALID_AUTH) {
            errorDescription.setBase(QT_TR_NOOP("bad signature of share key"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("unexpected EDAM system exception"));
            errorDescription.details() = QStringLiteral("error code = ");
            errorDescription.details() += ToString(systemException.errorCode());
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(systemException.errorCode());
    }
    CATCH_GENERIC_EXCEPTIONS_NO_RET()

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<qint32>(qevercloud::EDAMErrorCode::UNKNOWN);
}

void NoteStore::onGetNoteAsyncFinished(
    const qevercloud::Note & result, const std::exception_ptr & e,
    const IRequestContextPtr & ctx)
{
    QNDEBUG("synchronization:note_store", "NoteStore::onGetNoteAsyncFinished");

    --m_getNoteAsyncRequestCount;

    auto it = m_noteRequestDataById.find(ctx->requestId());
    if (Q_UNLIKELY(it == m_noteRequestDataById.end())) {
        QNWARNING(
            "synchronization:note_store",
            "Received getNoteAsyncFinished "
                << "event for unidentified request id: " << ctx->requestId());
        processNextPendingGetNoteAsyncRequest();
        return;
    }

    const auto & requestData = it.value();
    qevercloud::Note note;
    note.setGuid(requestData.m_guid);
    if (requestData.m_pFutureWatcher) {
        requestData.m_pFutureWatcher->disconnect(this);
        requestData.m_pFutureWatcher->deleteLater();
    }

    m_noteRequestDataById.erase(it);

    ErrorString errorDescription;
    qint32 errorCode = 0;
    qint32 rateLimitSeconds = -1;

    if (e) {
        try {
            std::rethrow_exception(e);
        }
        catch (const qevercloud::EDAMUserException & userException) {
            errorCode = processEdamUserExceptionForGetNote(
                note, userException, errorDescription);
        }
        catch (const qevercloud::EDAMNotFoundException & notFoundException) {
            processEdamNotFoundException(notFoundException, errorDescription);
            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            errorCode = static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN);
        }
        catch (const qevercloud::EDAMSystemException & systemException) {
            errorCode = processEdamSystemException(
                systemException, errorDescription, rateLimitSeconds);
        }
        CATCH_GENERIC_EXCEPTIONS_IMPL(
            errorCode = static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN))

        Q_EMIT getNoteAsyncFinished(
            errorCode, note, rateLimitSeconds, errorDescription);

        processNextPendingGetNoteAsyncRequest();
        return;
    }

    note = result;
    Q_EMIT getNoteAsyncFinished(
        errorCode, note, rateLimitSeconds, errorDescription);

    processNextPendingGetNoteAsyncRequest();
}

void NoteStore::onGetResourceAsyncFinished(
    const qevercloud::Resource & result, const std::exception_ptr & e,
    const IRequestContextPtr & ctx)
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore::onGetResourceAsyncFinished");

    auto it = m_resourceRequestDataById.find(ctx->requestId());
    if (Q_UNLIKELY(it == m_resourceRequestDataById.end())) {
        QNWARNING(
            "synchronization:note_store",
            "Received getResourceAsyncFinished event for unidentified request "
                << "id: " << ctx->requestId());
        return;
    }

    const auto & requestData = it.value();
    qevercloud::Resource resource;
    resource.setGuid(requestData.m_guid);
    if (requestData.m_pFutureWatcher) {
        requestData.m_pFutureWatcher->disconnect(this);
        requestData.m_pFutureWatcher->deleteLater();
    }

    m_resourceRequestDataById.erase(it);

    ErrorString errorDescription;
    qint32 errorCode = 0;
    qint32 rateLimitSeconds = -1;

    if (e) {
        try {
            std::rethrow_exception(e);
        }
        catch (const qevercloud::EDAMUserException & userException) {
            errorCode = processEdamUserExceptionForGetResource(
                resource, userException, errorDescription);
        }
        catch (const qevercloud::EDAMNotFoundException & notFoundException) {
            processEdamNotFoundException(notFoundException, errorDescription);
            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            errorCode = static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN);
        }
        catch (const qevercloud::EDAMSystemException & systemException) {
            errorCode = processEdamSystemException(
                systemException, errorDescription, rateLimitSeconds);
        }
        CATCH_GENERIC_EXCEPTIONS_IMPL(
            errorCode = static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN))

        Q_EMIT getResourceAsyncFinished(
            errorCode, resource, rateLimitSeconds, errorDescription);
        return;
    }

    resource = result;
    Q_EMIT getResourceAsyncFinished(
        errorCode, resource, rateLimitSeconds, errorDescription);
}

qint32 NoteStore::processEdamUserExceptionForTag(
    const qevercloud::Tag & tag,
    const qevercloud::EDAMUserException & userException,
    const NoteStore::UserExceptionSource & source,
    ErrorString & errorDescription) const
{
    bool thrownOnCreation = (source == UserExceptionSource::Creation);

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to create a tag"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to update a tag"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Tag.name")) {
            if (tag.name()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid length or pattern of tag's name"));
                errorDescription.details() = *tag.name();
            }
            else {
                errorDescription.appendBase(QT_TR_NOOP("tag has no name"));
            }
        }
        else if (
            *userException.parameter() == QStringLiteral("Tag.parentGuid")) {
            if (tag.parentGuid()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("malformed parent guid of tag"));
                errorDescription.details() = *tag.parentGuid();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("error code indicates malformed parent guid "
                               "but it is empty"));
            }
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() == qevercloud::EDAMErrorCode::DATA_CONFLICT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to create a tag"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to update a tag"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Tag.name")) {
            if (tag.name()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid length or pattern of tag's name"));
                errorDescription.details() = *tag.name();
            }
            else {
                errorDescription.appendBase(QT_TR_NOOP("tag has no name"));
            }
        }

        if (!thrownOnCreation &&
            (*userException.parameter() == QStringLiteral("Tag.parentGuid")))
        {
            if (tag.parentGuid()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("can't set parent for tag: circular "
                               "parent-child correlation detected"));
                errorDescription.details() = *tag.parentGuid();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("error code indicates the problem with "
                               "circular parent-child correlation but tag's "
                               "parent guid is empty"));
            }
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (thrownOnCreation &&
        (userException.errorCode() == qevercloud::EDAMErrorCode::LIMIT_REACHED))
    {
        errorDescription.setBase(
            QT_TR_NOOP("LIMIT_REACHED exception during "
                       "the attempt to create a tag"));

        if (userException.parameter() &&
            (*userException.parameter() == QStringLiteral("Tag")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("already at max number of tags, "
                           "please remove some of them"));
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (!thrownOnCreation &&
        (userException.errorCode() ==
         qevercloud::EDAMErrorCode::PERMISSION_DENIED))
    {
        errorDescription.setBase(
            QT_TR_NOOP("PERMISSION_DENIED exception during "
                       "the attempt to update a tag"));

        if (userException.parameter() &&
            (*userException.parameter() == QStringLiteral("Tag")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("user doesn't own the tag, it can't be updated"));
            if (tag.name()) {
                errorDescription.details() = *tag.name();
            }
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    return processUnexpectedEdamUserException(
        QStringLiteral("tag"), userException, source, errorDescription);
}

qint32 NoteStore::processEdamUserExceptionForSavedSearch(
    const qevercloud::SavedSearch & search,
    const qevercloud::EDAMUserException & userException,
    const NoteStore::UserExceptionSource & source,
    ErrorString & errorDescription) const
{
    bool thrownOnCreation = (source == UserExceptionSource::Creation);

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to create a saved search"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to update a saved search"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("SavedSearch.name"))
        {
            if (search.name()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid length or pattern "
                               "of saved search's name"));
                errorDescription.details() = *search.name();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("saved search has no name"));
            }
        }
        else if (
            *userException.parameter() ==
            QStringLiteral("SavedSearch.query")) {
            if (search.query()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid length of saved search's query"));

                errorDescription.details() =
                    QString::number(search.query()->length());

                QNWARNING(
                    "synchronization:note_store",
                    errorDescription << ", query: " << *search.query());
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("saved search has no query"));
            }
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() == qevercloud::EDAMErrorCode::DATA_CONFLICT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to create a saved search"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to update a saved search"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("SavedSearch.name"))
        {
            if (search.name()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("saved search's name is already in use"));
                errorDescription.details() = *search.name();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("saved search has no name"));
            }
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (thrownOnCreation &&
        (userException.errorCode() == qevercloud::EDAMErrorCode::LIMIT_REACHED))
    {
        errorDescription.setBase(
            QT_TR_NOOP("LIMIT_REACHED exception during "
                       "the attempt to create saved search: "
                       "already at max number of saved searches"));
        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (!thrownOnCreation &&
        (userException.errorCode() ==
         qevercloud::EDAMErrorCode::PERMISSION_DENIED))
    {
        errorDescription.setBase(
            QT_TR_NOOP("PERMISSION_DENIED exception during "
                       "the attempt to update saved search: "
                       "user doesn't own saved search"));
        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    return processUnexpectedEdamUserException(
        QStringLiteral("saved search"), userException, source,
        errorDescription);
}

qint32 NoteStore::processEdamUserExceptionForGetSyncChunk(
    const qevercloud::EDAMUserException & userException, const qint32 afterUSN,
    const qint32 maxEntries, ErrorString & errorDescription) const
{
    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT) {
        errorDescription.setBase(
            QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                       "the attempt to get sync chunk"));

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("afterUSN")) {
            errorDescription.appendBase(QT_TR_NOOP("afterUSN is negative"));
            errorDescription.details() = QString::number(afterUSN);
        }
        else if (*userException.parameter() == QStringLiteral("maxEntries"))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("maxEntries is less than 1"));
            errorDescription.details() = QString::number(maxEntries);
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }
    }
    else {
        errorDescription.setBase(
            QT_TR_NOOP("Unknown EDAM user exception on "
                       "attempt to get sync chunk"));
        errorDescription.details() =
            QString::fromUtf8(userException.what());
    }

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<int>(userException.errorCode());
}

qint32 NoteStore::processEdamUserExceptionForGetNote(
    const qevercloud::Note & note,
    const qevercloud::EDAMUserException & userException,
    ErrorString & errorDescription) const
{
    Q_UNUSED(note) // Maybe it'd be actually used in future

    if (userException.errorCode() == qevercloud::EDAMErrorCode::BAD_DATA_FORMAT) {
        errorDescription.setBase(
            QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                       "the attempt to get note"));

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Note.guid")) {
            errorDescription.appendBase(QT_TR_NOOP("note's guid is missing"));
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::PERMISSION_DENIED)
    {
        errorDescription.setBase(
            QT_TR_NOOP("PERMISSION_DENIED exception during "
                       "the attempt to get note"));

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Note")) {
            errorDescription.appendBase(
                QT_TR_NOOP("note is not owned by user"));
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    errorDescription.setBase(
        QT_TR_NOOP("Unexpected EDAM user exception "
                   "on attempt to get note"));
    errorDescription.details() = QStringLiteral("error code = ");
    errorDescription.details() += ToString(userException.errorCode());

    if (userException.parameter()) {
        errorDescription.details() += QStringLiteral("; parameter: ");
        errorDescription.details() += *userException.parameter();
    }

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<int>(userException.errorCode());
}

qint32 NoteStore::processEdamUserExceptionForGetResource(
    const qevercloud::Resource & resource,
    const qevercloud::EDAMUserException & userException,
    ErrorString & errorDescription) const
{
    Q_UNUSED(resource) // Maybe it'd be actually used in future

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT) {
        errorDescription.setBase(
            QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                       "the attempt to get resource"));

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Resource.guid")) {
            errorDescription.appendBase(
                QT_TR_NOOP("resource's guid is missing"));
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::PERMISSION_DENIED)
    {
        errorDescription.setBase(
            QT_TR_NOOP("PERMISSION_DENIED exception during "
                       "the attempt to get resource"));

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Resource")) {
            errorDescription.appendBase(
                QT_TR_NOOP("resource is not owned by user"));
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    errorDescription.setBase(QT_TR_NOOP(
        "Unexpected EDAM user exception on attempt to get resource"));
    errorDescription.details() = QStringLiteral("error code = ");
    errorDescription.details() += ToString(userException.errorCode());

    if (userException.parameter()) {
        errorDescription.details() += QStringLiteral("; parameter: ");
        errorDescription.details() += *userException.parameter();
    }

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<int>(userException.errorCode());
}

qint32 NoteStore::processEdamUserExceptionForNotebook(
    const qevercloud::Notebook & notebook,
    const qevercloud::EDAMUserException & userException,
    const NoteStore::UserExceptionSource & source,
    ErrorString & errorDescription) const
{
    bool thrownOnCreation = (source == UserExceptionSource::Creation);

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to create a notebook"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to update a notebook"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Notebook.name")) {
            if (notebook.name()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid length or pattern of notebook's name"));
                errorDescription.details() = *notebook.name();
            }
            else {
                errorDescription.appendBase(QT_TR_NOOP("notebook has no name"));
            }
        }
        else if (
            *userException.parameter() == QStringLiteral("Notebook.stack")) {
            if (notebook.stack()) {
                errorDescription.appendBase(QT_TR_NOOP(
                    "invalid length or pattern of notebook's stack"));
                errorDescription.details() = *notebook.stack();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("notebook has no stack"));
            }
        }
        else if (
            *userException.parameter() == QStringLiteral("Publishing.uri")) {
            if (notebook.publishing() && notebook.publishing()->uri()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid publishing uri "
                               "for notebook"));
                errorDescription.details() = *notebook.publishing()->uri();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("notebook has no publishing uri"));
            }
        }
        else if (
            *userException.parameter() ==
            QStringLiteral("Publishing.publicDescription"))
        {
            if (notebook.publishing() &&
                notebook.publishing()->publicDescription()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("public description for notebook is too long"));
                errorDescription.details() =
                    *notebook.publishing()->publicDescription();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("notebook has no public description"));
            }
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() == qevercloud::EDAMErrorCode::DATA_CONFLICT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to create a notebook"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to update a notebook"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Notebook.name")) {
            if (notebook.name()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("notebook's name is already in use"));
                errorDescription.details() = *notebook.name();
            }
            else {
                errorDescription.appendBase(QT_TR_NOOP("notebook has no name"));
            }
        }
        else if (
            userException.parameter() == QStringLiteral("Publishing.uri")) {
            if (notebook.publishing() && notebook.publishing()->uri()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("notebook's publishing uri is already in use"));
                errorDescription.details() = *notebook.publishing()->uri();
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("notebook has no publishing uri"));
            }
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (thrownOnCreation &&
        (userException.errorCode() == qevercloud::EDAMErrorCode::LIMIT_REACHED))
    {
        errorDescription.setBase(
            QT_TR_NOOP("LIMIT_REACHED exception during "
                       "the attempt to create notebook"));

        if (userException.parameter() &&
            (*userException.parameter() == QStringLiteral("Notebook")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("already at max number of "
                           "notebooks, please remove some of them"));
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    return processUnexpectedEdamUserException(
        QStringLiteral("notebook"), userException, source, errorDescription);
}

qint32 NoteStore::processEdamUserExceptionForNote(
    const qevercloud::Note & note,
    const qevercloud::EDAMUserException & userException,
    const NoteStore::UserExceptionSource & source,
    ErrorString & errorDescription) const
{
    bool thrownOnCreation = (source == UserExceptionSource::Creation);

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::BAD_DATA_FORMAT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to create a note"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("BAD_DATA_FORMAT exception during "
                           "the attempt to update a note"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Note.title")) {
            if (note.title()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid length or pattern of note's title"));
                errorDescription.details() = *note.title();
            }
            else {
                errorDescription.appendBase(QT_TR_NOOP("note has no title"));
            }
        }
        else if (
            *userException.parameter() == QStringLiteral("Note.content")) {
            if (note.content()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid length for note's ENML content"));

                errorDescription.details() =
                    QString::number(note.content()->length());

                QNWARNING(
                    "synchronization:note_store",
                    errorDescription << ", note's content: " << *note.content());
            }
            else {
                errorDescription.appendBase(QT_TR_NOOP("note has no content"));
            }
        }
        else if (userException.parameter()->startsWith(
                     QStringLiteral("NoteAttributes.")))
        {
            if (note.attributes()) {
                errorDescription.appendBase(
                    QT_TR_NOOP("invalid note attributes"));

                QNWARNING(
                    "synchronization:note_store",
                    errorDescription << ": " << *note.attributes());
            }
            else {
                errorDescription.appendBase(
                    QT_TR_NOOP("note has no attributes"));
            }
        }
        else if (userException.parameter()->startsWith(
                     QStringLiteral("ResourceAttributes.")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("invalid resource attributes "
                           "for some of note's resources"));

            QNWARNING(
                "synchronization:note_store",
                errorDescription << ", note: " << note);
        }
        else if (
            *userException.parameter() == QStringLiteral("Resource.mime")) {
            errorDescription.appendBase(
                QT_TR_NOOP("invalid mime type for some of note's resources"));

            QNWARNING(
                "synchronization:note_store",
                errorDescription << ", note: " << note);
        }
        else if (*userException.parameter() == QStringLiteral("Tag.name")) {
            errorDescription.appendBase(
                QT_TR_NOOP("Note.tagNames was provided "
                           "and one of the specified tags "
                           "had invalid length or pattern"));

            QNWARNING(
                "synchronization:note_store",
                errorDescription << ", note: " << note);
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() == qevercloud::EDAMErrorCode::DATA_CONFLICT) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to create a note"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_CONFLICT exception during "
                           "the attempt to update a note"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Note.deleted")) {
            errorDescription.appendBase(
                QT_TR_NOOP("deletion timestamp is set on active note"));
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() == qevercloud::EDAMErrorCode::DATA_REQUIRED) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_REQUIRED exception during "
                           "the attempt to create a note"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("DATA_REQUIRED exception during "
                           "the attempt to update a note"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() == QStringLiteral("Resource.data")) {
            errorDescription.appendBase(
                QT_TR_NOOP("data body for some of note's "
                           "resources is missing"));

            QNWARNING(
                "synchronization:note_store",
                errorDescription << ", note: " << note);
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::ENML_VALIDATION) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("ENML_VALIDATION exception during "
                           "the attempt to create a note"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("ENML_VALIDATION exception during "
                           "the attempt to update a note"));
        }

        errorDescription.appendBase(
            QT_TR_NOOP("note's content doesn't validate against DTD"));

        QNWARNING(
            "synchronization:note_store",
            errorDescription << ", note: " << note);
        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() == qevercloud::EDAMErrorCode::LIMIT_REACHED) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("LIMIT_REACHED exception during "
                           "the attempt to create a note"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("LIMIT_REACHED exception during "
                           "the attempt to update a note"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (thrownOnCreation &&
            (*userException.parameter() == QStringLiteral("Note")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("already at maximum number of notes per account"));
        }
        else if (*userException.parameter() == QStringLiteral("Note.size")) {
            errorDescription.appendBase(
                QT_TR_NOOP("total note size is too large"));
        }
        else if (
            *userException.parameter() == QStringLiteral("Note.resources")) {
            errorDescription.appendBase(
                QT_TR_NOOP("too many resources on note"));
        }
        else if (
            *userException.parameter() == QStringLiteral("Note.tagGuids")) {
            errorDescription.appendBase(QT_TR_NOOP("too many tags on note"));
        }
        else if (
            *userException.parameter() ==
            QStringLiteral("Resource.data.size")) {
            errorDescription.appendBase(
                QT_TR_NOOP("one of note's resource's data is too large"));
        }
        else if (userException.parameter()->startsWith(
                     QStringLiteral("NoteAttribute.")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("note attributes string is too large"));
            if (note.attributes()) {
                QNWARNING(
                    "synchronization:note_store",
                    errorDescription << ", note attributes: "
                                     << *note.attributes());
            }
        }
        else if (userException.parameter()->startsWith(
                     QStringLiteral("ResourceAttribute.")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("one of note's resources has too large resource "
                           "attributes string"));

            QNWARNING(
                "synchronization:note_store",
                errorDescription << ", note: " << note);
        }
        else if (*userException.parameter() == QStringLiteral("Tag")) {
            errorDescription.appendBase(
                QT_TR_NOOP("Note.tagNames was provided, and the required new "
                           "tags would exceed the maximum number per account"));

            QNWARNING(
                "synchronization:note_store",
                errorDescription << ", note: " << note);
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() ==
        qevercloud::EDAMErrorCode::PERMISSION_DENIED)
    {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("PERMISSION_DENIED exception during "
                           "the attempt to create a note"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("PERMISSION_DENIED exception during "
                           "the attempt to update a note"));
        }

        if (!userException.parameter()) {
            errorDescription.details() =
                QString::fromUtf8(userException.what());

            // FIXME: should actually return properly typed
            // qevercloud::EDAMErrorCode
            return static_cast<int>(userException.errorCode());
        }

        if (*userException.parameter() ==
            QStringLiteral("Note.notebookGuid")) {
            errorDescription.appendBase(
                QT_TR_NOOP("note's notebook is not owned by user"));
            if (note.notebookGuid()) {
                QNWARNING(
                    "synchronization:note_store",
                    errorDescription << ", notebook guid: "
                                     << *note.notebookGuid());
            }
        }
        else if (
            !thrownOnCreation &&
            (*userException.parameter() == QStringLiteral("Note")))
        {
            errorDescription.appendBase(
                QT_TR_NOOP("note is not owned by user"));
        }
        else {
            errorDescription.appendBase(QT_TR_NOOP("unexpected parameter"));
            errorDescription.details() = *userException.parameter();
        }

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    if (userException.errorCode() == qevercloud::EDAMErrorCode::QUOTA_REACHED) {
        if (thrownOnCreation) {
            errorDescription.setBase(
                QT_TR_NOOP("QUOTA_REACHED exception during "
                           "the attempt to create a note"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("QUOTA_REACHED exception during "
                           "the attempt to update a note"));
        }

        errorDescription.appendBase(
            QT_TR_NOOP("note exceeds upload quota limit"));

        // FIXME: should actually return properly typed
        // qevercloud::EDAMErrorCode
        return static_cast<int>(userException.errorCode());
    }

    return processUnexpectedEdamUserException(
        QStringLiteral("note"), userException, source, errorDescription);
}

qint32 NoteStore::processUnexpectedEdamUserException(
    const QString & typeName,
    const qevercloud::EDAMUserException & userException,
    const NoteStore::UserExceptionSource & source,
    ErrorString & errorDescription) const
{
    bool thrownOnCreation = (source == UserExceptionSource::Creation);

    if (thrownOnCreation) {
        errorDescription.setBase(
            QT_TR_NOOP("Unexpected EDAM user exception on "
                       "attempt to create data element"));
    }
    else {
        errorDescription.setBase(
            QT_TR_NOOP("Unexpected EDAM user exception on "
                       "attempt to update data element"));
    }

    errorDescription.details() = typeName;
    errorDescription.details() += QStringLiteral(", error code = ");
    errorDescription.details() += ToString(userException.errorCode());

    if (userException.parameter()) {
        errorDescription.details() += QStringLiteral(", parameter = ");
        errorDescription.details() += *userException.parameter();
    }

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<int>(userException.errorCode());
}

qint32 NoteStore::processEdamSystemException(
    const qevercloud::EDAMSystemException & systemException,
    ErrorString & errorDescription, qint32 & rateLimitSeconds) const
{
    rateLimitSeconds = -1;

    if (systemException.errorCode() ==
        qevercloud::EDAMErrorCode::RATE_LIMIT_REACHED) {
        if (!systemException.rateLimitDuration()) {
            errorDescription.setBase(
                QT_TR_NOOP("Evernote API rate limit exceeded but no rate limit "
                           "duration is available"));
        }
        else {
            errorDescription.setBase(
                QT_TR_NOOP("Evernote API rate limit exceeded, retry in"));
            errorDescription.details() =
                QString::number(*systemException.rateLimitDuration());
            errorDescription.details() += QStringLiteral(" sec");
            rateLimitSeconds = *systemException.rateLimitDuration();
        }
    }
    else {
        errorDescription.setBase(QT_TR_NOOP("Caught EDAM system exception"));
        errorDescription.details() = QStringLiteral("error code = ");
        errorDescription.details() += ToString(systemException.errorCode());

        if (systemException.message() &&
            !systemException.message()->isEmpty()) {
            errorDescription.details() += QStringLiteral(": ");
            errorDescription.details() += *systemException.message();
        }
    }

    // FIXME: should actually return properly typed qevercloud::EDAMErrorCode
    return static_cast<int>(systemException.errorCode());
}

void NoteStore::processEdamNotFoundException(
    const qevercloud::EDAMNotFoundException & notFoundException,
    ErrorString & errorDescription) const
{
    errorDescription.setBase(
        QT_TR_NOOP("Note store could not find data element"));

    if (notFoundException.identifier() &&
        !notFoundException.identifier()->isEmpty())
    {
        errorDescription.details() = *notFoundException.identifier();
    }

    if (notFoundException.key() && !notFoundException.key()->isEmpty()) {
        errorDescription.details() = *notFoundException.key();
    }
}

void NoteStore::processNextPendingGetNoteAsyncRequest()
{
    QNDEBUG(
        "synchronization:note_store",
        "NoteStore"
            << "::processNextPendingGetNoteAsyncRequest");

    if (m_pendingGetNoteRequests.isEmpty()) {
        QNDEBUG("synchronization:note_store", "No pending get note request");
        return;
    }

    QNDEBUG(
        "synchronization:note_store",
        "Queue of pending get note async "
            << "requests is not empty, executing the next pending request");

    auto request = m_pendingGetNoteRequests.dequeue();
    ErrorString errorDescription;

    bool res = getNoteAsync(
        request.m_withContent, request.m_withResourceData,
        request.m_withResourcesRecognition, request.m_withResourceAlternateData,
        request.m_withSharedNotes, request.m_withNoteAppDataValues,
        request.m_withResourceAppDataValues, request.m_withNoteLimits,
        request.m_guid, request.m_authToken, errorDescription);
    if (Q_UNLIKELY(!res)) {
        auto errorCode = static_cast<int>(qevercloud::EDAMErrorCode::UNKNOWN);

        qevercloud::Note note;
        note.setGuid(request.m_guid);

        Q_EMIT getNoteAsyncFinished(errorCode, note, -1, errorDescription);

        processNextPendingGetNoteAsyncRequest();
        return;
    }

    QNDEBUG(
        "synchronization:note_store",
        "Queue of pending get note async "
            << "requests now contains " << m_pendingGetNoteRequests.size()
            << " items");
}

} // namespace quentier
