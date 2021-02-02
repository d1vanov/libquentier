/*
 * Copyright 2016-2021 Dmitry Ivanov
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

ResourceLocalStorageManagerAsyncTester::ResourceLocalStorageManagerAsyncTester(
    QObject * parent) :
    QObject(parent)
{}

ResourceLocalStorageManagerAsyncTester::
    ~ResourceLocalStorageManagerAsyncTester()
{
    clear();
}

void ResourceLocalStorageManagerAsyncTester::onInitTestCase()
{
    QString username = QStringLiteral("ResourceLocalStorageManagerAsyncTester");
    qint32 userId = 6;

    clear();

    m_pLocalStorageManagerThread = new QThread(this);
    const Account account{username, Account::Type::Evernote, userId};

    const LocalStorageManager::StartupOptions startupOptions(
        LocalStorageManager::StartupOption::ClearDatabase);

    m_pLocalStorageManagerAsync =
        new LocalStorageManagerAsync(account, startupOptions);

    createConnections();

    m_pLocalStorageManagerAsync->init();
    m_pLocalStorageManagerAsync->moveToThread(m_pLocalStorageManagerThread);

    m_pLocalStorageManagerThread->setObjectName(QStringLiteral(
        "ResourceLocalStorageManagerAsyncTester-local-storage-thread"));

    m_pLocalStorageManagerThread->start();
}

void ResourceLocalStorageManagerAsyncTester::initialize()
{
    m_notebook.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000047"));
    m_notebook.setUpdateSequenceNum(1);
    m_notebook.setName(QStringLiteral("Fake notebook name"));
    m_notebook.setServiceCreated(1);
    m_notebook.setServiceUpdated(1);
    m_notebook.setDefaultNotebook(true);
    m_notebook.setPublishing(qevercloud::Publishing{});

    m_notebook.mutablePublishing()->setUri(
        QStringLiteral("Fake publishing uri"));

    m_notebook.mutablePublishing()->setOrder(
        qevercloud::NoteSortOrder::CREATED);

    m_notebook.mutablePublishing()->setAscending(true);

    m_notebook.mutablePublishing()->setPublicDescription(
        QStringLiteral("Fake public description"));

    m_notebook.setPublished(true);
    m_notebook.setStack(QStringLiteral("Fake notebook stack"));

    m_notebook.setBusinessNotebook(qevercloud::BusinessNotebook{});

    m_notebook.mutableBusinessNotebook()->setNotebookDescription(
        QStringLiteral("Fake business notebook description"));

    m_notebook.mutableBusinessNotebook()->setPrivilege(
        qevercloud::SharedNotebookPrivilegeLevel::FULL_ACCESS);

    m_notebook.mutableBusinessNotebook()->setRecommended(true);

    m_state = STATE_SENT_ADD_NOTEBOOK_REQUEST;
    Q_EMIT addNotebookRequest(m_notebook, QUuid::createUuid());
}

void ResourceLocalStorageManagerAsyncTester::onAddNotebookCompleted(
    qevercloud::Notebook notebook, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

#define HANDLE_WRONG_STATE()                                                   \
    else {                                                                     \
        errorDescription.setBase(                                              \
            "Internal error in "                                               \
            "ResourceLocalStorageManagerAsyncTester: "                         \
            "found wrong state");                                              \
        QNWARNING("tests:local_storage", errorDescription << ": " << m_state); \
        Q_EMIT failure(errorDescription.nonLocalizedString());                 \
    }

    if (m_state == STATE_SENT_ADD_NOTEBOOK_REQUEST) {
        if (m_notebook != notebook) {
            errorDescription.setBase(
                "Internal error in ResourceLocalStorageManagerAsyncTester: "
                "notebook in onAddNotebookCompleted slot "
                "doesn't match the original Notebook");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_note.setGuid(QStringLiteral("00000000-0000-0000-c000-000000000048"));
        m_note.setUpdateSequenceNum(1);
        m_note.setTitle(QStringLiteral("Fake note"));

        m_note.setContent(
            QStringLiteral("<en-note><h1>Hello, world</h1></en-note>"));

        m_note.setCreated(1);
        m_note.setUpdated(1);
        m_note.setNotebookGuid(m_notebook.guid());
        m_note.setNotebookLocalId(m_notebook.localId());
        m_note.setActive(true);

        m_state = STATE_SENT_ADD_NOTE_REQUEST;
        Q_EMIT addNoteRequest(m_note, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onAddNotebookFailed(
    qevercloud::Notebook notebook, ErrorString errorDescription,
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Notebook: " << notebook);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onAddNoteCompleted(
    qevercloud::Note note, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_ADD_NOTE_REQUEST) {
        if (m_note != note) {
            errorDescription.setBase(
                "Internal error in ResourceLocalStorageManagerAsyncTester: "
                "note in onAddNoteCompleted slot doesn't "
                "match the original Note");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_initialResource.setGuid(
            QStringLiteral("00000000-0000-0000-c000-000000000048"));

        m_initialResource.setUpdateSequenceNum(1);

        if (note.guid()) {
            m_initialResource.setNoteGuid(note.guid());
        }

        if (!note.localId().isEmpty()) {
            m_initialResource.setNoteLocalId(note.localId());
        }

        m_initialResource.setIndexInNote(0);
        m_initialResource.setData(qevercloud::Data{});

        m_initialResource.mutableData()->setBody(
            QByteArray("Fake resource data body"));

        m_initialResource.mutableData()->setSize(
            m_initialResource.data()->body()->size());

        m_initialResource.mutableData()->setBodyHash(
            QByteArray("Fake hash      1"));

        m_initialResource.setRecognition(qevercloud::Data{});
        m_initialResource.mutableRecognition()->setBody(
            QByteArray("<recoIndex docType=\"handwritten\" objType=\"image\" "
                       "objID=\"fc83e58282d8059be17debabb69be900\" "
                       "engineVersion=\"5.5.22.7\" recoType=\"service\" "
                       "lang=\"en\" objWidth=\"2398\" objHeight=\"1798\"> "
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

        m_initialResource.mutableRecognition()->setSize(
            m_initialResource.recognition()->body()->size());

        m_initialResource.mutableRecognition()->setBodyHash(
            QByteArray("Fake hash      2"));

        m_initialResource.setMime(QStringLiteral("text/plain"));
        m_initialResource.setWidth(1);
        m_initialResource.setHeight(1);

        m_initialResource.setAttributes(qevercloud::ResourceAttributes{});
        auto & attributes = *m_initialResource.mutableAttributes();

        attributes.setSourceURL(QStringLiteral("Fake resource source URL"));
        attributes.setTimestamp(1);
        attributes.setLatitude(0.0);
        attributes.setLongitude(38.0);
        attributes.setAltitude(12.0);
        attributes.setCameraMake(QStringLiteral("Fake resource camera make"));
        attributes.setCameraModel(QStringLiteral("Fake resource camera model"));
        attributes.setFileName(QStringLiteral("Fake resource file name"));

        attributes.setApplicationData(qevercloud::LazyMap{});
        qevercloud::LazyMap & appData = *attributes.mutableApplicationData();
        appData.setKeysOnly(QSet<QString>{});
        auto & keysOnly = *appData.mutableKeysOnly();
        keysOnly.reserve(3);
        keysOnly.insert(QStringLiteral("key_1"));
        keysOnly.insert(QStringLiteral("key_2"));
        keysOnly.insert(QStringLiteral("key_3"));

        appData.setFullMap(QMap<QString, QString>{});
        auto & fullMap = *appData.mutableFullMap();
        fullMap = QMap<QString, QString>();
        fullMap[QStringLiteral("key_1")] = QStringLiteral("value_1");
        fullMap[QStringLiteral("key_2")] = QStringLiteral("value_2");
        fullMap[QStringLiteral("key_3")] = QStringLiteral("value_3");

        m_state = STATE_SENT_ADD_REQUEST;
        Q_EMIT addResourceRequest(m_initialResource, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onAddNoteFailed(
    qevercloud::Note note, ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Note: " << note);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onGetResourceCountCompleted(
    int count, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST) {
        if (count != 1) {
            errorDescription.setBase(
                "GetResourceCount returned result different "
                "from the expected one (1)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_EXPUNGE_REQUEST;
        Q_EMIT expungeResourceRequest(m_modifiedResource, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST) {
        if (count != 0) {
            errorDescription.setBase(
                "GetResourceCount returned result different "
                "from the expected one (0)");

            errorDescription.details() = QString::number(count);
            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        Q_EMIT success();
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onGetResourceCountFailed(
    ErrorString errorDescription, QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onAddResourceCompleted(
    qevercloud::Resource resource, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;
    if (m_state == STATE_SENT_ADD_REQUEST) {
        if (m_initialResource != resource) {
            errorDescription.setBase(
                "Internal error in ResourceLocalStorageManagerAsyncTester: "
                "resource in onAddResourceCompleted doesn't "
                "match the original Resource");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundResource = qevercloud::Resource{};
        m_foundResource.setLocalId(m_initialResource.localId());

        m_state = STATE_SENT_FIND_AFTER_ADD_REQUEST;

        LocalStorageManager::GetResourceOptions options(
            LocalStorageManager::GetResourceOption::WithBinaryData);

        Q_EMIT findResourceRequest(
            m_foundResource, options, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onAddResourceFailed(
    qevercloud::Resource resource, ErrorString errorDescription,
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Resource: " << resource);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onUpdateResourceCompleted(
    qevercloud::Resource resource, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_UPDATE_REQUEST) {
        if (m_modifiedResource != resource) {
            errorDescription.setBase(
                "Internal error in ResourceLocalStorageManagerAsyncTester: "
                "resource in onUpdateResourceCompleted "
                "doesn't match the original Resource");

            QNWARNING("tests:local_storage", errorDescription);
            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_foundResource = qevercloud::Resource{};
        m_foundResource.setLocalId(m_modifiedResource.localId());

        m_state = STATE_SENT_FIND_AFTER_UPDATE_REQUEST;

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
        LocalStorageManager::GetResourceOptions options;
#else
        LocalStorageManager::GetResourceOptions options(0);
#endif

        Q_EMIT findResourceRequest(
            m_foundResource, options, QUuid::createUuid());
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onUpdateResourceFailed(
    qevercloud::Resource resource, ErrorString errorDescription,
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Resource: " << resource);

    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onFindResourceCompleted(
    qevercloud::Resource resource,
    LocalStorageManager::GetResourceOptions options, QUuid requestId)
{
    Q_UNUSED(requestId)
    Q_UNUSED(options)

    ErrorString errorDescription;

    if (m_state == STATE_SENT_FIND_AFTER_ADD_REQUEST) {
        if (resource != m_initialResource) {
            errorDescription.setBase(
                "Added and found resources in the local "
                "storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": Resource added to the local storage: "
                    << m_initialResource
                    << "\nResource found in the local storage: " << resource);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        // Ok, found resource is good, updating it now
        m_modifiedResource = m_initialResource;

        m_modifiedResource.setUpdateSequenceNum(
            m_initialResource.updateSequenceNum().value() + 1);

        m_modifiedResource.setHeight(m_initialResource.height().value() + 1);
        m_modifiedResource.setWidth(m_initialResource.width().value() + 1);

        auto & attributes = m_modifiedResource.mutableAttributes().value();

        attributes.setCameraMake(
            attributes.cameraMake().value() + QStringLiteral("_modified"));

        attributes.setCameraModel(
            attributes.cameraModel().value() + QStringLiteral("_modified"));

        m_state = STATE_SENT_UPDATE_REQUEST;
        Q_EMIT updateResourceRequest(m_modifiedResource, QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_UPDATE_REQUEST) {
        // Find after update was called without binary data, so need to remove
        // it from the modified resource prior to the comparison
        if (m_modifiedResource.data() && m_modifiedResource.data()->body()) {
            m_modifiedResource.mutableData()->setBody(std::nullopt);
        }

        if (resource != m_modifiedResource) {
            errorDescription.setBase(
                "Updated and found resources in the local storage don't match");

            QNWARNING(
                "tests:local_storage",
                errorDescription
                    << ": Resource updated in the local storage: "
                    << m_modifiedResource
                    << "\nResource found in the local storage: " << resource);

            Q_EMIT failure(errorDescription.nonLocalizedString());
            return;
        }

        m_state = STATE_SENT_GET_COUNT_AFTER_UPDATE_REQUEST;
        Q_EMIT getResourceCountRequest(QUuid::createUuid());
    }
    else if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        errorDescription.setBase(
            "Found resource which should have been expunged "
            "from the local storage");

        QNWARNING(
            "tests:local_storage",
            errorDescription
                << ": Resource expunged from the local storage: "
                << m_modifiedResource
                << "\nResource fond in the local storage: " << resource);

        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }
    HANDLE_WRONG_STATE();
}

void ResourceLocalStorageManagerAsyncTester::onFindResourceFailed(
    qevercloud::Resource resource,
    LocalStorageManager::GetResourceOptions options,
    ErrorString errorDescription, QUuid requestId)
{
    if (m_state == STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST) {
        m_state = STATE_SENT_GET_COUNT_AFTER_EXPUNGE_REQUEST;
        Q_EMIT getResourceCountRequest(QUuid::createUuid());
        return;
    }

    QNWARNING(
        "tests:local_storage",
        errorDescription
            << ", requestId = " << requestId << ", Resource: " << resource
            << ", withBinaryData = "
            << ((options &
                 LocalStorageManager::GetResourceOption::WithBinaryData)
                    ? "true"
                    : "false"));
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::onExpungeResourceCompleted(
    qevercloud::Resource resource, QUuid requestId)
{
    Q_UNUSED(requestId)

    ErrorString errorDescription;

    if (m_modifiedResource != resource) {
        errorDescription.setBase(
            "Internal error in ResourceLocalStorageManagerAsyncTester: "
            "resource in onExpungeResourceCompleted slot "
            "doesn't match the original expunged Resource");

        QNWARNING("tests:local_storage", errorDescription);
        Q_EMIT failure(errorDescription.nonLocalizedString());
        return;
    }

    m_state = STATE_SENT_FIND_AFTER_EXPUNGE_REQUEST;

    LocalStorageManager::GetResourceOptions options(
        LocalStorageManager::GetResourceOption::WithBinaryData);

    Q_EMIT findResourceRequest(m_foundResource, options, QUuid::createUuid());
}

void ResourceLocalStorageManagerAsyncTester::onExpungeResourceFailed(
    qevercloud::Resource resource, ErrorString errorDescription,
    QUuid requestId)
{
    QNWARNING(
        "tests:local_storage",
        errorDescription << ", requestId = " << requestId
                         << ", Resource: " << resource);
    Q_EMIT failure(errorDescription.nonLocalizedString());
}

void ResourceLocalStorageManagerAsyncTester::createConnections()
{
    QObject::connect(
        m_pLocalStorageManagerThread, &QThread::finished,
        m_pLocalStorageManagerThread, &QThread::deleteLater);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::initialized,
        this, &ResourceLocalStorageManagerAsyncTester::initialize);

    // Request --> slot connections
    QObject::connect(
        this, &ResourceLocalStorageManagerAsyncTester::addNotebookRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNotebookRequest);

    QObject::connect(
        this, &ResourceLocalStorageManagerAsyncTester::addNoteRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddNoteRequest);

    QObject::connect(
        this, &ResourceLocalStorageManagerAsyncTester::addResourceRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onAddResourceRequest);

    QObject::connect(
        this, &ResourceLocalStorageManagerAsyncTester::updateResourceRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onUpdateResourceRequest);

    QObject::connect(
        this, &ResourceLocalStorageManagerAsyncTester::findResourceRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onFindResourceRequest);

    QObject::connect(
        this, &ResourceLocalStorageManagerAsyncTester::getResourceCountRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onGetResourceCountRequest);

    QObject::connect(
        this, &ResourceLocalStorageManagerAsyncTester::expungeResourceRequest,
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::onExpungeResourceRequest);

    // Slot <-- result connections
    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookComplete, this,
        &ResourceLocalStorageManagerAsyncTester::onAddNotebookCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addNotebookFailed, this,
        &ResourceLocalStorageManagerAsyncTester::onAddNotebookFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteComplete,
        this, &ResourceLocalStorageManagerAsyncTester::onAddNoteCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync, &LocalStorageManagerAsync::addNoteFailed,
        this, &ResourceLocalStorageManagerAsyncTester::onAddNoteFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addResourceComplete, this,
        &ResourceLocalStorageManagerAsyncTester::onAddResourceCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::addResourceFailed, this,
        &ResourceLocalStorageManagerAsyncTester::onAddResourceFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceComplete, this,
        &ResourceLocalStorageManagerAsyncTester::onUpdateResourceCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::updateResourceFailed, this,
        &ResourceLocalStorageManagerAsyncTester::onUpdateResourceFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceComplete, this,
        &ResourceLocalStorageManagerAsyncTester::onFindResourceCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::findResourceFailed, this,
        &ResourceLocalStorageManagerAsyncTester::onFindResourceFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getResourceCountComplete, this,
        &ResourceLocalStorageManagerAsyncTester::onGetResourceCountCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::getResourceCountFailed, this,
        &ResourceLocalStorageManagerAsyncTester::onGetResourceCountFailed);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeResourceComplete, this,
        &ResourceLocalStorageManagerAsyncTester::onExpungeResourceCompleted);

    QObject::connect(
        m_pLocalStorageManagerAsync,
        &LocalStorageManagerAsync::expungeResourceFailed, this,
        &ResourceLocalStorageManagerAsyncTester::onExpungeResourceFailed);
}

void ResourceLocalStorageManagerAsyncTester::clear()
{
    if (m_pLocalStorageManagerThread) {
        m_pLocalStorageManagerThread->quit();
        m_pLocalStorageManagerThread->wait();
        m_pLocalStorageManagerThread->deleteLater();
        m_pLocalStorageManagerThread = nullptr;
    }

    if (m_pLocalStorageManagerAsync) {
        m_pLocalStorageManagerAsync->deleteLater();
        m_pLocalStorageManagerAsync = nullptr;
    }

    m_state = STATE_UNINITIALIZED;
}

} // namespace test
} // namespace quentier
