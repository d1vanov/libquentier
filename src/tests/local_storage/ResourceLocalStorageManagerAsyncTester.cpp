/*
 * Copyright 2016 Dmitry Ivanov
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

#include "ResourceLocalStorageManagerAsyncTester.h"
#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/logging/QuentierLogger.h>
#include <QThread>

namespace quentier {
namespace test {

ResourceLocalStorageManagerAsyncTester::ResourceLocalStorageManagerAsyncTester(QObject * parent) :
    QObject(parent),
    m_state(STATE_UNINITIALIZED),
    m_pLocalStorageManagerAsync(Q_NULLPTR),
    m_pLocalStorageManagerThread(Q_NULLPTR),
    m_notebook(),
    m_note(),
    m_initialResource(),
    m_foundResource(),
    m_modifiedResource()
{}

ResourceLocalStorageManagerAsyncTester::~ResourceLocalStorageManagerAsyncTester()
{
    clear();
}

void ResourceLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("ResourceLocalStorageManagerAsyncTester");
    qint32 userId = 6;
    bool startFromScratch = true;
    bool overrideLock = false;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    Account account(username, Account::Type::Evernote, userId);
    m_pLocalStorageManagerAsync = new LocalStorageManagerAsync(account, startFromScratch, overrideLock);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->start();
}

void ResourceLocalStorageManagerAsyncTester::onWorkerInitialized()
{
    m_notebook = Notebook();
    m_notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    m_notebook.setUpdateSequenceNumber(1);
    m_notebook.setName(QStringLiteral("Fake notebook name"));
    m_notebook.setCreationTimestamp(1);
    m_notebook.setModificationTimestamp(1);
    m_notebook.setDefaultNotebook(true);
    m_notebook.setLastUsed(false);
    m_notebook.setPublishingUri(QStringLiteral("Fake publishing uri"));
    m_notebook.setPublishingOrder(1);
    m_notebook.setPublishingAscending(true);
    m_notebook.setPublishingPublicDescription(QStringLiteral("Fake public description"));
    m_notebook.setPublished(true);
    m_notebook.setStack(QStringLiteral("Fake notebook stack"));
    m_notebook.setBusinessNotebookDescription(QStringLiteral("Fake business notebook description"));
    m_notebook.setBusinessNotebookPrivilegeLevel(1);
    m_notebook.setBusinessNotebookRecommended(true);

    ErrorString errorDescription;
    if (!m_notebook.checkParameters(errorDescription)) {
        QNWARNING(QStringLiteral("Found invalid notebook: ") << m_notebook << QStringLiteral(", error: ") << errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_ADD_NOTEBOOK_REQUEST;
    Q_EMIT addNotebookRequest(m_notebook);
}

void ResourceLocalStorageManagerAsyncTester::onAddNotebookCompleted(Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE() \
    else { \
        errorDescription.setBase("Internal error in ResourceLocalStorageManagerAsyncTester: found wrong state"); \
        QNWARNING(errorDescription << ": " << m_state); \
        Q_EMIT failure(errorDescription.nonLocalizedString()); \
    }

    if (m_state == STATE_SENT_ADD_NOTEBOOK_REQUEST)
    {
        if (m_notebook != notebook) {
            errorDescription.setBase("Internal error in ResourceLocalStorageManagerAsyncTester: "
                                     "notebook in onAddNotebookCompleted slot doesn't match the original Notebook");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note = Note();
        m_note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
        m_note.setUpdateSequenceNumber(1);
        m_note.setTitle(QStringLiteral("Fake note"));
        m_note.setContent(QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));
        m_note.setCreationTimestamp(1);
        m_note.setModificationTimestamp(1);
        m_note.setNotebookGuid(m_notebook.guid());
        m_note.setNotebookLocalUid(m_notebook.localUid());
        m_note.setActive(true);

        m_state = STATE_SENT_ADD_NOTE_REQUEST;
        Q_EMIT addNoteRequest(m_note);
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onAddNotebookFailed(Notebook notebook, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", Notebook: ") << notebook);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onAddNoteCompleted(Note note, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_NOTE_REQUEST)
    {
        if (m_note != note) {
            errorDescription.setBase("Internal error in ResourceLocalStorageManagerAsyncTester: "
                                     "note in onAddNoteCompleted slot doesn't match the original Note");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_initialResource.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
        m_initialResource.setUpdateSequenceNumber(1);
        if (note.hasGuid()) {
            m_initialResource.setNoteGuid(note.guid());
        }
        if (!note.localUid().isEmpty()) {
            m_initialResource.setNoteLocalUid(note.localUid());
        }
        m_initialResource.setIndexInNote(0);
        m_initialResource.setDataBody(QByteArray("Fake resource data body"));
        m_initialResource.setDataSize(m_initialResource.dataBody().size());
        m_initialResource.setDataHash(QByteArray("Fake hash      1"));

        m_initialResource.setRecognitionDataBody(QByteArray("<recoIndex docType=\"handwritten\" objType=\"image\" objID=\"fc83e58282d8059be17debabb69be900\" "
                                                            "engineVersion=\"5.5.22.7\" recoType=\"service\" lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
                                                            "<item x=\"437\" y=\"589\" w=\"1415\" h=\"190\">"
                                                            "<t w=\"87\">EVER ?</t>"
                                                            "<t w=\"83\">EVER NOTE</t>"
                                                            "<t w=\"82\">EVERNOTE</t>"
                                                            "<t w=\"71\">EVER NaTE</t>"
                                                            "<t w=\"67\">EVER nine</t>"
                                                            "<t w=\"67\">EVER none</t>"
                                                            "<t w=\"66\">EVER not</t>"
                                                            "<t w=\"62\">over NOTE</t>"
                                                            "<t w=\"62\">even NOTE</t>"
                                                            "<t w=\"61\">EVER nose</t>"
                                                            "<t w=\"50\">EV£RNoTE</t>"
                                                            "</item>"
                                                            "<item x=\"1850\" y=\"1465\" w=\"14\" h=\"12\">"
                                                            "<t w=\"11\">et</t>"
                                                            "<t w=\"10\">TQ</t>"
                                                            "</item>"
                                                            "</recoIndex>"));
        m_initialResource.setRecognitionDataSize(m_initialResource.recognitionDataBody().size());
        m_initialResource.setRecognitionDataHash(QByteArray("Fake hash      2"));

        m_initialResource.setMime(QStringLiteral("text/plain"));
        m_initialResource.setWidth(1);
        m_initialResource.setHeight(1);

        qevercloud::ResourceAttributes & attributes = m_initialResource.resourceAttributes();

        attributes.sourceURL = QStringLiteral("Fake resource source URL");
        attributes.timestamp = 1;
        attributes.latitude = 0.0;
        attributes.longitude = 38.0;
        attributes.altitude = 12.0;
        attributes.cameraMake = QStringLiteral("Fake resource camera make");
        attributes.cameraModel = QStringLiteral("Fake resource camera model");
        attributes.fileName = QStringLiteral("Fake resource file name");

        attributes.applicationData = qevercloud::LazyMap();
        qevercloud::LazyMap & appData = attributes.applicationData.ref();
        appData.keysOnly = QSet<QString>();
        auto & keysOnly = appData.keysOnly.ref();
        keysOnly.reserve(3);
        keysOnly.insert(QStringLiteral("key_1"));
        keysOnly.insert(QStringLiteral("key_2"));
        keysOnly.insert(QStringLiteral("key_3"));

        appData.fullMap = QMap<QString, QString>();
        auto & fullMap = appData.fullMap.ref();
        fullMap = QMap<QString, QString>();
        fullMap[QStringLiteral("key_1")] = QStringLiteral("value_1");
        fullMap[QStringLiteral("key_2")] = QStringLiteral("value_2");
        fullMap[QStringLiteral("key_3")] = QStringLiteral("value_3");

        m_state = STATE_SENT_ADD_REQUEST;
        Q_EMIT addResourceRequest(m_initialResource);
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onAddNoteFailed(Note note, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", Note: ") << note);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onGetResourceCountCompleted(int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST)
    {
        if (count != 1) {
            errorDescription.setBase("GetResourceCount returned result different from the expected one (1)");
            errorDescription.details() = QString::number(count);
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_EXPUNGE_REQUEST;
        Q_EMIT expungeResourceRequest(m_modifiedResource);
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST)
    {
        if (count != 0) {
            errorDescription.setBase("GetResourceCount returned result different from the expected one (0)");
            errorDescription.details() = QString::number(count);
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        Q_EMIT success();
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onGetResourceCountFailed(ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onAddResourceCompleted(Resource resource, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;
    if (m_state == STATE_SENT_ADD_REQUEST)
    {
        if (m_initialResource != resource) {
            errorDescription.setBase("Internal error in ResourceLocalStorageManagerAsyncTester: "
                                     "resource in onAddResourceCompleted doesn't match the original Resource");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundResource.clear();
        m_foundResource.setLocalUid(m_initialResource.localUid());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;
        bool withBinaryData = true;
        Q_EMIT findResourceRequest(m_foundResource, withBinaryData);
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onAddResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", Resource: ") << resource);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onUpdateResourceCompleted(Resource resource, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST)
    {
        if (m_modifiedResource != resource) {
            errorDescription.setBase("Internal error in ResourceLocalStorageManagerAsyncTester: "
                                     "resource in onUpdateResourceCompleted doesn't match the original Resource");
            QNWARNING(errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundResource.clear();
        m_foundResource.setLocalUid(m_modifiedResource.localUid());

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;
        bool withBinaryData = false;    // test find without binary data, for a change
        Q_EMIT findResourceRequest(m_foundResource, withBinaryData);
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onUpdateResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", Resource: ") << resource);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onFindResourceCompleted(Resource resource,
                                                                     bool withBinaryData, QUuid requestId)
{
    Q_UNUSED(requestId)
    Q_UNUSED(withBinaryData)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST)
    {
        if (resource != m_initialResource) {
            errorDescription.setBase("Added and found resources in local storage don't match");
            QNWARNING(errorDescription << QStringLiteral(": Resource added to LocalStorageManager: ") << m_initialResource
                      << QStringLiteral("\nResource found in LocalStorageManager: ") << resource);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found resource is good, updating it now
        m_modifiedResource = m_initialResource;
        m_modifiedResource.setUpdateSequenceNumber(m_initialResource.updateSequenceNumber() + 1);
        m_modifiedResource.setHeight(m_initialResource.height() + 1);
        m_modifiedResource.setWidth(m_initialResource.width() + 1);

        qevercloud::ResourceAttributes & attributes = m_modifiedResource.resourceAttributes();
        attributes.cameraMake.ref() += QStringLiteral("_modified");
        attributes.cameraModel.ref() += QStringLiteral("_modified");

        m_state = STATE_SENT_UPDATE_REQUEST;
        Q_EMIT updateResourceRequest(m_modifiedResource);
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST)
    {
        // Find after update was called without binary data, so need to remove it from modified
        // resource prior to the comparison
        if (m_modifiedResource.hasDataBody()) {
            m_modifiedResource.setDataBody(QByteArray());
        }

        if (m_modifiedResource.hasRecognitionDataBody()) {
            m_modifiedResource.setRecognitionDataBody(QByteArray());
        }

        if (resource != m_modifiedResource) {
            errorDescription.setBase("Updated and found resources in local storage don't match");
            QNWARNING(errorDescription << QStringLiteral(": Resource updated in LocalStorageManager: ") << m_modifiedResource
                      << QStringLiteral("\nResource found in LocalStorageManager: ") << resource);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getResourceCountRequest();
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST)
    {
        errorDescription.setBase("Found resource which should have been expunged from local storage");
        QNWARNING(errorDescription << QStringLiteral(": Resource expunged from LocalStorageManager: ") << m_modifiedResource
                  << QStringLiteral("\nResource fond in LocalStorageManager: ") << resource);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onFindResourceFailed(Resource resource,
                                                                  bool withBinaryData, ErrorString errorDescription, QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getResourceCountRequest();
        return;
    }

    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", Resource: ") << resource
              << QStringLiteral(", withBinaryData = ") << (withBinaryData ? QStringLiteral("true") : QStringLiteral("false")));
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onExpungeResourceCompleted(Resource resource, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedResource != resource) {
        errorDescription.setBase("Internal error in ResourceLocalStorageManagerAsyncTester: "
                                 "resource in onExpungeResourceCompleted slot doesn't match "
                                 "the original expunged Resource");
        QNWARNING(errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;
    bool withBinaryData = true;
    Q_EMIT findResourceRequest(m_foundResource, withBinaryData);
}

void ResourceLocalStorageManagerAsyncTester::onExpungeResourceFailed(Resource resource, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(errorDescription << QStringLiteral(", requestId = ") << requestId << QStringLiteral(", Resource: ") << resource);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(m_pLocalStorageManagerThread, QNSIGNAL(QThread,finished),
                     m_pLocalStorageManagerThread, QNSLOT(QThread,deleteLater));

    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,initialized),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onWorkerInitialized));

    // Request --> slot connections
    QObject::connect(this, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,addNotebookRequest,Notebook,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNotebookRequest,Notebook,QUuid));
    QObject::connect(this, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,addNoteRequest,Note,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddNoteRequest,Note,QUuid));
    QObject::connect(this, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,addResourceRequest,Resource,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onAddResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,updateResourceRequest,Resource,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onUpdateResourceRequest,Resource,QUuid));
    QObject::connect(this, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,findResourceRequest,Resource,bool,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onFindResourceRequest,Resource,bool,QUuid));
    QObject::connect(this, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,getResourceCountRequest,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onGetResourceCountRequest,QUuid));
    QObject::connect(this, QNSIGNAL(ResourceLocalStorageManagerAsyncTester,expungeResourceRequest,Resource,QUuid),
                     m_pLocalStorageManagerAsync, QNSLOT(LocalStorageManagerAsync,onExpungeResourceRequest,Resource,QUuid));

    // Slot <-- result connections
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookComplete,Notebook,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onAddNotebookCompleted,Notebook,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNotebookFailed,Notebook,ErrorString,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onAddNotebookFailed,Notebook,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteComplete,Note,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onAddNoteCompleted,Note,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addNoteFailed,Note,ErrorString,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onAddNoteFailed,Note,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceComplete,Resource,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onAddResourceCompleted,Resource,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,addResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onAddResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceComplete,Resource,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onUpdateResourceCompleted,Resource,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,updateResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onUpdateResourceFailed,Resource,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceComplete,Resource,bool,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onFindResourceCompleted,Resource,bool,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,findResourceFailed,Resource,bool,ErrorString,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onFindResourceFailed,Resource,bool,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,getResourceCountComplete,int,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onGetResourceCountCompleted,int,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,getResourceCountFailed,ErrorString,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onGetResourceCountFailed,ErrorString,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeResourceComplete,Resource,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onExpungeResourceCompleted,Resource,QUuid));
    QObject::connect(m_pLocalStorageManagerAsync, QNSIGNAL(LocalStorageManagerAsync,expungeResourceFailed,Resource,ErrorString,QUuid),
                     this, QNSLOT(ResourceLocalStorageManagerAsyncTester,onExpungeResourceFailed,Resource,ErrorString,QUuid));
}

void ResourceLocalStorageManagerAsyncTester::clear()
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

} // namespace test
} // namespace quentier
