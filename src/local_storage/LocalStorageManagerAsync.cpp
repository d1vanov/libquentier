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

#include <quentier/local_storage/LocalStorageManagerAsync.h>
#include <quentier/local_storage/NoteSearchQuery.h>
#include <quentier/logging/QuentierLogger.h>
#include <quentier/utility/SysInfo.h>

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QMetaMethod>
#endif

namespace quentier {

namespace {

/**
 * Removes dataBody and alternateDataBody from note's resources and returns
 * resources containing dataBody and/or alternateDataBody within a separate list
 * in order to cache them separately from notes
 */
void splitNoteAndResourcesForCaching(Note & note, QList<Resource> & resources)
{
    resources = note.resources();
    QList<Resource> noteResources = resources;
    for(auto it = noteResources.begin(), end = noteResources.end(); it != end; ++it) {
        Resource & resource = *it;
        resource.setDataBody(QByteArray());
        resource.setAlternateDataBody(QByteArray());
    }
    note.setResources(noteResources);
}

}

LocalStorageManagerAsync::LocalStorageManagerAsync(const Account & account, const bool startFromScratch,
                                                   const bool overrideLock, QObject * parent) :
    QObject(parent),
    m_account(account),
    m_startFromScratch(startFromScratch),
    m_overrideLock(overrideLock),
    m_useCache(true),
    m_pLocalStorageManager(Q_NULLPTR),
    m_pLocalStorageCacheManager(Q_NULLPTR)
{}

LocalStorageManagerAsync::~LocalStorageManagerAsync()
{
    if (m_pLocalStorageCacheManager) {
        delete m_pLocalStorageCacheManager;
    }

    if (m_pLocalStorageManager) {
        delete m_pLocalStorageManager;
    }
}

void LocalStorageManagerAsync::setUseCache(const bool useCache)
{
    if (m_useCache) {
        // Cache is being disabled - no point to store things in it anymore, it would get rotten pretty quick
        m_pLocalStorageCacheManager->clear();
    }

    m_useCache = useCache;
}

const LocalStorageCacheManager * LocalStorageManagerAsync::localStorageCacheManager() const
{
    if (!m_useCache) {
        return Q_NULLPTR;
    }
    else {
        return m_pLocalStorageCacheManager;
    }
}

bool LocalStorageManagerAsync::installCacheExpiryFunction(const ILocalStorageCacheExpiryChecker & checker)
{
    if (m_useCache && m_pLocalStorageCacheManager) {
        m_pLocalStorageCacheManager->installCacheExpiryFunction(checker);
        return true;
    }

    return false;
}

const LocalStorageManager * LocalStorageManagerAsync::localStorageManager() const
{
    return m_pLocalStorageManager;
}

LocalStorageManager * LocalStorageManagerAsync::localStorageManager()
{
    return m_pLocalStorageManager;
}

void LocalStorageManagerAsync::init()
{
    if (m_pLocalStorageManager) {
        delete m_pLocalStorageManager;
    }

    m_pLocalStorageManager = new LocalStorageManager(m_account, m_startFromScratch, m_overrideLock);

    if (m_pLocalStorageCacheManager) {
        delete m_pLocalStorageCacheManager;
    }

    m_pLocalStorageCacheManager = new LocalStorageCacheManager();

    Q_EMIT initialized();
}

void LocalStorageManagerAsync::onGetUserCountRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->userCount(errorDescription);
        if (count < 0) {
            Q_EMIT getUserCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getUserCountComplete(count, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get user count from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getUserCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onSwitchUserRequest(Account account, bool startFromScratch, QUuid requestId)
{
    try
    {
        m_pLocalStorageManager->switchUser(account, startFromScratch);
    }
    catch(const std::exception & e)
    {
        ErrorString errorDescription(QT_TR_NOOP("Can't switch user in the local storage: caught exception"));
        errorDescription.details() = QString::fromUtf8(e.what());

        if (m_useCache) {
            m_pLocalStorageCacheManager->clear();
        }

        Q_EMIT switchUserFailed(account, errorDescription, requestId);
        return;
    }

    if (m_useCache) {
        m_pLocalStorageCacheManager->clear();
    }

    Q_EMIT switchUserComplete(account, requestId);
}

void LocalStorageManagerAsync::onAddUserRequest(User user, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->addUser(user, errorDescription);
        if (!res) {
            Q_EMIT addUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT addUserComplete(user, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't add user to the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT addUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateUserRequest(User user, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->updateUser(user, errorDescription);
        if (!res) {
            Q_EMIT updateUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT updateUserComplete(user, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't update user within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT updateUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindUserRequest(User user, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->findUser(user, errorDescription);
        if (!res) {
            Q_EMIT findUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT findUserComplete(user, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find user in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onDeleteUserRequest(User user, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->deleteUser(user, errorDescription);
        if (!res) {
            Q_EMIT deleteUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT deleteUserComplete(user, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't mark user as deleted in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT deleteUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeUserRequest(User user, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->expungeUser(user, errorDescription);
        if (!res) {
            Q_EMIT expungeUserFailed(user, errorDescription, requestId);
            return;
        }

        Q_EMIT expungeUserComplete(user, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge user from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeUserFailed(user, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetNotebookCountRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->notebookCount(errorDescription);
        if (count < 0) {
            Q_EMIT getNotebookCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getNotebookCountComplete(count, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get notebook count from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getNotebookCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->addNotebook(notebook, errorDescription);
        if (!res) {
            Q_EMIT addNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheNotebook(notebook);
        }

        Q_EMIT addNotebookComplete(notebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't add notebook to the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT addNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->updateNotebook(notebook, errorDescription);
        if (!res) {
            Q_EMIT updateNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheNotebook(notebook);
        }

        Q_EMIT updateNotebookComplete(notebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't update notebook in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT updateNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool foundNotebookInCache = false;
        if (m_useCache)
        {
            bool notebookHasGuid = notebook.hasGuid();
            if (notebookHasGuid || !notebook.localUid().isEmpty())
            {
                const QString uid = (notebookHasGuid ? notebook.guid() : notebook.localUid());
                LocalStorageCacheManager::WhichUid wg = (notebookHasGuid ? LocalStorageCacheManager::Guid : LocalStorageCacheManager::LocalUid);

                const Notebook * pNotebook = m_pLocalStorageCacheManager->findNotebook(uid, wg);
                if (pNotebook) {
                    notebook = *pNotebook;
                    foundNotebookInCache = true;
                }
            }
            else if (notebook.hasName() && !notebook.name().isEmpty())
            {
                const QString notebookName = notebook.name();
                const Notebook * pNotebook = m_pLocalStorageCacheManager->findNotebookByName(notebookName);
                if (pNotebook) {
                    notebook = *pNotebook;
                    foundNotebookInCache = true;
                }
            }
        }

        if (!foundNotebookInCache)
        {
            bool res = m_pLocalStorageManager->findNotebook(notebook, errorDescription);
            if (!res) {
                Q_EMIT findNotebookFailed(notebook, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findNotebookComplete(notebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find notebook within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindDefaultNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->findDefaultNotebook(notebook, errorDescription);
        if (!res) {
            Q_EMIT findDefaultNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        Q_EMIT findDefaultNotebookComplete(notebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find the default notebook within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findDefaultNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindLastUsedNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->findLastUsedNotebook(notebook, errorDescription);
        if (!res) {
            Q_EMIT findLastUsedNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        Q_EMIT findLastUsedNotebookComplete(notebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find the last used notebook within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findLastUsedNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindDefaultOrLastUsedNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->findDefaultOrLastUsedNotebook(notebook, errorDescription);
        if (!res) {
            Q_EMIT findDefaultOrLastUsedNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        Q_EMIT findDefaultOrLastUsedNotebookComplete(notebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find default or last used notebook within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findDefaultOrLastUsedNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllNotebooksRequest(size_t limit, size_t offset,
                                                         LocalStorageManager::ListNotebooksOrder::type order,
                                                         LocalStorageManager::OrderDirection::type orderDirection,
                                                         QString linkedNotebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<Notebook> notebooks = m_pLocalStorageManager->listAllNotebooks(errorDescription, limit, offset, order,
                                                                             orderDirection, linkedNotebookGuid);
        if (notebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllNotebooksFailed(limit, offset, order, orderDirection, linkedNotebookGuid,
                                        errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            const int numNotebooks = notebooks.size();
            for(int i = 0; i < numNotebooks; ++i) {
                const Notebook & notebook = notebooks[i];
                m_pLocalStorageCacheManager->cacheNotebook(notebook);
            }
        }

        Q_EMIT listAllNotebooksComplete(limit, offset, order, orderDirection, linkedNotebookGuid, notebooks, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list all notebooks from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listAllNotebooksFailed(limit, offset, order, orderDirection, linkedNotebookGuid,
                                      error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllSharedNotebooksRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<SharedNotebook> sharedNotebooks = m_pLocalStorageManager->listAllSharedNotebooks(errorDescription);
        if (sharedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllSharedNotebooksFailed(errorDescription, requestId);
            return;
        }

        Q_EMIT listAllSharedNotebooksComplete(sharedNotebooks, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list all shared notebooks from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listAllSharedNotebooksFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotebooksRequest(LocalStorageManager::ListObjectsOptions flag,
                                                      size_t limit, size_t offset,
                                                      LocalStorageManager::ListNotebooksOrder::type order,
                                                      LocalStorageManager::OrderDirection::type orderDirection,
                                                      QString linkedNotebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<Notebook> notebooks = m_pLocalStorageManager->listNotebooks(flag, errorDescription, limit,
                                                                          offset, order, orderDirection,
                                                                          linkedNotebookGuid);
        if (notebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotebooksFailed(flag, limit, offset, order, orderDirection, linkedNotebookGuid,
                                       errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            const int numNotebooks = notebooks.size();
            for(int i = 0; i < numNotebooks; ++i) {
                const Notebook & notebook = notebooks[i];
                m_pLocalStorageCacheManager->cacheNotebook(notebook);
            }
        }

        Q_EMIT listNotebooksComplete(flag, limit, offset, order, orderDirection, linkedNotebookGuid, notebooks, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list notebooks from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listNotebooksFailed(flag, limit, offset, order, orderDirection, linkedNotebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onListSharedNotebooksPerNotebookGuidRequest(QString notebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<SharedNotebook> sharedNotebooks = m_pLocalStorageManager->listSharedNotebooksPerNotebookGuid(notebookGuid, errorDescription);
        if (sharedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listSharedNotebooksPerNotebookGuidFailed(notebookGuid, errorDescription, requestId);
            return;
        }

        Q_EMIT listSharedNotebooksPerNotebookGuidComplete(notebookGuid, sharedNotebooks, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list shared notebooks by notebook guid from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listSharedNotebooksPerNotebookGuidFailed(notebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->expungeNotebook(notebook, errorDescription);
        if (!res) {
            Q_EMIT expungeNotebookFailed(notebook, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->expungeNotebook(notebook);
        }

        Q_EMIT expungeNotebookComplete(notebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge notebook from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeNotebookFailed(notebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetLinkedNotebookCountRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->linkedNotebookCount(errorDescription);
        if (count < 0) {
            Q_EMIT getLinkedNotebookCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getLinkedNotebookCountComplete(count, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get linked notebook count from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getLinkedNotebookCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->addLinkedNotebook(linkedNotebook, errorDescription);
        if (!res) {
            Q_EMIT addLinkedNotebookFailed(linkedNotebook, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheLinkedNotebook(linkedNotebook);
        }

        Q_EMIT addLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't add linked notebook to the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT addLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->updateLinkedNotebook(linkedNotebook, errorDescription);
        if (!res) {
            Q_EMIT updateLinkedNotebookFailed(linkedNotebook, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheLinkedNotebook(linkedNotebook);
        }

        Q_EMIT updateLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't update linked notebook in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT updateLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool foundLinkedNotebookInCache = false;
        if (m_useCache && linkedNotebook.hasGuid())
        {
            const QString guid = linkedNotebook.guid();
            const LinkedNotebook * pLinkedNotebook = m_pLocalStorageCacheManager->findLinkedNotebook(guid);
            if (pLinkedNotebook) {
                linkedNotebook = *pLinkedNotebook;
                foundLinkedNotebookInCache = true;
            }
        }

        if (!foundLinkedNotebookInCache)
        {
            bool res = m_pLocalStorageManager->findLinkedNotebook(linkedNotebook, errorDescription);
            if (!res) {
                Q_EMIT findLinkedNotebookFailed(linkedNotebook, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find linked notebook within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllLinkedNotebooksRequest(size_t limit, size_t offset,
                                                               LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                                               LocalStorageManager::OrderDirection::type orderDirection,
                                                               QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<LinkedNotebook> linkedNotebooks = m_pLocalStorageManager->listAllLinkedNotebooks(errorDescription, limit,
                                                                                               offset, order, orderDirection);
        if (linkedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllLinkedNotebooksFailed(limit, offset, order, orderDirection, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            const int numLinkedNotebooks = linkedNotebooks.size();
            for(int i = 0; i < numLinkedNotebooks; ++i) {
                const LinkedNotebook & linkedNotebook = linkedNotebooks[i];
                m_pLocalStorageCacheManager->cacheLinkedNotebook(linkedNotebook);
            }
        }

        Q_EMIT listAllLinkedNotebooksComplete(limit, offset, order, orderDirection, linkedNotebooks, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list all linked notebooks from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listAllLinkedNotebooksFailed(limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListLinkedNotebooksRequest(LocalStorageManager::ListObjectsOptions flag,
                                                            size_t limit, size_t offset,
                                                            LocalStorageManager::ListLinkedNotebooksOrder::type order,
                                                            LocalStorageManager::OrderDirection::type orderDirection,
                                                            QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<LinkedNotebook> linkedNotebooks = m_pLocalStorageManager->listLinkedNotebooks(flag, errorDescription, limit,
                                                                                            offset, order, orderDirection);
        if (linkedNotebooks.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listLinkedNotebooksFailed(flag, limit, offset, order, orderDirection, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            const int numLinkedNotebooks = linkedNotebooks.size();
            for(int i = 0; i < numLinkedNotebooks; ++i) {
                const LinkedNotebook & linkedNotebook = linkedNotebooks[i];
                m_pLocalStorageCacheManager->cacheLinkedNotebook(linkedNotebook);
            }
        }

        Q_EMIT listLinkedNotebooksComplete(flag, limit, offset, order, orderDirection, linkedNotebooks, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list linked notebooks from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listLinkedNotebooksFailed(flag, limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeLinkedNotebookRequest(LinkedNotebook linkedNotebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->expungeLinkedNotebook(linkedNotebook, errorDescription);
        if (!res) {
            Q_EMIT expungeLinkedNotebookFailed(linkedNotebook, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->expungeLinkedNotebook(linkedNotebook);
        }

        Q_EMIT expungeLinkedNotebookComplete(linkedNotebook, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge linked notebook from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeLinkedNotebookFailed(linkedNotebook, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->noteCount(errorDescription);
        if (count < 0) {
            Q_EMIT getNoteCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getNoteCountComplete(count, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get note count from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getNoteCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountPerNotebookRequest(Notebook notebook, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->noteCountPerNotebook(notebook, errorDescription);
        if (count < 0) {
            Q_EMIT getNoteCountPerNotebookFailed(errorDescription, notebook, requestId);
        }
        else {
            Q_EMIT getNoteCountPerNotebookComplete(count, notebook, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get note count per notebook from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getNoteCountPerNotebookFailed(error, notebook, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountPerTagRequest(Tag tag, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->noteCountPerTag(tag, errorDescription);
        if (count < 0) {
            Q_EMIT getNoteCountPerTagFailed(errorDescription, tag, requestId);
        }
        else {
            Q_EMIT getNoteCountPerTagComplete(count, tag, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get note count per tag from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getNoteCountPerTagFailed(error, tag, requestId);
    }
}

void LocalStorageManagerAsync::onGetNoteCountsPerAllTagsRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QHash<QString, int> noteCountsPerTagLocalUid;
        bool res = m_pLocalStorageManager->noteCountsPerAllTags(noteCountsPerTagLocalUid, errorDescription);
        if (!res) {
            Q_EMIT getNoteCountsPerAllTagsFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getNoteCountsPerAllTagsComplete(noteCountsPerTagLocalUid, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get note counts per all tags from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getNoteCountsPerAllTagsFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddNoteRequest(Note note, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->addNote(note, errorDescription);
        if (!res) {
            Q_EMIT addNoteFailed(note, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            Note noteForCaching = note;
            QList<Resource> resourcesForCaching;
            splitNoteAndResourcesForCaching(noteForCaching, resourcesForCaching);

            m_pLocalStorageCacheManager->cacheNote(noteForCaching);
            for(auto it = resourcesForCaching.constBegin(), end = resourcesForCaching.constEnd(); it != end; ++it) {
                m_pLocalStorageCacheManager->cacheResource(*it);
            }
        }

        Q_EMIT addNoteComplete(note, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't add note to the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT addNoteFailed(note, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateNoteRequest(Note note, const LocalStorageManager::UpdateNoteOptions options,
                                                   QUuid requestId)
{
    try
    {
        bool shouldCheckForNotebookChange = false;
        bool shouldCheckForTagListUpdate = false;

#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
        static const QMetaMethod noteMovedToAnotherNotebookSignal = QMetaMethod::fromSignal(&LocalStorageManagerAsync::noteMovedToAnotherNotebook);
        if (isSignalConnected(noteMovedToAnotherNotebookSignal)) {
            shouldCheckForNotebookChange = true;
        }

        if (options & LocalStorageManager::UpdateNoteOption::UpdateTags)
        {
            static const QMetaMethod noteTagListChangedSignal = QMetaMethod::fromSignal(&LocalStorageManagerAsync::noteTagListChanged);
            if (isSignalConnected(noteTagListChangedSignal)) {
                shouldCheckForTagListUpdate = true;
            }
        }
#else
        if (receivers(SIGNAL(noteMovedToAnotherNotebook(QString,QString,QString))) > 0) {
            shouldCheckForNotebookChange = true;
        }

        if ((options & LocalStorageManager::UpdateNoteOption::UpdateTags) &&
            (receivers(SIGNAL(noteTagListChanged(QString,QStringList,QStringList))) > 0))
        {
            shouldCheckForTagListUpdate = true;
        }
#endif

        Note previousNoteVersion;
        if (shouldCheckForNotebookChange || shouldCheckForTagListUpdate)
        {
            bool foundNoteInCache = false;
            if (m_useCache)
            {
                bool noteHasGuid = note.hasGuid();
                const QString uid = (noteHasGuid ? note.guid() : note.localUid());
                LocalStorageCacheManager::WhichUid wu = (noteHasGuid
                                                         ? LocalStorageCacheManager::Guid
                                                         : LocalStorageCacheManager::LocalUid);
                const Note * pNote = m_pLocalStorageCacheManager->findNote(uid, wu);
                if (pNote) {
                    previousNoteVersion = *pNote;
                    foundNoteInCache = true;
                }
            }

            if (!foundNoteInCache)
            {
                ErrorString errorDescription;
                bool res = m_pLocalStorageManager->findNote(previousNoteVersion, errorDescription,
                                                            /* with resource metadata = */ false,
                                                            /* with resource binary data = */ false);
                if (!res) {
                    Q_EMIT updateNoteFailed(note, options, errorDescription, requestId);
                    return;
                }
            }
        }

        ErrorString errorDescription;
        bool res = m_pLocalStorageManager->updateNote(note, options, errorDescription);
        if (!res) {
            Q_EMIT updateNoteFailed(note, options, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            if ((options & LocalStorageManager::UpdateNoteOption::UpdateResourceMetadata) &&
                (options & LocalStorageManager::UpdateNoteOption::UpdateTags))
            {
                Note noteForCaching = note;
                QList<Resource> resourcesForCaching;
                splitNoteAndResourcesForCaching(noteForCaching, resourcesForCaching);
                m_pLocalStorageCacheManager->cacheNote(noteForCaching);

                if (options & LocalStorageManager::UpdateNoteOption::UpdateResourceBinaryData)
                {
                    for(auto it = resourcesForCaching.constBegin(), end = resourcesForCaching.constEnd(); it != end; ++it) {
                        m_pLocalStorageCacheManager->cacheResource(*it);
                    }
                }
                else
                {
                    // Since resources metadata might have changed, it would become stale within the cache so need to remove it from there
                    for(auto it = resourcesForCaching.constBegin(), end = resourcesForCaching.constEnd(); it != end; ++it) {
                        m_pLocalStorageCacheManager->expungeResource(*it);
                    }
                }
            }
            else
            {
                // The note was somehow changed but the resources or tags information was not updated =>
                // the note in the cache is stale/incomplete in either case, need to remove it from there
                m_pLocalStorageCacheManager->expungeNote(note);

                // Same goes for its resources
                QList<Resource> resources = note.resources();
                for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it) {
                    m_pLocalStorageCacheManager->expungeResource(*it);
                }
            }
        }

        Q_EMIT updateNoteComplete(note, options, requestId);

        if (shouldCheckForNotebookChange)
        {
            bool notebookChanged = false;
            if (note.hasNotebookGuid() && previousNoteVersion.hasNotebookGuid()) {
                notebookChanged = (note.notebookGuid() != previousNoteVersion.notebookGuid());
            }
            else {
                notebookChanged = (note.notebookLocalUid() != previousNoteVersion.notebookLocalUid());
            }

            if (notebookChanged) {
                QNDEBUG(QStringLiteral("Notebook change detected for note ") << note.localUid()
                        << QStringLiteral(": moved from notebook ") << previousNoteVersion.notebookLocalUid()
                        << QStringLiteral(" to notebook ") << note.notebookLocalUid());
                Q_EMIT noteMovedToAnotherNotebook(note.localUid(), previousNoteVersion.notebookLocalUid(),
                                                  note.notebookLocalUid());
            }
        }

        if (shouldCheckForTagListUpdate)
        {
            const QStringList & previousTagLocalUids = previousNoteVersion.tagLocalUids();
            const QStringList & updatedTagLocalUids = note.tagLocalUids();

            bool tagListUpdated = (previousTagLocalUids.size() != updatedTagLocalUids.size());
            if (!tagListUpdated)
            {
                for(auto it = previousTagLocalUids.constBegin(),
                    end = previousTagLocalUids.constEnd(); it != end; ++it)
                {
                    const QString & prevTagLocalUid = *it;
                    int index = updatedTagLocalUids.indexOf(prevTagLocalUid);
                    if (index < 0) {
                        tagListUpdated = true;
                        break;
                    }
                }
            }

            if (tagListUpdated) {
                QNDEBUG(QStringLiteral("Tags list update detected for note ") << note.localUid()
                        << QStringLiteral(": previous tag local uids: ") << previousTagLocalUids.join(QStringLiteral(", "))
                        << QStringLiteral("; updated tag local uids: ") << updatedTagLocalUids.join(QStringLiteral(",")));
                Q_EMIT noteTagListChanged(note.localUid(), previousTagLocalUids, updatedTagLocalUids);
            }
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't update note in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT updateNoteFailed(note, options, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindNoteRequest(Note note, bool withResourceMetadata, bool withResourceBinaryData, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool foundNoteInCache = false;
        if (m_useCache)
        {
            bool noteHasGuid = note.hasGuid();
            const QString uid = (noteHasGuid ? note.guid() : note.localUid());
            LocalStorageCacheManager::WhichUid wu = (noteHasGuid
                                                     ? LocalStorageCacheManager::Guid
                                                     : LocalStorageCacheManager::LocalUid);

            const Note * pNote = m_pLocalStorageCacheManager->findNote(uid, wu);
            if (pNote)
            {
                note = *pNote;
                foundNoteInCache = true;

                if (withResourceBinaryData)
                {
                    QList<Resource> resources = note.resources();
                    for(auto it = resources.begin(), end = resources.end(); it != end; ++it)
                    {
                        Resource & resource = *it;

                        bool resourceHasGuid = resource.hasGuid();
                        const QString resourceUid = (resourceHasGuid ? resource.guid() : resource.localUid());
                        LocalStorageCacheManager::WhichUid rwu = (resourceHasGuid ? LocalStorageCacheManager::Guid : LocalStorageCacheManager::LocalUid);

                        const Resource * pResource = m_pLocalStorageCacheManager->findResource(resourceUid, rwu);
                        if (pResource)
                        {
                            resource = *pResource;
                        }
                        else
                        {
                            bool res = m_pLocalStorageManager->findEnResource(resource, errorDescription, /* with resource binary data = */ true);
                            if (!res) {
                                Q_EMIT findNoteFailed(note, withResourceMetadata, withResourceBinaryData, errorDescription, requestId);
                                return;
                            }
                        }
                    }

                    note.setResources(resources);
                }
            }
        }

        if (!foundNoteInCache)
        {
            bool res = m_pLocalStorageManager->findNote(note, errorDescription, withResourceMetadata, withResourceBinaryData);
            if (!res) {
                Q_EMIT findNoteFailed(note, withResourceMetadata, withResourceBinaryData, errorDescription, requestId);
                return;
            }
        }

        if (!foundNoteInCache && m_useCache)
        {
            QList<Resource> resources = note.resources();
            for(auto it = resources.begin(), end = resources.end(); it != end; ++it) {
                Resource & resource = *it;
                resource.setDataBody(QByteArray());
                resource.setAlternateDataBody(QByteArray());
            }

            Note noteWithoutResourceBinaryData = note;
            noteWithoutResourceBinaryData.setResources(resources);
            m_pLocalStorageCacheManager->cacheNote(noteWithoutResourceBinaryData);
        }

        if (foundNoteInCache && !withResourceMetadata) {
            note.setResources(QList<Resource>());
        }

        Q_EMIT findNoteComplete(note, withResourceMetadata, withResourceBinaryData, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find note within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findNoteFailed(note, withResourceMetadata, withResourceBinaryData, error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotesPerNotebookRequest(Notebook notebook, bool withResourceMetadata,
                                                             bool withResourceBinaryData,
                                                             LocalStorageManager::ListObjectsOptions flag,
                                                             size_t limit, size_t offset,
                                                             LocalStorageManager::ListNotesOrder::type order,
                                                             LocalStorageManager::OrderDirection::type orderDirection,
                                                             QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        QList<Note> notes = m_pLocalStorageManager->listNotesPerNotebook(notebook, errorDescription,
                                                                         withResourceMetadata, withResourceBinaryData,
                                                                         flag, limit, offset, order, orderDirection);
        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesPerNotebookFailed(notebook, withResourceMetadata, withResourceBinaryData, flag,
                                              limit, offset, order, orderDirection, errorDescription, requestId);
            return;
        }

        if (m_useCache && withResourceMetadata && withResourceBinaryData)
        {
            const int numNotes = notes.size();
            for(int i = 0; i < numNotes; ++i) {
                const Note & note = notes[i];
                m_pLocalStorageCacheManager->cacheNote(note);
            }
        }

        Q_EMIT listNotesPerNotebookComplete(notebook, withResourceMetadata, withResourceBinaryData, flag, limit,
                                            offset, order, orderDirection, notes, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list notes per notebook from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listNotesPerNotebookFailed(notebook, withResourceMetadata, withResourceBinaryData, flag, limit, offset,
                                          order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotesPerTagRequest(Tag tag, bool withResourceMetadata, bool withResourceBinaryData,
                                                        LocalStorageManager::ListObjectsOptions flag,
                                                        size_t limit, size_t offset,
                                                        LocalStorageManager::ListNotesOrder::type order,
                                                        LocalStorageManager::OrderDirection::type orderDirection,
                                                        QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        QList<Note> notes = m_pLocalStorageManager->listNotesPerTag(tag, errorDescription, withResourceMetadata,
                                                                    withResourceBinaryData, flag,
                                                                    limit, offset, order, orderDirection);
        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesPerTagFailed(tag, withResourceMetadata, withResourceBinaryData, flag, limit, offset,
                                         order, orderDirection, errorDescription, requestId);
            return;
        }

        if (m_useCache && withResourceMetadata && withResourceBinaryData)
        {
            const int numNotes = notes.size();
            for(int i = 0; i < numNotes; ++i) {
                const Note & note = notes[i];
                m_pLocalStorageCacheManager->cacheNote(note);
            }
        }

        Q_EMIT listNotesPerTagComplete(tag, withResourceMetadata, withResourceBinaryData, flag, limit,
                                       offset, order, orderDirection, notes, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list notes per tag from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listNotesPerTagFailed(tag, withResourceMetadata, withResourceBinaryData, flag, limit, offset,
                                     order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListNotesRequest(LocalStorageManager::ListObjectsOptions flag, bool withResourceMetadata,
                                                  bool withResourceBinaryData, size_t limit, size_t offset,
                                                  LocalStorageManager::ListNotesOrder::type order,
                                                  LocalStorageManager::OrderDirection::type orderDirection,
                                                  QString linkedNotebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<Note> notes = m_pLocalStorageManager->listNotes(flag, errorDescription, withResourceMetadata, withResourceBinaryData,
                                                              limit, offset, order, orderDirection, linkedNotebookGuid);
        if (notes.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listNotesFailed(flag, withResourceMetadata, withResourceBinaryData, limit, offset, order,
                                   orderDirection, linkedNotebookGuid, errorDescription, requestId);
            return;
        }

        if (m_useCache && withResourceMetadata && withResourceBinaryData)
        {
            const int numNotes = notes.size();
            for(int i = 0; i < numNotes; ++i) {
                const Note & note = notes[i];
                m_pLocalStorageCacheManager->cacheNote(note);
            }
        }

        Q_EMIT listNotesComplete(flag, withResourceMetadata, withResourceBinaryData, limit, offset, order,
                                 orderDirection, linkedNotebookGuid, notes, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list notes from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listNotesFailed(flag, withResourceMetadata, withResourceBinaryData, limit, offset,
                               order, orderDirection, linkedNotebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindNoteLocalUidsWithSearchQuery(NoteSearchQuery noteSearchQuery, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QStringList noteLocalUids = m_pLocalStorageManager->findNoteLocalUidsWithSearchQuery(noteSearchQuery,
                                                                                             errorDescription);
        if (noteLocalUids.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT findNoteLocalUidsWithSearchQueryFailed(noteSearchQuery, errorDescription, requestId);
            return;
        }

        Q_EMIT findNoteLocalUidsWithSearchQueryComplete(noteLocalUids, noteSearchQuery, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find note local uids with search query within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findNoteLocalUidsWithSearchQueryFailed(noteSearchQuery, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeNoteRequest(Note note, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        QList<Resource> resources = note.resources();

        bool res = m_pLocalStorageManager->expungeNote(note, errorDescription);
        if (!res) {
            Q_EMIT expungeNoteFailed(note, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            m_pLocalStorageCacheManager->expungeNote(note);

            for(auto it = resources.constBegin(), end = resources.constEnd(); it != end; ++it) {
                m_pLocalStorageCacheManager->expungeResource(*it);
            }
        }

        Q_EMIT expungeNoteComplete(note, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge note from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeNoteFailed(note, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetTagCountRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->tagCount(errorDescription);
        if (count < 0) {
            Q_EMIT getTagCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getTagCountComplete(count, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get tag count from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getTagCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddTagRequest(Tag tag, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->addTag(tag, errorDescription);
        if (!res) {
            Q_EMIT addTagFailed(tag, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheTag(tag);
        }

        Q_EMIT addTagComplete(tag, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't add tag to the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT addTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateTagRequest(Tag tag, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->updateTag(tag, errorDescription);
        if (!res) {
            Q_EMIT updateTagFailed(tag, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheTag(tag);
        }

        Q_EMIT updateTagComplete(tag, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't update tag in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT updateTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindTagRequest(Tag tag, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool foundTagInCache = false;
        if (m_useCache)
        {
            bool tagHasGuid = tag.hasGuid();
            if (tagHasGuid || !tag.localUid().isEmpty())
            {
                const QString uid = (tagHasGuid ? tag.guid() : tag.localUid());
                LocalStorageCacheManager::WhichUid wg = (tagHasGuid ? LocalStorageCacheManager::Guid : LocalStorageCacheManager::LocalUid);

                const Tag * pTag = m_pLocalStorageCacheManager->findTag(uid, wg);
                if (pTag) {
                    tag = *pTag;
                    foundTagInCache = true;
                }
            }
            else if (tag.hasName() && !tag.name().isEmpty())
            {
                const QString tagName = tag.name();
                const Tag * pTag = m_pLocalStorageCacheManager->findTagByName(tagName);
                if (pTag) {
                    tag = *pTag;
                    foundTagInCache = true;
                }
            }
        }

        if (!foundTagInCache)
        {
            bool res = m_pLocalStorageManager->findTag(tag, errorDescription);
            if (!res) {
                Q_EMIT findTagFailed(tag, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findTagComplete(tag, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find tag within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllTagsPerNoteRequest(Note note, LocalStorageManager::ListObjectsOptions flag,
                                                           size_t limit, size_t offset,
                                                           LocalStorageManager::ListTagsOrder::type order,
                                                           LocalStorageManager::OrderDirection::type orderDirection,
                                                           QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        QList<Tag> tags = m_pLocalStorageManager->listAllTagsPerNote(note, errorDescription, flag, limit,
                                                                     offset, order, orderDirection);
        if (tags.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllTagsPerNoteFailed(note, flag, limit, offset, order,
                                          orderDirection, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            foreach(const Tag & tag, tags) {
                m_pLocalStorageCacheManager->cacheTag(tag);
            }
        }

        Q_EMIT listAllTagsPerNoteComplete(tags, note, flag, limit, offset, order, orderDirection, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list all tags per note from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listAllTagsPerNoteFailed(note, flag, limit, offset,
                                        order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllTagsRequest(size_t limit, size_t offset,
                                                    LocalStorageManager::ListTagsOrder::type order,
                                                    LocalStorageManager::OrderDirection::type orderDirection,
                                                    QString linkedNotebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        QList<Tag> tags = m_pLocalStorageManager->listAllTags(errorDescription, limit, offset,
                                                              order, orderDirection, linkedNotebookGuid);
        if (tags.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllTagsFailed(limit, offset, order, orderDirection, linkedNotebookGuid, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            const int numTags = tags.size();
            for(int i = 0; i < numTags; ++i) {
                const Tag & tag = tags[i];
                m_pLocalStorageCacheManager->cacheTag(tag);
            }
        }

        Q_EMIT listAllTagsComplete(limit, offset, order, orderDirection, linkedNotebookGuid, tags, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list all tags from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listAllTagsFailed(limit, offset, order, orderDirection, linkedNotebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onListTagsRequest(LocalStorageManager::ListObjectsOptions flag,
                                                 size_t limit, size_t offset,
                                                 LocalStorageManager::ListTagsOrder::type order,
                                                 LocalStorageManager::OrderDirection::type orderDirection,
                                                 QString linkedNotebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<Tag> tags = m_pLocalStorageManager->listTags(flag, errorDescription, limit, offset, order,
                                                           orderDirection, linkedNotebookGuid);
        if (tags.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listTagsFailed(flag, limit, offset, order, orderDirection, linkedNotebookGuid, errorDescription, requestId);
        }

        if (m_useCache)
        {
            for(auto it = tags.constBegin(), end = tags.constEnd(); it != end; ++it) {
                const Tag & tag = *it;
                m_pLocalStorageCacheManager->cacheTag(tag);
            }
        }

        Q_EMIT listTagsComplete(flag, limit, offset, order, orderDirection, linkedNotebookGuid, tags, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list tags from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listTagsFailed(flag, limit, offset, order, orderDirection, linkedNotebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onListTagsWithNoteLocalUidsRequest(LocalStorageManager::ListObjectsOptions flag,
                                                                  size_t limit, size_t offset,
                                                                  LocalStorageManager::ListTagsOrder::type order,
                                                                  LocalStorageManager::OrderDirection::type orderDirection,
                                                                  QString linkedNotebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<std::pair<Tag, QStringList> > tagsWithNoteLocalUids = m_pLocalStorageManager->listTagsWithNoteLocalUids(flag, errorDescription,
                                                                                                                      limit, offset, order,
                                                                                                                      orderDirection, linkedNotebookGuid);
        if (tagsWithNoteLocalUids.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listTagsWithNoteLocalUidsFailed(flag, limit, offset, order, orderDirection, linkedNotebookGuid, errorDescription, requestId);
        }

        if (m_useCache)
        {
            for(auto it = tagsWithNoteLocalUids.constBegin(), end = tagsWithNoteLocalUids.constEnd(); it != end; ++it) {
                const Tag & tag = it->first;
                m_pLocalStorageCacheManager->cacheTag(tag);
            }
        }

        Q_EMIT listTagsWithNoteLocalUidsComplete(flag, limit, offset, order, orderDirection, linkedNotebookGuid, tagsWithNoteLocalUids, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list tags with note local uids from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listTagsWithNoteLocalUidsFailed(flag, limit, offset, order, orderDirection, linkedNotebookGuid, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeTagRequest(Tag tag, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        QStringList expungedChildTagLocalUids;
        bool res = m_pLocalStorageManager->expungeTag(tag, expungedChildTagLocalUids, errorDescription);
        if (!res) {
            Q_EMIT expungeTagFailed(tag, errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            m_pLocalStorageCacheManager->expungeTag(tag);

            for(auto it = expungedChildTagLocalUids.constBegin(), end = expungedChildTagLocalUids.constEnd(); it != end; ++it) {
                Tag dummyTag;
                dummyTag.setLocalUid(*it);
                m_pLocalStorageCacheManager->expungeTag(dummyTag);
            }
        }

        Q_EMIT expungeTagComplete(tag, expungedChildTagLocalUids, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge tag from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeTagFailed(tag, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeNotelessTagsFromLinkedNotebooksRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        bool res = m_pLocalStorageManager->expungeNotelessTagsFromLinkedNotebooks(errorDescription);
        if (!res) {
            Q_EMIT expungeNotelessTagsFromLinkedNotebooksFailed(errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->clearAllNotes();
            m_pLocalStorageCacheManager->clearAllResources();
        }

        Q_EMIT expungeNotelessTagsFromLinkedNotebooksComplete(requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge noteless tags from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeNotelessTagsFromLinkedNotebooksFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onGetResourceCountRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->enResourceCount(errorDescription);
        if (count < 0) {
            Q_EMIT getResourceCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getResourceCountComplete(count, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get resource count from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getResourceCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddResourceRequest(Resource resource, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->addEnResource(resource, errorDescription);
        if (!res) {
            Q_EMIT addResourceFailed(resource, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheResource(resource);
        }

        Q_EMIT addResourceComplete(resource, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't add resource to the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT addResourceFailed(resource, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateResourceRequest(Resource resource, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->updateEnResource(resource, errorDescription);
        if (!res) {
            Q_EMIT updateResourceFailed(resource, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheResource(resource);
        }

        Q_EMIT updateResourceComplete(resource, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't update resource in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT updateResourceFailed(resource, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindResourceRequest(Resource resource, bool withBinaryData, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool foundResourceInCache = false;
        if (m_useCache)
        {
            bool resourceHasGuid = resource.hasGuid();
            const QString uid = (resourceHasGuid ? resource.guid() : resource.localUid());
            LocalStorageCacheManager::WhichUid wu = (resourceHasGuid ? LocalStorageCacheManager::Guid : LocalStorageCacheManager::LocalUid);

            const Resource * pResource = m_pLocalStorageCacheManager->findResource(uid, wu);
            if (pResource) {
                resource = *pResource;
                foundResourceInCache = true;
            }
        }

        if (!foundResourceInCache)
        {
            bool res = m_pLocalStorageManager->findEnResource(resource, errorDescription, withBinaryData);
            if (!res) {
                Q_EMIT findResourceFailed(resource, withBinaryData, errorDescription, requestId);
                return;
            }

            if (withBinaryData && m_useCache) {
                m_pLocalStorageCacheManager->cacheResource(resource);
            }
        }
        else if (!withBinaryData)
        {
            resource.setDataBody(QByteArray());
            resource.setAlternateDataBody(QByteArray());
        }

        Q_EMIT findResourceComplete(resource, withBinaryData, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find resource within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findResourceFailed(resource, withBinaryData, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeResourceRequest(Resource resource, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->expungeEnResource(resource, errorDescription);
        if (!res) {
            Q_EMIT expungeResourceFailed(resource, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->expungeResource(resource);
        }

        Q_EMIT expungeResourceComplete(resource, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge resource from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeResourceFailed(resource, error, requestId);
    }
}

void LocalStorageManagerAsync::onGetSavedSearchCountRequest(QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        int count = m_pLocalStorageManager->savedSearchCount(errorDescription);
        if (count < 0) {
            Q_EMIT getSavedSearchCountFailed(errorDescription, requestId);
        }
        else {
            Q_EMIT getSavedSearchCountComplete(count, requestId);
        }
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get saved searches count from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT getSavedSearchCountFailed(error, requestId);
    }
}

void LocalStorageManagerAsync::onAddSavedSearchRequest(SavedSearch search, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->addSavedSearch(search, errorDescription);
        if (!res) {
            Q_EMIT addSavedSearchFailed(search, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheSavedSearch(search);
        }

        Q_EMIT addSavedSearchComplete(search, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't add saved search to the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT addSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onUpdateSavedSearchRequest(SavedSearch search, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->updateSavedSearch(search, errorDescription);
        if (!res) {
            Q_EMIT updateSavedSearchFailed(search, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->cacheSavedSearch(search);
        }

        Q_EMIT updateSavedSearchComplete(search, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't update saved search in the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT updateSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onFindSavedSearchRequest(SavedSearch search, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool foundCachedSavedSearch = false;
        if (m_useCache)
        {
            bool searchHasGuid = search.hasGuid();
            if (searchHasGuid || !search.localUid().isEmpty())
            {
                const QString uid = (searchHasGuid ? search.guid() : search.localUid());
                const LocalStorageCacheManager::WhichUid wg = (searchHasGuid ? LocalStorageCacheManager::Guid : LocalStorageCacheManager::LocalUid);

                const SavedSearch * pSearch = m_pLocalStorageCacheManager->findSavedSearch(uid, wg);
                if (pSearch) {
                    search = *pSearch;
                    foundCachedSavedSearch = true;
                }
            }
            else if (search.hasName() && !search.name().isEmpty())
            {
                const QString searchName = search.name();
                const SavedSearch * pSearch = m_pLocalStorageCacheManager->findSavedSearchByName(searchName);
                if (pSearch) {
                    search = *pSearch;
                    foundCachedSavedSearch = true;
                }
            }
        }

        if (!foundCachedSavedSearch)
        {
            bool res = m_pLocalStorageManager->findSavedSearch(search, errorDescription);
            if (!res) {
                Q_EMIT findSavedSearchFailed(search, errorDescription, requestId);
                return;
            }
        }

        Q_EMIT findSavedSearchComplete(search, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't find saved search within the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT findSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onListAllSavedSearchesRequest(size_t limit, size_t offset,
                                                             LocalStorageManager::ListSavedSearchesOrder::type order,
                                                             LocalStorageManager::OrderDirection::type orderDirection,
                                                             QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<SavedSearch> savedSearches = m_pLocalStorageManager->listAllSavedSearches(errorDescription, limit, offset,
                                                                                   order, orderDirection);
        if (savedSearches.isEmpty() && !errorDescription.isEmpty()) {
            Q_EMIT listAllSavedSearchesFailed(limit, offset, order, orderDirection,
                                            errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            const int numSavedSearches = savedSearches.size();
            for(int i = 0; i < numSavedSearches; ++i) {
                const SavedSearch & search = savedSearches[i];
                m_pLocalStorageCacheManager->cacheSavedSearch(search);
            }
        }

        Q_EMIT listAllSavedSearchesComplete(limit, offset, order, orderDirection, savedSearches, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list all saved searches from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listAllSavedSearchesFailed(limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onListSavedSearchesRequest(LocalStorageManager::ListObjectsOptions flag,
                                                          size_t limit, size_t offset,
                                                          LocalStorageManager::ListSavedSearchesOrder::type order,
                                                          LocalStorageManager::OrderDirection::type orderDirection,
                                                          QUuid requestId)
{
    try
    {
        ErrorString errorDescription;
        QList<SavedSearch> savedSearches = m_pLocalStorageManager->listSavedSearches(flag, errorDescription, limit,
                                                                                     offset, order, orderDirection);
        if (savedSearches.isEmpty() && !errorDescription.isEmpty()) {
            QNTRACE(QStringLiteral("Failed: ") << errorDescription);
            Q_EMIT listSavedSearchesFailed(flag, limit, offset, order, orderDirection,
                                           errorDescription, requestId);
            return;
        }

        if (m_useCache)
        {
            const int numSavedSearches = savedSearches.size();
            for(int i = 0; i < numSavedSearches; ++i) {
                const SavedSearch & search = savedSearches[i];
                m_pLocalStorageCacheManager->cacheSavedSearch(search);
            }
        }

        Q_EMIT listSavedSearchesComplete(flag, limit, offset, order, orderDirection, savedSearches, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't list all saved searches from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT listSavedSearchesFailed(flag, limit, offset, order, orderDirection, error, requestId);
    }
}

void LocalStorageManagerAsync::onExpungeSavedSearchRequest(SavedSearch search, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        bool res = m_pLocalStorageManager->expungeSavedSearch(search, errorDescription);
        if (!res) {
            Q_EMIT expungeSavedSearchFailed(search, errorDescription, requestId);
            return;
        }

        if (m_useCache) {
            m_pLocalStorageCacheManager->expungeSavedSearch(search);
        }

        Q_EMIT expungeSavedSearchComplete(search, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't expunge saved search from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT expungeSavedSearchFailed(search, error, requestId);
    }
}

void LocalStorageManagerAsync::onAccountHighUsnRequest(QString linkedNotebookGuid, QUuid requestId)
{
    try
    {
        ErrorString errorDescription;

        qint32 updateSequenceNumber = m_pLocalStorageManager->accountHighUsn(linkedNotebookGuid, errorDescription);
        if (updateSequenceNumber < 0) {
            Q_EMIT accountHighUsnFailed(linkedNotebookGuid, errorDescription, requestId);
            return;
        }

        Q_EMIT accountHighUsnComplete(updateSequenceNumber, linkedNotebookGuid, requestId);
    }
    catch(const std::exception & e)
    {
        ErrorString error(QT_TR_NOOP("Can't get account high USN from the local storage: caught exception"));
        error.details() = QString::fromUtf8(e.what());
        SysInfo sysInfo;
        QNERROR(error << QStringLiteral("; backtrace: ") << sysInfo.stackTrace());
        Q_EMIT accountHighUsnFailed(linkedNotebookGuid, error, requestId);
    }
}

} // namespace quentier
